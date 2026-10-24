#include "rb/hwdiag/diag_types.hpp"

namespace rb::hwdiag {

const char* reset_reason_name(ResetReason reason) noexcept
{
    switch (reason) {
    case ResetReason::PowerOn: return "PowerOn";
    case ResetReason::External: return "External";
    case ResetReason::Software: return "Software";
    case ResetReason::Panic: return "Panic";
    case ResetReason::InterruptWatchdog: return "InterruptWatchdog";
    case ResetReason::TaskWatchdog: return "TaskWatchdog";
    case ResetReason::OtherWatchdog: return "OtherWatchdog";
    case ResetReason::DeepSleep: return "DeepSleep";
    case ResetReason::Brownout: return "Brownout";
    case ResetReason::Other: return "Other";
    case ResetReason::Unknown:
    default: return "Unknown";
    }
}

bool is_watchdog(ResetReason reason) noexcept
{
    return reason == ResetReason::InterruptWatchdog || reason == ResetReason::TaskWatchdog ||
           reason == ResetReason::OtherWatchdog;
}

const char* event_name(DiagEvent event) noexcept
{
    switch (event) {
    case DiagEvent::Boot: return "Boot";
    case DiagEvent::StorageMounted: return "StorageMounted";
    case DiagEvent::StorageMountFailed: return "StorageMountFailed";
    case DiagEvent::StorageUnmounted: return "StorageUnmounted";
    case DiagEvent::VerifyCompleted: return "VerifyCompleted";
    case DiagEvent::SectorReadError: return "SectorReadError";
    case DiagEvent::BenchmarkCompleted: return "BenchmarkCompleted";
    case DiagEvent::LatencyBudgetExceeded: return "LatencyBudgetExceeded";
    default: return "Unknown";
    }
}

}  // namespace rb::hwdiag
