#pragma once

#include "rb/hwdiag/system_probe.hpp"

namespace rb::esp {

// SystemProbe on the ESP32 (H4, autonomous diagnostics): uptime (esp_timer),
// reset reason (esp_reset_reason), reset history kept in RTC no-init memory,
// heap figures (capture_memory), watchdog settings from sdkconfig and the
// firmware identity (esp_app_get_description).
//
// Reads internal state only. It configures no GPIO, starts no ADC and says
// nothing about voltages on the board's pins.
class EspSystemProbe final : public rb::hwdiag::SystemProbe {
public:
    explicit EspSystemProbe(const char* boardName) noexcept : boardName_(boardName) {}

    // Updates the reset history for this boot; call once, early in app_main.
    rb::hwdiag::BootRecord registerBoot() noexcept;

    rb::hwdiag::SystemSnapshot capture() const override;

private:
    const char* boardName_;
    rb::hwdiag::BootRecord boot_{};
};

}  // namespace rb::esp
