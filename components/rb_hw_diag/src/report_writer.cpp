#include "rb/hwdiag/report_writer.hpp"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rb::hwdiag {

namespace {

const char* on_off(bool value)
{
    return value ? "on" : "off";
}

const char* yes_no(bool value)
{
    return value ? "yes" : "no";
}

}  // namespace

void Crc32Accumulator::add(const uint8_t* data, std::size_t length) noexcept
{
    for (std::size_t i = 0; i < length; ++i) {
        crc_ ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc_ = (crc_ >> 1) ^ (0xEDB88320u & (0u - (crc_ & 1u)));
        }
    }
}

void ReportWriter::emit(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer_.data(), buffer_.size(), format, args);
    va_end(args);
    if (written < 0) {
        std::snprintf(buffer_.data(), buffer_.size(), "DIAG format-error");
    }
    // vsnprintf truncates to the buffer and always terminates: a long value
    // shortens the line, it never overruns it.
    if (framing_) {
        crc_.add(reinterpret_cast<const uint8_t*>(buffer_.data()), std::strlen(buffer_.data()));
        const uint8_t newline = '\n';
        crc_.add(&newline, 1);
        ++framedLines_;
    }
    sink_.line(buffer_.data());
}

const char* ReportWriter::errorName(uint8_t code) const
{
    if (code == 0) {
        return "none";
    }
    return errorName_ != nullptr ? errorName_(code) : "error";
}

void ReportWriter::writeStatus(const SystemSnapshot& system, const DiagnosticsRecorder& recorder)
{
    const FirmwareIdentity& f = system.firmware;
    emit("DIAG firmware project=%s version=%s idf=%s board=%s", f.project, f.version, f.idfVersion, f.board);
    emit("DIAG uptime us=%" PRIu64, system.uptimeUs);
    const BootRecord& b = system.boot;
    emit("DIAG boot reason=%s boots_since_power_on=%" PRIu32 " watchdog_resets=%" PRIu32 " panic_resets=%" PRIu32
         " brownout_resets=%" PRIu32,
         reset_reason_name(b.reason), b.bootsSincePowerOn, b.watchdogResets, b.panicResets, b.brownoutResets);
    const WatchdogConfig& w = system.watchdog;
    emit("DIAG watchdog task=%s task_timeout_ms=%" PRIu32 " task_panic=%s interrupt=%s interrupt_timeout_ms=%" PRIu32,
         on_off(w.taskWatchdogEnabled), w.taskWatchdogTimeoutMs, yes_no(w.taskWatchdogPanics),
         on_off(w.interruptWatchdogEnabled), w.interruptWatchdogTimeoutMs);
    const StorageHealth& s = recorder.storage();
    const uint32_t storageErrors = s.mountFailures + s.verifyReadErrors + s.sectorReadErrors + s.benchmarkErrors;
    emit("DIAG summary storage_mounted=%s storage_errors=%" PRIu32 " events=%" PRIu32 " events_lost=%" PRIu32,
         yes_no(s.mounted), storageErrors, recorder.events().total(), recorder.events().overwritten());
}

void ReportWriter::writeMemory(const SystemSnapshot& system)
{
    const MemoryFigures& m = system.memory;
    emit("DIAG memory free8=%" PRIu32 " min8=%" PRIu32 " largest8=%" PRIu32 " internal=%" PRIu32, m.free8bit,
         m.minimumFree8bit, m.largestFree8bit, m.freeInternal);
}

void ReportWriter::writeStorage(const DiagnosticsRecorder& recorder)
{
    const StorageHealth& s = recorder.storage();
    emit("DIAG storage mounted=%s mount_attempts=%" PRIu32 " mount_failures=%" PRIu32 " last_esp_err=0x%" PRIx32
         " last_error=%s",
         yes_no(s.mounted), s.mountAttempts, s.mountFailures, static_cast<uint32_t>(s.lastMountEspError),
         errorName(s.lastErrorCode));
    emit("DIAG reads verify_runs=%" PRIu32 " sectors=%" PRIu32 " verify_errors=%" PRIu32 " sector_read_errors=%" PRIu32,
         s.verifyRuns, s.sectorsVerified, s.verifyReadErrors, s.sectorReadErrors);
    if (!s.hasLatency) {
        emit("DIAG latency runs=%" PRIu32 " reads=%" PRIu32 " errors=%" PRIu32 " budget_us=%" PRIu32
             " budget_exceeded=%" PRIu32 " last=none",
             s.benchmarkRuns, s.benchmarkReads, s.benchmarkErrors, s.latencyBudgetUs, s.budgetExceeded);
        return;
    }
    const LatencyFigures& l = s.lastLatency;
    emit("DIAG latency runs=%" PRIu32 " reads=%" PRIu32 " errors=%" PRIu32 " budget_us=%" PRIu32
         " budget_exceeded=%" PRIu32 " count=%" PRIu32 " dropped=%" PRIu32 " min=%" PRIu32 " mean=%" PRIu32
         " p50=%" PRIu32 " p95=%" PRIu32 " p99=%" PRIu32 " max=%" PRIu32,
         s.benchmarkRuns, s.benchmarkReads, s.benchmarkErrors, s.latencyBudgetUs, s.budgetExceeded, l.count, l.dropped,
         l.min, l.mean, l.p50, l.p95, l.p99, l.max);
}

void ReportWriter::writeEvents(const DiagnosticsRecorder& recorder)
{
    const DiagnosticsRecorder::Log& log = recorder.events();
    emit("DIAG events total=%" PRIu32 " kept=%u lost=%" PRIu32 " capacity=%u", log.total(),
         static_cast<unsigned>(log.size()), log.overwritten(), static_cast<unsigned>(DiagnosticsRecorder::kEventCapacity));
    for (std::size_t i = 0; i < log.size(); ++i) {
        const EventRecord& e = log.at(i);
        emit("DIAG event seq=%" PRIu32 " t_us=%" PRIu64 " type=%s value=0x%08" PRIx32, e.sequence, e.timeUs,
             event_name(e.event), e.value);
    }
}

void ReportWriter::writeReport(const SystemSnapshot& system, const DiagnosticsRecorder& recorder)
{
    crc_ = Crc32Accumulator{};
    framedLines_ = 0;
    framing_ = true;
    emit("DIAG BEGIN format=%d source=retrobridge-hw-diag", kFormatVersion);
    writeStatus(system, recorder);
    writeMemory(system);
    writeStorage(recorder);
    writeEvents(recorder);
    framing_ = false;
    emit("DIAG END lines=%" PRIu32 " crc32=%08" PRIx32, framedLines_, crc_.value());
}

}  // namespace rb::hwdiag
