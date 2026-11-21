#pragma once

// Outputs that drive nothing (cassette-cas C106): until H4 is approved no SIO pin may be an
// output, so DATA IN gets neither the records nor the FSK signal. The tape still moves with
// MOTOR, so `cas status` can follow a load attempt: records at their real pace, FSK runs as
// fast as the loader reads them (nothing plays them in real time). Counters only.

#include <cstdint>

#include "rb/cassette/cassette_player.hpp"

namespace rb::cassette {

class DiscardByteOut final : public ByteOut {
public:
    bool setBaud(uint32_t baud) override
    {
        baud_ = baud;
        return true;
    }
    bool send(rb::core::Span<const uint8_t> bytes) override
    {
        bytes_ += static_cast<uint32_t>(bytes.size());
        return true;
    }
    uint32_t baud() const noexcept { return baud_; }
    uint32_t bytes() const noexcept { return bytes_; }

private:
    uint32_t baud_ = 0;
    uint32_t bytes_ = 0;
};

class DiscardWaveOut final : public WaveOut {
public:
    bool push(const FskPortion&) override
    {
        ++portions_;
        return true;
    }
    bool idle() const override { return true; }
    void stop() override {}
    uint32_t portions() const noexcept { return portions_; }

private:
    uint32_t portions_ = 0;
};

}  // namespace rb::cassette
