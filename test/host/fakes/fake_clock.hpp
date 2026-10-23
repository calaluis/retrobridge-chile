#pragma once

// Simulated monotonic clock for host tests. Time only moves when a test double
// advances it (transport byte times, remote latency) or when waitUntil() is called.

#include <cstdint>

#include "rb/app/ports/clock.hpp"

namespace rb::test {

class FakeClock final : public rb::app::Clock {
public:
    uint64_t nowMicros() const override { return now_; }

    void waitUntil(uint64_t micros) override
    {
        if (micros > now_) {
            now_ = micros;
        }
    }

    void advance(uint64_t micros) { now_ += micros; }

private:
    uint64_t now_ = 0;
};

// Time on the wire for `bytes` bytes at 19200 baud, 8N1 (10 bits per byte),
// rounded up to the microsecond: 1 byte = 521 us.
constexpr uint64_t sio_byte_time_us(uint64_t bytes)
{
    return (bytes * 10u * 1000000u + 19199u) / 19200u;
}

}  // namespace rb::test
