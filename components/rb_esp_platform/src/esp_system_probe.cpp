#include "rb/esp/esp_system_probe.hpp"

#include <cstdint>

#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "rb/esp/memory_snapshot.hpp"
#include "sdkconfig.h"

namespace rb::esp {

using rb::hwdiag::BootRecord;
using rb::hwdiag::ResetReason;

namespace {

// Reset history across soft resets. RTC no-init memory keeps its contents over
// software, panic and watchdog resets and is undefined after a power-on, so the
// record carries a magic word and a check value and restarts when either is wrong.
struct RtcBootRecord {
    uint32_t magic;
    uint32_t boots;
    uint32_t watchdogResets;
    uint32_t panicResets;
    uint32_t brownoutResets;
    uint32_t check;
};

constexpr uint32_t kRtcMagic = 0x52424447u;  // "RBDG"

RTC_NOINIT_ATTR RtcBootRecord g_rtc_boot;

uint32_t check_value(const RtcBootRecord& r)
{
    return r.magic ^ r.boots ^ (r.watchdogResets << 1) ^ (r.panicResets << 2) ^ (r.brownoutResets << 3) ^ 0xA5A5A5A5u;
}

ResetReason to_reason(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return ResetReason::PowerOn;
    case ESP_RST_EXT: return ResetReason::External;
    case ESP_RST_SW: return ResetReason::Software;
    case ESP_RST_PANIC: return ResetReason::Panic;
    case ESP_RST_INT_WDT: return ResetReason::InterruptWatchdog;
    case ESP_RST_TASK_WDT: return ResetReason::TaskWatchdog;
    case ESP_RST_WDT: return ResetReason::OtherWatchdog;
    case ESP_RST_DEEPSLEEP: return ResetReason::DeepSleep;
    case ESP_RST_BROWNOUT: return ResetReason::Brownout;
    case ESP_RST_UNKNOWN: return ResetReason::Unknown;
    default: return ResetReason::Other;
    }
}

rb::hwdiag::WatchdogConfig watchdog_config()
{
    rb::hwdiag::WatchdogConfig w{};
#if defined(CONFIG_ESP_TASK_WDT_EN)
    w.taskWatchdogEnabled = true;
#endif
#if defined(CONFIG_ESP_TASK_WDT_TIMEOUT_S)
    w.taskWatchdogTimeoutMs = static_cast<uint32_t>(CONFIG_ESP_TASK_WDT_TIMEOUT_S) * 1000u;
#endif
#if defined(CONFIG_ESP_TASK_WDT_PANIC)
    w.taskWatchdogPanics = true;
#endif
#if defined(CONFIG_ESP_INT_WDT)
    w.interruptWatchdogEnabled = true;
#endif
#if defined(CONFIG_ESP_INT_WDT_TIMEOUT_MS)
    w.interruptWatchdogTimeoutMs = static_cast<uint32_t>(CONFIG_ESP_INT_WDT_TIMEOUT_MS);
#endif
    return w;
}

}  // namespace

BootRecord EspSystemProbe::registerBoot() noexcept
{
    const ResetReason reason = to_reason(esp_reset_reason());
    RtcBootRecord& r = g_rtc_boot;
    if (reason == ResetReason::PowerOn || r.magic != kRtcMagic || r.check != check_value(r)) {
        r = RtcBootRecord{kRtcMagic, 0, 0, 0, 0, 0};
    }
    ++r.boots;
    if (rb::hwdiag::is_watchdog(reason)) {
        ++r.watchdogResets;
    } else if (reason == ResetReason::Panic) {
        ++r.panicResets;
    } else if (reason == ResetReason::Brownout) {
        ++r.brownoutResets;
    }
    r.check = check_value(r);
    boot_ = BootRecord{reason, r.boots, r.watchdogResets, r.panicResets, r.brownoutResets};
    return boot_;
}

rb::hwdiag::SystemSnapshot EspSystemProbe::capture() const
{
    rb::hwdiag::SystemSnapshot s{};
    s.uptimeUs = static_cast<uint64_t>(esp_timer_get_time());
    const esp_app_desc_t* app = esp_app_get_description();
    s.firmware = {app->project_name, app->version, app->idf_ver, boardName_};
    s.boot = boot_;
    const MemorySnapshot m = capture_memory();
    s.memory = {static_cast<uint32_t>(m.free8bit), static_cast<uint32_t>(m.minimumFree8bit),
                static_cast<uint32_t>(m.largestFree8bit), static_cast<uint32_t>(m.freeInternal)};
    s.watchdog = watchdog_config();
    return s;
}

}  // namespace rb::esp
