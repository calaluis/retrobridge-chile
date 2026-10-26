#pragma once

#include <array>
#include <cstdint>

#include "rb/core/result.hpp"

namespace rb::app {

// What was received while COMMAND was low. `count` is the number of bytes
// actually received (it may differ from 5 for an incomplete or overlong frame;
// only the first 5 are kept). `commandReleasedAtUs` is when COMMAND went high,
// the reference for t2.
struct CommandBytes {
    std::array<uint8_t, 5> bytes;
    uint8_t count;
    uint64_t commandReleasedAtUs;
};

// Receive-only side of the SIO bus: command frames, nothing else. A component
// that holds only this port cannot transmit or touch DATA IN by construction
// (the H5 listener). SioTransport extends it with the transmit operations.
//
// Contract: waitCommandFrame() returns the next frame, Timeout if none arrives
// within `timeoutUs`, or TransportError on a receive failure (framing, overrun).
class SioCommandSource {
public:
    SioCommandSource(const SioCommandSource&) = delete;
    SioCommandSource& operator=(const SioCommandSource&) = delete;

    virtual rb::core::Result<CommandBytes> waitCommandFrame(uint32_t timeoutUs) = 0;

protected:
    SioCommandSource() = default;
    ~SioCommandSource() = default;  // never deleted through the interface
};

}  // namespace rb::app
