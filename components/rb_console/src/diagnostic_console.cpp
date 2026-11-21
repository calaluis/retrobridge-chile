#include "rb/console/diagnostic_console.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "esp_console.h"
#include "esp_rom_crc.h"
#include "rb/app/diagnostics/latency_stats.hpp"
#include "rb/esp/memory_snapshot.hpp"
#include "rb/hwdiag/report_writer.hpp"

namespace rb::console {

using rb::app::BenchmarkMode;
using rb::app::ImageLocation;
using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::DriveNumber;
using rb::domain::SectorNumber;

namespace {

ConsoleContext* g_context = nullptr;  // set once before the REPL starts

// Benchmark samples: fixed storage, no allocation (H3-12).
std::array<uint32_t, 2048> g_latency_samples{};

const char* error_name(ErrorCode error)
{
    switch (error) {
    case ErrorCode::InvalidLocation: return "InvalidLocation";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::NoFreeSlot: return "NoFreeSlot";
    case ErrorCode::IoError: return "IoError";
    case ErrorCode::InvalidAtrHeader: return "InvalidAtrHeader";
    case ErrorCode::UnsupportedSectorSize: return "UnsupportedSectorSize";
    case ErrorCode::UnsupportedAtrLayout: return "UnsupportedAtrLayout";
    case ErrorCode::ImageTooSmall: return "ImageTooSmall";
    case ErrorCode::ImageTooLarge: return "ImageTooLarge";
    case ErrorCode::SectorOutOfRange: return "SectorOutOfRange";
    case ErrorCode::NoDiskMounted: return "NoDiskMounted";
    case ErrorCode::DriveNotConfigured: return "DriveNotConfigured";
    case ErrorCode::MailboxBusy: return "MailboxBusy";
    case ErrorCode::BoardNotConfirmed: return "BoardNotConfirmed";
    case ErrorCode::InvalidPinAssignment: return "InvalidPinAssignment";
    case ErrorCode::StorageNotMounted: return "StorageNotMounted";
    case ErrorCode::WriteProtected: return "WriteProtected";
    case ErrorCode::DensityMismatch: return "DensityMismatch";
    default: return "Error";
    }
}

const char* error_code_name(uint8_t code)
{
    return error_name(static_cast<ErrorCode>(code));
}

uint64_t now_us()
{
    return g_context->clock.nowMicros();
}

const char* density_name(rb::domain::Density density)
{
    switch (density) {
    case rb::domain::Density::Single: return "single (SD)";
    case rb::domain::Density::Enhanced: return "enhanced (ED)";
    case rb::domain::Density::Double: return "double (DD)";
    case rb::domain::Density::NonStandard: return "non-standard";
    default: return "unknown";
    }
}

void print_board_warning()
{
    std::printf("HARDWARE NOT VALIDATED: board profile '%s' is unconfirmed.\n"
                "The microSD bus, board GPIOs and SIO stay disabled until the board is identified (H3c/H4).\n",
                g_context->board.name);
}

bool require_storage()
{
    if (g_context->storage.mounted()) {
        return true;
    }
    std::printf("storage not mounted (run 'sd mount')\n");
    if (!g_context->board.hardwareConfirmed) {
        print_board_warning();
    }
    return false;
}

std::optional<ImageLocation> parse_location(int argc, char** argv, int index)
{
    if (argc <= index) {
        std::printf("missing image path (relative to %s)\n", rb::esp::SdCardStorage::kMountPoint);
        return std::nullopt;
    }
    const auto location = ImageLocation::from(argv[index]);
    if (!location.ok()) {
        std::printf("invalid image path\n");
        return std::nullopt;
    }
    return location.value();
}

// "games/sd-720.atr" -> "sd-720": the image name used by tools/compare_crc.py.
void image_name(const char* path, std::array<char, 64>& out)
{
    const char* base = std::strrchr(path, '/');
    base = base != nullptr ? base + 1 : path;
    std::snprintf(out.data(), out.size(), "%s", base);
    char* dot = std::strrchr(out.data(), '.');
    if (dot != nullptr && (std::strcmp(dot, ".atr") == 0 || std::strcmp(dot, ".ATR") == 0)) {
        *dot = '\0';
    }
}

void print_memory(const char* label)
{
    const rb::esp::MemorySnapshot m = rb::esp::capture_memory();
    std::printf("%s free8=%u min8=%u largest8=%u internal=%u\n", label, static_cast<unsigned>(m.free8bit),
                static_cast<unsigned>(m.minimumFree8bit), static_cast<unsigned>(m.largestFree8bit),
                static_cast<unsigned>(m.freeInternal));
}

// --- board -------------------------------------------------------------------------------

int cmd_board(int, char**)
{
    const rb::board::BoardProfile& board = g_context->board;
    std::printf("board: %s, confirmed: %s\n", board.name, board.hardwareConfirmed ? "yes" : "no");
    const auto valid = rb::board::validate_for_storage(board);
    if (!valid.ok()) {
        std::printf("storage profile: %s\n", error_name(valid.error()));
        if (!board.hardwareConfirmed) {
            print_board_warning();
        }
        return 1;
    }
    std::printf("storage profile: valid, warnings 0x%02x\n", valid.value());
    return 0;
}

// --- sd ------------------------------------------------------------------------------------

int sd_ls(const char* subdirectory)
{
    std::array<char, 300> path{};
    const int written = std::snprintf(path.data(), path.size(), "%s%s%s", rb::esp::SdCardStorage::kMountPoint,
                                      subdirectory != nullptr ? "/" : "", subdirectory != nullptr ? subdirectory : "");
    if (written < 0 || static_cast<std::size_t>(written) >= path.size()) {
        std::printf("path too long\n");
        return 1;
    }
    DIR* dir = opendir(path.data());
    if (dir == nullptr) {
        std::printf("cannot open %s\n", path.data());
        return 1;
    }
    unsigned entries = 0;
    while (dirent* entry = readdir(dir)) {
        std::array<char, 600> full{};
        const int fullLength = std::snprintf(full.data(), full.size(), "%s/%s", path.data(), entry->d_name);
        struct stat info {};
        if (fullLength < 0 || static_cast<std::size_t>(fullLength) >= full.size()) {
            std::printf("  (name too long) %s\n", entry->d_name);
        } else if (stat(full.data(), &info) == 0 && S_ISDIR(info.st_mode)) {
            std::printf("  [dir] %s\n", entry->d_name);
        } else {
            std::printf("  %10ld %s\n", static_cast<long>(info.st_size), entry->d_name);
        }
        ++entries;
    }
    closedir(dir);
    std::printf("%u entries\n", entries);
    return 0;
}

int cmd_sd(int argc, char** argv)
{
    const char* action = argc > 1 ? argv[1] : "status";
    if (std::strcmp(action, "mount") == 0) {
        const auto mounted = g_context->storage.mount(g_context->board);
        g_context->diagnostics.recordMount(mounted.ok(), g_context->storage.lastEspError(),
                                           mounted.ok() ? 0 : static_cast<uint8_t>(mounted.error()), now_us());
        if (!mounted.ok()) {
            std::printf("mount failed: %s (esp_err 0x%x)\n", error_name(mounted.error()),
                        static_cast<unsigned>(g_context->storage.lastEspError()));
            if (mounted.error() == ErrorCode::BoardNotConfirmed) {
                print_board_warning();
            }
            return 1;
        }
        std::printf("mounted at %s (pin warnings 0x%02x)\n", rb::esp::SdCardStorage::kMountPoint,
                    g_context->storage.pinWarnings());
        return 0;
    }
    if (std::strcmp(action, "unmount") == 0) {
        if (g_context->repository.open_count() != 0) {
            std::printf("refused: %u image file(s) still open (unmount the drive first)\n",
                        static_cast<unsigned>(g_context->repository.open_count()));
            return 1;
        }
        const auto unmounted = g_context->storage.unmount();
        if (unmounted.ok()) {
            g_context->diagnostics.recordUnmount(now_us());
        }
        std::printf("%s\n", unmounted.ok() ? "unmounted" : error_name(unmounted.error()));
        return unmounted.ok() ? 0 : 1;
    }
    if (std::strcmp(action, "ls") == 0) {
        return require_storage() ? sd_ls(argc > 2 ? argv[2] : nullptr) : 1;
    }
    std::printf("storage: %s, open image files: %u\n", g_context->storage.mounted() ? "mounted" : "not mounted",
                static_cast<unsigned>(g_context->repository.open_count()));
    return 0;
}

// --- atr ------------------------------------------------------------------------------------

class PrintingSink final : public rb::app::SectorCrcSink {
public:
    void onSector(uint16_t sector, uint16_t, uint32_t crc) override
    {
        std::printf("CRC %u %08" PRIx32 "\n", static_cast<unsigned>(sector), crc);
    }
    void onReadError(uint16_t sector, ErrorCode error) override
    {
        std::printf("ERR %u %s\n", static_cast<unsigned>(sector), error_name(error));
        lastError = static_cast<uint8_t>(error);
    }

    uint8_t lastError = 0;
};

int atr_info(const ImageLocation& location)
{
    const auto info = g_context->inspect.execute(location);
    if (!info.ok()) {
        std::printf("error: %s\n", error_name(info.error()));
        return 1;
    }
    const auto& i = info.value();
    std::printf("file size: %" PRIu32 " bytes\nsectors: %u x %u bytes (sectors 1-3: 128)\ndensity: %s\n", i.fileSize,
                static_cast<unsigned>(i.geometry.sectorCount()), static_cast<unsigned>(i.geometry.sectorSize()),
                density_name(i.geometry.density()));
    std::printf("D-8: header size %s, ignored trailing bytes: %" PRIu32 "\n",
                i.headerSizeMismatch ? "differs from the file" : "matches", i.ignoredTrailingBytes);
    return 0;
}

int atr_crc(const ImageLocation& location, const char* path)
{
    std::array<char, 64> name{};
    image_name(path, name);
    std::printf("IMAGE %s\n", name.data());
    PrintingSink sink;
    const auto report = g_context->verify.execute(location, sink);
    if (!report.ok()) {
        std::printf("error: %s\n", error_name(report.error()));
        return 1;
    }
    std::printf("DONE %s sectors=%" PRIu32 " read_errors=%" PRIu32 "\n", name.data(), report.value().sectors,
                report.value().readErrors);
    g_context->diagnostics.recordVerify(report.value().sectors, report.value().readErrors, sink.lastError, now_us());
    return report.value().readErrors == 0 ? 0 : 1;
}

int atr_bench(const ImageLocation& location, int argc, char** argv)
{
    const bool random = argc > 3 && std::strcmp(argv[3], "rand") == 0;
    const uint32_t count = argc > 4 ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10)) : (random ? 1000u : 0u);
    rb::app::LatencyStats stats{Span<uint32_t>{g_latency_samples}};

    print_memory("memory before:");
    const auto report =
        g_context->benchmark.execute(location, random ? BenchmarkMode::Random : BenchmarkMode::Sequential, count, stats);
    print_memory("memory after: ");
    if (!report.ok()) {
        std::printf("error: %s\n", error_name(report.error()));
        return 1;
    }
    const auto& r = report.value();
    const auto s = stats.summarize();
    std::printf("mode=%s reads=%" PRIu32 " errors=%" PRIu32 " open_us=%" PRIu32 " first_read_us=%" PRIu32 "\n",
                random ? "random" : "sequential", r.reads, r.readErrors, r.openMicros, r.firstReadMicros);
    std::printf("latency_us min=%" PRIu32 " mean=%" PRIu32 " p50=%" PRIu32 " p95=%" PRIu32 " p99=%" PRIu32
                " max=%" PRIu32 " (samples=%" PRIu32 " dropped=%" PRIu32 ")\n",
                s.min, s.mean, s.p50, s.p95, s.p99, s.max, s.count, s.dropped);
    // Raw comparison only: whether these were physical or cached reads depends on
    // the mode and geometry (H3-12); no claim about the objective is made here.
    std::printf("max %s the %" PRIu32 " us budget in this run\n", s.max <= kSectorReadBudgetUs ? "within" : "EXCEEDS",
                kSectorReadBudgetUs);
    g_context->diagnostics.recordBenchmark(
        rb::hwdiag::LatencyFigures{s.count, s.dropped, s.min, s.mean, s.p50, s.p95, s.p99, s.max}, r.reads,
        r.readErrors, now_us());
    return 0;
}

int cmd_atr(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: atr info|crc <path> | atr bench <path> [seq|rand] [count]\n");
        return 1;
    }
    if (!require_storage()) {
        return 1;
    }
    const auto location = parse_location(argc, argv, 2);
    if (!location) {
        return 1;
    }
    if (std::strcmp(argv[1], "info") == 0) {
        return atr_info(*location);
    }
    if (std::strcmp(argv[1], "crc") == 0) {
        return atr_crc(*location, argv[2]);
    }
    if (std::strcmp(argv[1], "bench") == 0) {
        return atr_bench(*location, argc, argv);
    }
    std::printf("unknown atr action\n");
    return 1;
}

// --- drive: both contexts in sequence (H3-7) ---------------------------------------------------

int print_outcome()
{
    const std::optional<rb::app::DriveOutcome> outcome = g_context->collectReply.execute();
    if (!outcome) {
        std::printf("no reply\n");
        return 1;
    }
    if (outcome->error) {
        std::printf("drive error: %s\n", error_name(*outcome->error));
        return 1;
    }
    std::printf("ok\n");
    return 0;
}

int cmd_drive(int argc, char** argv)
{
    const DriveNumber d1 = DriveNumber::from(1).value();
    const char* action = argc > 1 ? argv[1] : "status";

    if (std::strcmp(action, "mount") == 0 || std::strcmp(action, "unmount") == 0) {
        const bool mount = std::strcmp(action, "mount") == 0;
        if (mount) {
            if (!require_storage()) {
                return 1;
            }
            const auto location = parse_location(argc, argv, 2);
            if (!location) {
                return 1;
            }
            const auto report = g_context->mountImage.execute(d1, *location);  // storage context
            if (!report.ok()) {
                std::printf("error: %s\n", error_name(report.error()));
                return 1;
            }
        } else {
            const auto submitted = g_context->unmountImage.execute(d1);
            if (!submitted.ok()) {
                std::printf("error: %s\n", error_name(submitted.error()));
                return 1;
            }
        }
        (void)g_context->applyRequest.execute();  // drive context, in sequence
        return print_outcome();                   // storage context closes released sources
    }

    if (std::strcmp(action, "read") == 0) {
        if (argc < 3) {
            std::printf("usage: drive read <sector>\n");
            return 1;
        }
        const auto sector = SectorNumber::from(static_cast<uint16_t>(std::strtoul(argv[2], nullptr, 10)));
        if (!sector.ok()) {
            std::printf("invalid sector\n");
            return 1;
        }
        std::array<uint8_t, 256> buffer{};
        const auto read = g_context->readSector.execute(d1, sector.value(), Span<uint8_t>{buffer});
        if (!read.ok()) {
            g_context->diagnostics.recordSectorReadError(sector.value().value(), static_cast<uint8_t>(read.error()),
                                                         now_us());
            std::printf("error: %s\n", error_name(read.error()));
            return 1;
        }
        std::printf("sector %u: %u bytes, crc32 %08" PRIx32 ", first bytes:", static_cast<unsigned>(sector.value().value()),
                    static_cast<unsigned>(read.value()), rom_crc32(buffer.data(), read.value()));
        for (unsigned i = 0; i < 16 && i < read.value(); ++i) {
            std::printf(" %02x", buffer[i]);
        }
        std::printf("\n");
        return 0;
    }

    const auto status = g_context->driveStatus.execute(d1);
    if (!status.ok()) {
        std::printf("error: %s\n", error_name(status.error()));
        return 1;
    }
    std::printf("D1: %s, density %s, last operation %s\n", status.value().diskPresent() ? "disk" : "empty",
                density_name(status.value().density()), status.value().lastOperationFailed() ? "failed" : "ok");
    return 0;
}

// --- diag: autonomous diagnostics (H4) ---------------------------------------------------------------

class ConsoleLineSink final : public rb::hwdiag::LineSink {
public:
    void line(const char* text) override { std::printf("%s\n", text); }
};

int cmd_diag(int argc, char** argv)
{
    const char* action = argc > 1 ? argv[1] : "status";
    ConsoleLineSink sink;
    rb::hwdiag::ReportWriter writer{sink, error_code_name};
    const rb::hwdiag::DiagnosticsRecorder& recorder = g_context->diagnostics;
    if (std::strcmp(action, "status") == 0) {
        writer.writeStatus(g_context->system.capture(), recorder);
    } else if (std::strcmp(action, "memory") == 0) {
        writer.writeMemory(g_context->system.capture());
    } else if (std::strcmp(action, "storage") == 0) {
        writer.writeStorage(recorder);
    } else if (std::strcmp(action, "events") == 0) {
        writer.writeEvents(recorder);
    } else if (std::strcmp(action, "report") == 0) {
        writer.writeReport(g_context->system.capture(), recorder);
    } else {
        std::printf("usage: diag status|memory|storage|events|report\n");
        return 1;
    }
    return 0;
}

// --- misc -----------------------------------------------------------------------------------------

int cmd_mem(int, char**)
{
    print_memory("memory:");
    return 0;
}

int cmd_crc_selftest(int, char**)
{
    const char* check = "123456789";
    const uint32_t crc = rom_crc32(reinterpret_cast<const uint8_t*>(check), 9);
    std::printf("crc32(\"123456789\") = %08" PRIx32 " (expected cbf43926): %s\n", crc,
                crc == 0xCBF43926u ? "OK" : "MISMATCH");
    return crc == 0xCBF43926u ? 0 : 1;
}

// cas mount <path> | status | eject | rewind (cassette-cas C106, AC8). The path is relative to
// the microSD; a name with spaces can be quoted or typed as is.
int cmd_cas(int argc, char** argv)
{
    char line[rb::esp::CassetteService::kLineMax];
    std::size_t used = 0;
    line[0] = '\0';
    for (int i = 1; i < argc; ++i) {
        const int written = std::snprintf(line + used, sizeof line - used, "%s%s", i > 1 ? " " : "", argv[i]);
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof line - used) {
            std::printf("line too long\n");
            return 1;
        }
        used += static_cast<std::size_t>(written);
    }
    rb::esp::CassetteService& cassette = g_context->cassette;
    if (!cassette.started()) {
        std::printf("cassette disabled (no PSRAM or no microSD profile)\n");
        return 1;
    }
    char reply[rb::esp::CassetteService::kReplyMax];
    if (!cassette.request(line, reply, sizeof reply)) {
        std::printf("cassette busy: no reply\n");
        return 1;
    }
    std::printf("%s\n", reply);
    if (!cassette.motorEnabled()) {
        std::printf("(MOTOR not read: M3a not signed; DATA IN not driven: H4)\n");
    }
    return 0;
}

bool register_command(const char* name, const char* help, esp_console_cmd_func_t func)
{
    esp_console_cmd_t command{};
    command.command = name;
    command.help = help;
    command.func = func;
    return esp_console_cmd_register(&command) == ESP_OK;
}

}  // namespace

uint32_t rom_crc32(const uint8_t* data, std::size_t length)
{
    // The ROM routine inverts on entry and exit: seed 0 gives standard CRC-32 (H3-8).
    return esp_rom_crc32_le(0, data, static_cast<uint32_t>(length));
}

bool start_diagnostic_console(ConsoleContext& context)
{
    g_context = &context;

    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "retrobridge>";
    config.task_stack_size = 6144;
    esp_console_repl_t* repl = nullptr;
    if (esp_console_new_repl_stdio(&config, &repl) != ESP_OK) {
        return false;
    }
    const bool registered =
        esp_console_register_help_command() == ESP_OK &&
        register_command("board", "Show the board profile and whether hardware is validated", cmd_board) &&
        register_command("sd", "sd mount|unmount|status|ls [dir]: microSD storage", cmd_sd) &&
        register_command("atr", "atr info|crc <path> | atr bench <path> [seq|rand] [count]", cmd_atr) &&
        register_command("drive", "drive mount <path>|unmount|read <n>|status (contexts in sequence, H3-7)",
                         cmd_drive) &&
        register_command("cas", "cas mount <path>|status|eject|rewind: the cassette (paths on the microSD)",
                         cmd_cas) &&
        register_command("mem", "Show heap figures", cmd_mem) &&
        register_command("diag",
                         "diag status|memory|storage|events|report: internal diagnostics (report is CRC-framed; "
                         "check it with tools/check_diag_report.py)",
                         cmd_diag) &&
        register_command("crctest", "Check the ROM CRC-32 against the reference vector", cmd_crc_selftest);
    if (!registered) {
        return false;
    }
    return esp_console_start_repl(repl) == ESP_OK;
}

}  // namespace rb::console
