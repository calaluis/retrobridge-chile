#pragma once

#include <cstdint>

#include "rb/app/ports/sio_command_source.hpp"
#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::app {

// Outbound port isolating UART, GPIO and the COMMAND ISR from the SIO session
// (EspSioTransport in H5; ScriptedSioTransport in host tests). Adds the
// transmit side to SioCommandSource.
//
// Contract:
//   * waitCommandFrame(): see SioCommandSource.
//   * send() is all-or-nothing from the caller's view and returns only after the
//     last stop bit has left the line, since t5 is measured from that point;
//     TransportError on failure.
//   * releaseDataLine() puts DATA IN back in high impedance (open collector),
//     so a shared bus is never held after a transaction or an error.
class SioTransport : public SioCommandSource {
public:
    SioTransport(const SioTransport&) = delete;
    SioTransport& operator=(const SioTransport&) = delete;

    virtual rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) = 0;
    virtual void releaseDataLine() = 0;

protected:
    SioTransport() = default;
    ~SioTransport() = default;  // never deleted through the interface
};

}  // namespace rb::app
