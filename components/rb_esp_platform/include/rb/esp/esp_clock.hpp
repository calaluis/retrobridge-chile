#pragma once

#include <cstdint>

#include "rb/app/ports/clock.hpp"

namespace rb::esp {

// Clock port on the ESP32 (H3-10): esp_timer_get_time(), microseconds since boot.
// waitUntil() sleeps through FreeRTOS for long waits and busy-waits the last
// stretch (esp_rom_delay_us), so short deadlines stay precise. Fine tuning for
// the SIO t5 wait is H5 work.
class EspClock final : public rb::app::Clock {
public:
    static constexpr uint64_t kBusyWaitThresholdUs = 2000;

    EspClock() = default;

    uint64_t nowMicros() const override;
    void waitUntil(uint64_t micros) override;
};

}  // namespace rb::esp
