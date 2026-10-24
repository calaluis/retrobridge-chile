#include "rb/hwdiag/diagnostics_recorder.hpp"

namespace rb::hwdiag {

DiagnosticsRecorder::DiagnosticsRecorder(uint32_t latencyBudgetUs) noexcept
{
    storage_.latencyBudgetUs = latencyBudgetUs;
}

void DiagnosticsRecorder::recordBoot(const BootRecord& boot, uint64_t nowUs) noexcept
{
    events_.record(nowUs, DiagEvent::Boot, boot.bootsSincePowerOn);
}

void DiagnosticsRecorder::recordMount(bool ok, int32_t espError, uint8_t errorCode, uint64_t nowUs) noexcept
{
    ++storage_.mountAttempts;
    if (ok) {
        storage_.mounted = true;
        events_.record(nowUs, DiagEvent::StorageMounted, 0);
        return;
    }
    ++storage_.mountFailures;
    storage_.lastMountEspError = espError;
    storage_.lastErrorCode = errorCode;
    events_.record(nowUs, DiagEvent::StorageMountFailed, static_cast<uint32_t>(espError));
}

void DiagnosticsRecorder::recordUnmount(uint64_t nowUs) noexcept
{
    storage_.mounted = false;
    events_.record(nowUs, DiagEvent::StorageUnmounted, 0);
}

void DiagnosticsRecorder::recordVerify(uint32_t sectors, uint32_t readErrors, uint8_t lastErrorCode,
                                       uint64_t nowUs) noexcept
{
    ++storage_.verifyRuns;
    storage_.sectorsVerified += sectors;
    storage_.verifyReadErrors += readErrors;
    if (readErrors != 0) {
        storage_.lastErrorCode = lastErrorCode;
    }
    events_.record(nowUs, DiagEvent::VerifyCompleted, readErrors);
}

void DiagnosticsRecorder::recordSectorReadError(uint16_t sector, uint8_t errorCode, uint64_t nowUs) noexcept
{
    ++storage_.sectorReadErrors;
    storage_.lastErrorCode = errorCode;
    events_.record(nowUs, DiagEvent::SectorReadError, sector_error_value(sector, errorCode));
}

void DiagnosticsRecorder::recordBenchmark(const LatencyFigures& latency, uint32_t reads, uint32_t readErrors,
                                          uint64_t nowUs) noexcept
{
    ++storage_.benchmarkRuns;
    storage_.benchmarkReads += reads;
    storage_.benchmarkErrors += readErrors;
    storage_.lastLatency = latency;
    storage_.hasLatency = true;
    if (latency.max > storage_.latencyBudgetUs) {
        ++storage_.budgetExceeded;
        events_.record(nowUs, DiagEvent::LatencyBudgetExceeded, latency.max);
    } else {
        events_.record(nowUs, DiagEvent::BenchmarkCompleted, latency.max);
    }
}

}  // namespace rb::hwdiag
