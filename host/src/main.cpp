// retrobridge_host — RetroBridge as drive D1 of an emulated Atari over NetSIO
// (Issue #32, E1b; ADR-016). Reuses the real drive context: DriveBay,
// SioCommandDispatcher, ApplyDriveRequest and SioSession over NetSioSioTransport,
// with a loopback UDP port, the PC clock and an ATR read through
// StdioImageRepository. Read-only drive. Not firmware: no UART, GPIO or SIO timing.
//
//   retrobridge_host --image <file.atr> [--port 9997] [--peer netsiohub|fep003]
//                    [--log <file.jsonl>] [--seconds N]
//
// --peer netsiohub accepts the hub's trailing byte (decision D2); the default is
// strict FEP-003. --seconds 0 runs until the process is stopped.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "file_identity.hpp"
#include "host_clock.hpp"
#include "jsonl_log.hpp"
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
    const char* log = "retrobridge_host.jsonl";
    unsigned port = 9997;
    unsigned seconds = 0;
    rb::netsio::PeerProfile profile = rb::netsio::PeerProfile::Fep003Strict;
};

int usage()
{
    std::fputs("usage: retrobridge_host --image <file.atr> [--port 9997] [--peer netsiohub|fep003]\n"
               "                        [--log <file.jsonl>] [--seconds N]\n",
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
    return options.image != nullptr;
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

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        return usage();
    }
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

    // The log is opened for append: if it named the image, every line would be
    // written into the ATR being served. The image is open read-only, so it
    // exists; compare --log with the effective image path by file identity, not
    // spelling, and refuse before the log is opened.
    if (rb::host::compareFiles(options.log, imagePath) == rb::host::FileIdentity::Same) {
        std::fprintf(stderr, "retrobridge_host: --log names the image file %s; refusing to write into the image\n",
                     imagePath);
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
                  options.profile == rb::netsio::PeerProfile::NetsiohubTrailingByte ? "netsiohub" : "fep003",
                  port.localPort(), options.port);
    log.event("start", detail);
    std::printf("retrobridge_host: D1=%s, %s\n", options.image, detail);

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

    transport.disconnect();
    log.event("disconnect", "C0 sent");
    log.summary(transport.stats());
    std::fclose(logFile);
    const rb::netsio::NetSioStats& s = transport.stats();
    std::printf("retrobridge_host: frames=%u ack/nak=%u empty=%u credit_requests=%u resets=%u\n", s.framesDelivered,
                s.validSyncResponses, s.emptySyncResponses, s.creditRequests, s.resets);
    return 0;
}
