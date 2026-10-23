#pragma once

#include <cstdint>

namespace rb::app {

// Outbound port: monotonic time in microseconds (esp_timer on the ESP32, H5;
// FakeClock in host tests). Lets the SIO session express the protocol's
// absolute deadlines, e.g. "end of ACK + t5".
class Clock {
public:
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;

    virtual uint64_t nowMicros() const = 0;

    // Returns once nowMicros() >= micros; returns immediately if already past.
    virtual void waitUntil(uint64_t micros) = 0;

protected:
    Clock() = default;
    ~Clock() = default;  // never deleted through the interface
};

}  // namespace rb::app
