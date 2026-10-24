#pragma once

#include <cstdint>

#include "rb/hwdiag/diag_types.hpp"
#include "rb/hwdiag/event_log.hpp"

namespace rb::hwdiag {

// Records what the diagnostic firmware observes: storage counters, the last
// benchmark latencies and a bounded event log. Fixed memory, no allocation,
// every call O(1). Fed by the composition root and the console (one task).
class DiagnosticsRecorder {
public:
    static constexpr std::size_t kEventCapacity = 32;
    using Log = EventLog<kEventCapacity>;

    explicit DiagnosticsRecorder(uint32_t latencyBudgetUs) noexcept;

    void recordBoot(const BootRecord& boot, uint64_t nowUs) noexcept;
    void recordMount(bool ok, int32_t espError, uint8_t errorCode, uint64_t nowUs) noexcept;
    void recordUnmount(uint64_t nowUs) noexcept;
    void recordVerify(uint32_t sectors, uint32_t readErrors, uint8_t lastErrorCode, uint64_t nowUs) noexcept;
    void recordSectorReadError(uint16_t sector, uint8_t errorCode, uint64_t nowUs) noexcept;
    void recordBenchmark(const LatencyFigures& latency, uint32_t reads, uint32_t readErrors, uint64_t nowUs) noexcept;

    const StorageHealth& storage() const noexcept { return storage_; }
    const Log& events() const noexcept { return events_; }

    // Event value layout of SectorReadError: sector << 8 | error code.
    static constexpr uint32_t sector_error_value(uint16_t sector, uint8_t errorCode) noexcept
    {
        return (static_cast<uint32_t>(sector) << 8) | errorCode;
    }

private:
    StorageHealth storage_{};
    Log events_{};
};

}  // namespace rb::hwdiag
