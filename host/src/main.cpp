// retrobridge_host — RetroBridge as drive D1 of an emulated Atari over NetSIO
// (Issue #32, E1b; ADR-016). Reuses the real drive context: DriveBay,
// SioCommandDispatcher, ApplyDriveRequest and SioSession over NetSioSioTransport,
// with a loopback UDP port, the PC clock and ATRs read through
// StdioImageRepository. Read-only drive. Not firmware: no UART, GPIO or SIO timing.
//
//   retrobridge_host --image <file.atr> [--port 9997] [--peer netsiohub|fep003]
//                    [--log <file.jsonl>] [--seconds N]
//   retrobridge_host --images <folder> [same options]
//
// --image serves one ATR for the whole run (E1b, E1c). --images (Issue #38,
// ADR-017) serves the RetroBridge boot menu as D1 after every cold reset: the
// Atari lists the folder's .atr files, the user picks one with a key, the host
// validates it and mounts it as D1, and the menu boots it.
//
// --peer netsiohub accepts the hub's trailing byte (decision D2); the default is
// strict FEP-003. --seconds 0 runs until the process is stopped.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "file_identity.hpp"
#include "host_clock.hpp"
#include "host_image_repository.hpp"
#include "jsonl_log.hpp"
#include "menu_catalog.hpp"
#include "menu_controller.hpp"
#include "menu_image.hpp"
#include "menu_sio_transport.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/netsio/netsio_sio_transport.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"
#include "rb/sio/sio_session.hpp"
#include "rb/storage/image_path.hpp"
#include "rb/storage/stdio_image_repository.hpp"
#include "udp_datagram_port.hpp"

namespace {

struct Options {
    const char* image = nullptr;
    const char* images = nullptr;
    const char* log = "retrobridge_host.jsonl";
    unsigned port = 9997;
    unsigned seconds = 0;
    rb::netsio::PeerProfile profile = rb::netsio::PeerProfile::Fep003Strict;
};

int usage()
{
    std::fputs("usage: retrobridge_host --image <file.atr> | --images <folder>\n"
               "                        [--port 9997] [--peer netsiohub|fep003] [--log <file.jsonl>] [--seconds N]\n",
               stderr);
    return 2;
}

bool parse(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
        if (value == nullptr) {
            return false;
        }
        if (std::strcmp(arg, "--image") == 0) {
            options.image = value;
        } else if (std::strcmp(arg, "--images") == 0) {
            options.images = value;
        } else if (std::strcmp(arg, "--log") == 0) {
            options.log = value;
        } else if (std::strcmp(arg, "--port") == 0) {
            options.port = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
            if (options.port == 0 || options.port > 65535) {
                return false;
            }
        } else if (std::strcmp(arg, "--seconds") == 0) {
            options.seconds = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
        } else if (std::strcmp(arg, "--peer") == 0) {
            if (std::strcmp(value, "netsiohub") == 0) {
                options.profile = rb::netsio::PeerProfile::NetsiohubTrailingByte;
            } else if (std::strcmp(value, "fep003") == 0) {
                options.profile = rb::netsio::PeerProfile::Fep003Strict;
            } else {
                return false;
            }
        } else {
            return false;
        }
        ++i;
    }
    return (options.image != nullptr) != (options.images != nullptr);  // exactly one of them
}

// Splits "dir/name.atr" into a repository base directory and an image location.
bool splitImagePath(const char* path, char (&base)[128], char (&name)[128])
{
    const char* slash = nullptr;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            slash = p;
        }
    }
    const char* file = slash != nullptr ? slash + 1 : path;
    const std::size_t baseLength = slash != nullptr ? static_cast<std::size_t>(slash - path) : 1;
    if (baseLength == 0 || baseLength >= sizeof base || std::strlen(file) == 0 || std::strlen(file) >= sizeof name) {
        return false;
    }
    if (slash != nullptr) {
        std::memcpy(base, path, baseLength);
        base[baseLength] = '\0';
    } else {
        std::strcpy(base, ".");
    }
    std::strcpy(name, file);
    return true;
}

// The log is opened for append: if it named a served image, every line would be
// written into that ATR. Compare --log with the effective path the repository
// opens (base + '/' + location, built by the same function), by file identity,
// not spelling, and refuse before the log is opened.
bool logNamesImage(const char* log, const char* base, const char* name)
{
    const auto location = rb::app::ImageLocation::from(name);
    char imagePath[rb::storage::kMaxImagePath + 1];
    if (!location.ok() ||
        !rb::storage::build_image_path(base, location.value(), rb::core::Span<char>{imagePath}).ok()) {
        return false;
    }
    if (rb::host::compareFiles(log, imagePath) == rb::host::FileIdentity::Same) {
        std::fprintf(stderr, "retrobridge_host: --log names the image file %s; refusing to write into the image\n",
                     imagePath);
        return true;
    }
    return false;
}

// NetSIO observer of --images: logs every datagram and tells the menu about
// each inbound COLD_RESET (FF), the only event that brings the menu back.
class ResetWatch final : public rb::netsio::NetSioObserver {
public:
    ResetWatch(rb::host::JsonlLog& log, rb::host::MenuController& controller) : log_(log), controller_(controller) {}

    void onDatagram(bool outbound, rb::core::Span<const uint8_t> datagram) override
    {
        log_.onDatagram(outbound, datagram);
        if (!outbound && !datagram.empty() && datagram[0] == 0xFF) {
            controller_.onColdReset();
        }
    }

private:
    rb::host::JsonlLog& log_;
    rb::host::MenuController& controller_;
};

class LogMenuEvents final : public rb::host::MenuEvents {
public:
    explicit LogMenuEvents(rb::host::JsonlLog& log) : log_(log) {}
    void menuEvent(const char* name, const char* detail) override { log_.event(name, detail); }

private:
    rb::host::JsonlLog& log_;
};

// Registers with the hub and runs the session until --seconds or a dead hub.
// afterStep runs after every step, outside any transaction.
// onStop runs once, before the summary, while the log is still open.
template <typename AfterStep, typename OnStop>
int serve(const Options& options, rb::host::HostClock& clock, rb::host::JsonlLog& log, std::FILE* logFile,
          rb::netsio::NetSioSioTransport& transport, rb::sio::SioSession& session, AfterStep afterStep, OnStop onStop)
{
    if (!transport.connect().ok()) {
        log.event("connect", "failed: no PING_RESPONSE");
        log.summary(transport.stats());
        std::fclose(logFile);
        std::fprintf(stderr, "retrobridge_host: no answer from 127.0.0.1:%u\n", options.port);
        return 1;
    }
    log.event("connect", "registered");
    std::puts("retrobridge_host: registered; serving D1");

    const uint64_t deadline = options.seconds == 0 ? UINT64_MAX : clock.nowMicros() + options.seconds * 1000000ull;
    unsigned failuresInARow = 0;
    while (clock.nowMicros() < deadline) {
        const rb::sio::StepOutcome outcome = session.step();
        // Also after Idle steps: with --images a frame dropped after a cold reset
        // ends as Idle, and only afterStep() puts the menu back on D1.
        afterStep();
        if (outcome == rb::sio::StepOutcome::Idle) {
            continue;
        }
        log.step(outcome);
        if (outcome == rb::sio::StepOutcome::TransportFailed) {
            // A dead hub fails every receive at once; stop instead of spinning.
            if (++failuresInARow >= 50) {
                log.event("stop", "50 transport failures in a row");
                break;
            }
            clock.waitUntil(clock.nowMicros() + 20000);
        } else {
            failuresInARow = 0;
        }
    }

    onStop();
    transport.disconnect();
    log.event("disconnect", "C0 sent");
    log.summary(transport.stats());
    std::fclose(logFile);
    const rb::netsio::NetSioStats& s = transport.stats();
    std::printf("retrobridge_host: frames=%u ack/nak=%u empty=%u credit_requests=%u resets=%u\n", s.framesDelivered,
                s.validSyncResponses, s.emptySyncResponses, s.creditRequests, s.resets);
    return 0;
}

const char* peerName(const Options& options)
{
    return options.profile == rb::netsio::PeerProfile::NetsiohubTrailingByte ? "netsiohub" : "fep003";
}

int runImage(const Options& options)
{
    char base[128];
    char name[128];
    if (!splitImagePath(options.image, base, name)) {
        std::fprintf(stderr, "retrobridge_host: invalid image path\n");
        return 2;
    }
    // Drive D1 with the image mounted before the bus starts.
    static rb::storage::StdioImageRepository<1> repository{base};
    const auto location = rb::app::ImageLocation::from(name);
    if (!location.ok()) {
        std::fprintf(stderr, "retrobridge_host: invalid image name\n");
        return 2;
    }
    // The file the repository opens is base + '/' + location, built by the same
    // function it uses. On POSIX "images\disk.atr" is split at the '\' and opened
    // as "images/disk.atr", so the --image text does not name that file.
    char imagePath[rb::storage::kMaxImagePath + 1];
    if (!rb::storage::build_image_path(base, location.value(), rb::core::Span<char>{imagePath}).ok()) {
        std::fprintf(stderr, "retrobridge_host: invalid image path\n");
        return 2;
    }
    const auto source = repository.open(location.value());
    if (!source.ok()) {
        std::fprintf(stderr, "retrobridge_host: cannot open image %s\n", imagePath);
        return 1;
    }
    // The image is open read-only, so it exists.
    if (logNamesImage(options.log, base, name)) {
        return 2;
    }
    std::FILE* logFile = std::fopen(options.log, "a");
    if (logFile == nullptr) {
        std::fprintf(stderr, "retrobridge_host: cannot open log %s\n", options.log);
        return 2;
    }

    rb::host::HostClock clock;
    rb::host::JsonlLog log{logFile, clock};

    auto image = rb::domain::DiskImage::open(*source.value());
    if (!image.ok()) {
        std::fprintf(stderr, "retrobridge_host: not a valid ATR image\n");
        return 1;
    }
    rb::app::DriveBay bay{1};
    if (!bay.mount(rb::domain::DriveNumber::from(1).value(), std::move(image.value()), location.value()).ok()) {
        std::fprintf(stderr, "retrobridge_host: mount failed\n");
        return 1;
    }

    rb::host::UdpDatagramPort port;
    if (!port.open(static_cast<uint16_t>(options.port)).ok()) {
        std::fprintf(stderr, "retrobridge_host: cannot open UDP socket to 127.0.0.1:%u\n", options.port);
        return 1;
    }
    rb::netsio::NetSioConfig config;
    config.profile = options.profile;
    rb::netsio::NetSioSioTransport transport{port, clock, config, &log};
    rb::app::InlineDriveMailbox mailbox;
    rb::sio::SioCommandDispatcher dispatcher{bay};
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    rb::sio::SioSession session{transport, clock, dispatcher, apply};

    // Worst case: 127-byte name, "netsiohub", two ports of up to 10 digits (196
    // bytes with the literals). Sized from the name buffer so GCC's
    // -Wformat-truncation can prove the output fits.
    char detail[sizeof name + 96];
    std::snprintf(detail, sizeof detail, "image=%s peer=%s local=127.0.0.1:%u hub=127.0.0.1:%u", name,
                  peerName(options), port.localPort(), options.port);
    log.event("start", detail);
    std::printf("retrobridge_host: D1=%s, %s\n", options.image, detail);
    return serve(options, clock, log, logFile, transport, session, [] {}, [] {});
}

int runMenu(const Options& options)
{
    const std::size_t folderLength = std::strlen(options.images);
    if (folderLength == 0 || folderLength > rb::storage::StdioImageRepository<2>::kMaxBasePath) {
        std::fprintf(stderr, "retrobridge_host: invalid images folder\n");
        return 2;
    }
    static rb::host::MenuCatalog catalog;
    if (!rb::host::readCatalog(options.images, catalog)) {
        std::fprintf(stderr, "retrobridge_host: cannot read images folder %s\n", options.images);
        return 2;
    }
    if (catalog.count() == 0) {
        std::fprintf(stderr, "retrobridge_host: no .atr images in %s\n", options.images);
        return 2;
    }
    for (std::size_t i = 0; i < catalog.count(); ++i) {
        if (logNamesImage(options.log, options.images, catalog.name(i))) {
            return 2;
        }
    }
    std::FILE* logFile = std::fopen(options.log, "a");
    if (logFile == nullptr) {
        std::fprintf(stderr, "retrobridge_host: cannot open log %s\n", options.log);
        return 2;
    }

    rb::host::HostClock clock;
    rb::host::JsonlLog log{logFile, clock};
    static rb::host::MenuImage menu{catalog};
    static rb::host::HostImageRepository repository{options.images, menu};
    rb::app::DriveBay bay{1};
    rb::app::InlineDriveMailbox mailbox;
    LogMenuEvents events{log};
    rb::host::MenuController controller{catalog, repository, mailbox, &events};
    ResetWatch watch{log, controller};

    rb::host::UdpDatagramPort port;
    if (!port.open(static_cast<uint16_t>(options.port)).ok()) {
        std::fclose(logFile);
        std::fprintf(stderr, "retrobridge_host: cannot open UDP socket to 127.0.0.1:%u\n", options.port);
        return 1;
    }
    rb::netsio::NetSioConfig config;
    config.profile = options.profile;
    rb::netsio::NetSioSioTransport netsio{port, clock, config, &watch};
    rb::host::MenuSioTransport transport{netsio, clock, controller};
    rb::sio::SioCommandDispatcher dispatcher{bay};
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    rb::sio::SioSession session{transport, clock, dispatcher, apply};

    char detail[rb::storage::StdioImageRepository<2>::kMaxBasePath + 128];
    std::snprintf(detail, sizeof detail, "images=%s entries=%u atr_seen=%u peer=%s local=127.0.0.1:%u hub=127.0.0.1:%u",
                  options.images, static_cast<unsigned>(catalog.count()), static_cast<unsigned>(catalog.atrSeen()),
                  peerName(options), port.localPort(), options.port);
    log.event("start", detail);
    for (std::size_t i = 0; i < catalog.count(); ++i) {
        char entry[rb::host::MenuCatalog::kMaxName + 16];
        std::snprintf(entry, sizeof entry, "%c %s", static_cast<char>('A' + i), catalog.name(i));
        log.event("menu_entry", entry);
    }
    std::printf("retrobridge_host: menu with %u image(s), %s\n", static_cast<unsigned>(catalog.count()), detail);

    // D1 starts with the menu, as after a cold reset.
    controller.onColdReset();
    controller.service();
    return serve(options, clock, log, logFile, netsio, session, [&controller] { controller.service(); },
                 [&transport, &log] {
                     const rb::host::MenuTransportStats& m = transport.stats();
                     char stats[96];
                     std::snprintf(stats, sizeof stats, "menu_commands=%u menu_naks=%u dropped_after_reset=%u",
                                   m.menuCommands, m.menuNaks, m.droppedAfterReset);
                     log.event("menu_stats", stats);
                     std::printf("retrobridge_host: %s\n", stats);
                 });
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        return usage();
    }
    return options.images != nullptr ? runMenu(options) : runImage(options);
}
