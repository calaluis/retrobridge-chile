#pragma once

#include <array>
#include <cstdint>

#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/sio_transport.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"

namespace rb::sio {

enum class StepOutcome : uint8_t {
    Idle,             // no frame within the poll timeout; nothing transmitted
    MailboxApplied,   // a mount/unmount request was applied between transactions
    Ignored,          // frame not for us or unreadable; nothing transmitted
    Nacked,           // NAK sent
    Completed,        // ACK, then COMPLETE or ERROR (+ data frame) sent
    TransportFailed,  // a receive or send failed; the transaction was dropped
};

// Runs the SIO bus in the drive context (ADR-005, ADR-014). Each step() handles
// either one pending mailbox request or one bus transaction, never both, so a
// mount is never applied in the middle of a transaction.
//
// Transaction: accept() (no I/O) -> ACK -> execute() -> wait until the end of
// the ACK + kCompleteDelayUs (t5 >= 250 us) -> one send of [code][data][checksum].
// Write (W/P, menu-escritura-disco): accept() -> receiveAfterAck() (ACK + data
// frame) -> ACK or NAK of the data (bad checksum: NAK, nothing written, ends
// Nacked) -> execute() -> wait from the end of that ACK + kCompleteDelayUs ->
// COMPLETE or ERROR alone.
// The data line is released at the end of every transaction, including failures.
//
// Recovery: transport errors are recoverable and never abort. If the ACK cannot
// be sent the accepted command is abandoned unexecuted; if the completion cannot
// be sent the transaction is dropped. Either way the dispatcher has no pending
// command afterwards and the next frame is handled normally; disk ownership is
// untouched (the computer retries per its own SIO logic).
class SioSession {
public:
    static constexpr uint32_t kPollTimeoutUs = 20000;  // H2-14: bounds mailbox latency
    static constexpr uint64_t kCompleteDelayUs = 300;  // t5 >= 250 us (Atari C016555 §9), with margin

    SioSession(rb::app::SioTransport& transport, rb::app::Clock& clock, SioCommandDispatcher& dispatcher,
               rb::app::ApplyDriveRequest& applyRequest) noexcept
        : transport_(transport), clock_(clock), dispatcher_(dispatcher), applyRequest_(applyRequest)
    {
    }

    SioSession(const SioSession&) = delete;
    SioSession& operator=(const SioSession&) = delete;

    StepOutcome step();

private:
    StepOutcome endTransaction(StepOutcome outcome);
    StepOutcome sendNak();
    StepOutcome runAccepted();
    StepOutcome runWithData(rb::core::Span<uint8_t> incoming);

    rb::app::SioTransport& transport_;
    rb::app::Clock& clock_;
    SioCommandDispatcher& dispatcher_;
    rb::app::ApplyDriveRequest& applyRequest_;

    // Completion byte followed by the data frame; owned here (ADR-011).
    std::array<uint8_t, 1 + SioCommandDispatcher::kMaxFrameSize> response_{};
};

}  // namespace rb::sio
