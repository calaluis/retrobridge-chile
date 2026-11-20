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
//   * receiveAfterAck(frame) (menu-escritura-disco): for a command that carries
//     data from the computer, sends the ACK announcing frame.size() bytes (data
//     plus checksum) and fills `frame` with them. It returns with the data
//     frame still to be acknowledged: the next send() of one byte (ACK or NAK)
//     answers it. A transport that cannot receive data keeps the default,
//     TransportError, and sends nothing.
//   * releaseDataLine() puts DATA IN back in high impedance (open collector),
//     so a shared bus is never held after a transaction or an error.
class SioTransport : public SioCommandSource {
public:
    SioTransport(const SioTransport&) = delete;
    SioTransport& operator=(const SioTransport&) = delete;

    virtual rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) = 0;
    virtual void releaseDataLine() = 0;
    virtual rb::core::Result<void> receiveAfterAck(rb::core::Span<uint8_t> frame)
    {
        (void)frame;
        return rb::core::Failure{rb::core::ErrorCode::TransportError};
    }

protected:
    SioTransport() = default;
    ~SioTransport() = default;  // never deleted through the interface
};

}  // namespace rb::app
