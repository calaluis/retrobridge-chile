#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#include "rb/app/ports/clock.hpp"

namespace rb::host {

// Clock over std::chrono::steady_clock: microseconds since construction. PC time,
// not SIO time: on the emulated bus the emulator decides the timing (ADR-016, t5).
class HostClock final : public rb::app::Clock {
public:
    HostClock() : start_(std::chrono::steady_clock::now()) {}

    uint64_t nowMicros() const override
    {
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
    }

    void waitUntil(uint64_t micros) override
    {
        const uint64_t now = nowMicros();
        if (micros > now) {
            std::this_thread::sleep_for(std::chrono::microseconds(micros - now));
        }
    }

private:
    std::chrono::steady_clock::time_point start_;
};

}  // namespace rb::host
