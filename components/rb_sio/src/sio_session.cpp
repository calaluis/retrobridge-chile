#include "rb/sio/sio_session.hpp"

#include "rb/sio/command_frame.hpp"
#include "rb/sio/sio_codes.hpp"

namespace rb::sio {

using rb::app::CommandBytes;
using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;

StepOutcome SioSession::step()
{
    // Between transactions only: applying a request is O(1) and never blocks.
    if (applyRequest_.execute()) {
        return StepOutcome::MailboxApplied;
    }

    const Result<CommandBytes> received = transport_.waitCommandFrame(kPollTimeoutUs);
    if (!received.ok()) {
        if (received.error() == ErrorCode::Timeout) {
            return StepOutcome::Idle;  // nothing was driven on the bus
        }
        return endTransaction(StepOutcome::TransportFailed);
    }

    const CommandBytes& command = received.value();
    if (command.count != CommandFrame::kSize) {
        return endTransaction(StepOutcome::Ignored);  // incomplete or overlong frame
    }

    switch (dispatcher_.accept(Span<const uint8_t>{command.bytes})) {
    case Acceptance::Nak:
        return sendNak();
    case Acceptance::Accepted:
        return runAccepted();
    case Acceptance::Ignore:
    default:
        return endTransaction(StepOutcome::Ignored);
    }
}

StepOutcome SioSession::endTransaction(StepOutcome outcome)
{
    transport_.releaseDataLine();
    return outcome;
}

StepOutcome SioSession::sendNak()
{
    const uint8_t nak = code::kNak;
    const Result<void> sent = transport_.send(Span<const uint8_t>{&nak, 1});
    return endTransaction(sent.ok() ? StepOutcome::Nacked : StepOutcome::TransportFailed);
}

StepOutcome SioSession::runAccepted()
{
    const uint8_t ack = code::kAck;
    if (!transport_.send(Span<const uint8_t>{&ack, 1}).ok()) {
        dispatcher_.abandon();  // without an ACK the computer retries; never execute
        return endTransaction(StepOutcome::TransportFailed);
    }
    const uint64_t ackEnd = clock_.nowMicros();  // send() returns after the last bit

    // The data frame is written right after the completion byte: one buffer, one send.
    const Span<uint8_t> response{response_};
    const Completion completion = dispatcher_.execute(response.subspan(1, SioCommandDispatcher::kMaxFrameSize));
    response_[0] = completion.code;

    clock_.waitUntil(ackEnd + kCompleteDelayUs);
    const Result<void> sent = transport_.send(response.first(1u + completion.frameLength));
    return endTransaction(sent.ok() ? StepOutcome::Completed : StepOutcome::TransportFailed);
}

}  // namespace rb::sio
