#pragma once

#include <cstdint>

namespace rb::hwdiag {

// Why the chip last started (portable mirror of esp_reset_reason_t).
enum class ResetReason : uint8_t {
    Unknown,
    PowerOn,
    External,
    Software,
    Panic,
    InterruptWatchdog,
    TaskWatchdog,
    OtherWatchdog,
    DeepSleep,
    Brownout,
    Other,  // SDIO, USB, JTAG, eFuse, power glitch, CPU lock-up...
};

const char* reset_reason_name(ResetReason reason) noexcept;
bool is_watchdog(ResetReason reason) noexcept;

// Reset history since the last power-on. Kept by the platform in memory that
// survives soft resets (RTC no-init on the ESP32); best effort: a power-on or a
// corrupted record starts it again at boot 1.
struct BootRecord {
    ResetReason reason;
    uint32_t bootsSincePowerOn;
    uint32_t watchdogResets;
    uint32_t panicResets;
    uint32_t brownoutResets;
};

struct MemoryFigures {
    uint32_t free8bit;
    uint32_t minimumFree8bit;
    uint32_t largestFree8bit;
    uint32_t freeInternal;
};

// Watchdog configuration compiled into the firmware (sdkconfig).
struct WatchdogConfig {
    bool taskWatchdogEnabled;
    uint32_t taskWatchdogTimeoutMs;
    bool taskWatchdogPanics;
    bool interruptWatchdogEnabled;
    uint32_t interruptWatchdogTimeoutMs;
};

// Static strings (firmware image, board profile): pointers with program lifetime.
struct FirmwareIdentity {
    const char* project;
    const char* version;
    const char* idfVersion;
    const char* board;
};

struct SystemSnapshot {
    uint64_t uptimeUs;
    FirmwareIdentity firmware;
    BootRecord boot;
    MemoryFigures memory;
    WatchdogConfig watchdog;
};

// Latency summary of the last storage benchmark (microseconds).
struct LatencyFigures {
    uint32_t count;
    uint32_t dropped;
    uint32_t min;
    uint32_t mean;
    uint32_t p50;
    uint32_t p95;
    uint32_t p99;
    uint32_t max;
};

// What the storage context did since boot. Error codes are rb::core::ErrorCode
// values, kept as integers so this component depends on nothing but rb_core.
struct StorageHealth {
    bool mounted;
    uint32_t mountAttempts;
    uint32_t mountFailures;
    int32_t lastMountEspError;
    uint32_t verifyRuns;
    uint32_t sectorsVerified;
    uint32_t verifyReadErrors;
    uint32_t sectorReadErrors;  // single-sector reads (drive read)
    uint8_t lastErrorCode;      // 0: none yet
    uint32_t benchmarkRuns;
    uint32_t benchmarkReads;
    uint32_t benchmarkErrors;
    uint32_t budgetExceeded;    // benchmarks whose max latency passed the budget
    uint32_t latencyBudgetUs;
    bool hasLatency;
    LatencyFigures lastLatency;
};

enum class DiagEvent : uint8_t {
    Boot,
    StorageMounted,
    StorageMountFailed,
    StorageUnmounted,
    VerifyCompleted,
    SectorReadError,
    BenchmarkCompleted,
    LatencyBudgetExceeded,
};

const char* event_name(DiagEvent event) noexcept;

}  // namespace rb::hwdiag
