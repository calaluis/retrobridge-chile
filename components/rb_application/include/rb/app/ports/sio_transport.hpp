#pragma once

#include <array>
#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

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

// Outbound port isolating UART, GPIO and the COMMAND ISR from the SIO session
// (EspSioTransport in H5; ScriptedSioTransport in host tests).
//
// Contract:
//   * waitCommandFrame() returns the next frame, Timeout if none arrives within
//     `timeoutUs`, or TransportError on a receive failure (framing, overrun).
//   * send() is all-or-nothing from the caller's view and returns only after the
//     last stop bit has left the line, since t5 is measured from that point;
//     TransportError on failure.
//   * releaseDataLine() puts DATA IN back in high impedance (open collector),
//     so a shared bus is never held after a transaction or an error.
class SioTransport {
public:
    SioTransport(const SioTransport&) = delete;
    SioTransport& operator=(const SioTransport&) = delete;

    virtual rb::core::Result<CommandBytes> waitCommandFrame(uint32_t timeoutUs) = 0;
    virtual rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) = 0;
    virtual void releaseDataLine() = 0;

protected:
    SioTransport() = default;
    ~SioTransport() = default;  // never deleted through the interface
};

}  // namespace rb::app
