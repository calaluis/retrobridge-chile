#include "rb/sio/sio_line_transport.hpp"

#include "rb/sio/sio_codes.hpp"

namespace rb::sio {

using rb::app::CommandBytes;
using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

rb::core::Result<CommandBytes> SioLineTransport::waitCommandFrame(uint32_t timeoutUs)
{
    // Whatever is waiting now arrived outside a COMMAND window: never part of a
    // frame. Discarded before the wait, not after the fall, so a fall seen late
    // (one poll tick) cannot cost the first bytes of the frame.
    port_.flushInput();
    if (!port_.waitCommandLevel(true, timeoutUs)) {
        return Failure{ErrorCode::Timeout};
    }
    CommandBytes frame{};
    frame.commandAssertedAtUs = clock_.nowMicros();

    const Result<std::size_t> received = port_.read(Span<uint8_t>{frame.bytes.data(), frame.bytes.size()}, kFrameReadUs);
    if (!received.ok()) {
        ++stats_.receiveErrors;
        recover();
        return Failure{ErrorCode::TransportError};
    }
    frame.count = static_cast<uint8_t>(received.value());
    if (!port_.waitCommandLevel(false, kReleaseWaitUs)) {
        ++stats_.stuckLows;
        recover();
        return Failure{ErrorCode::TransportError};
    }
    frame.commandReleasedAtUs = clock_.nowMicros();
    if (frame.count == frame.bytes.size()) {
        ++stats_.frames;
        replyPending_ = true;
        releasedAtUs_ = frame.commandReleasedAtUs;
    } else {
        ++stats_.shortFrames;
        replyPending_ = false;
    }
    return frame;
}

rb::core::Result<void> SioLineTransport::send(Span<const uint8_t> bytes)
{
    if (replyPending_) {
        replyPending_ = false;
        const uint64_t now = clock_.nowMicros();
        const uint64_t latency = now > releasedAtUs_ ? now - releasedAtUs_ : 0;
        stats_.lastAckLatencyUs = latency > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(latency);
        if (stats_.lastAckLatencyUs > stats_.maxAckLatencyUs) {
            stats_.maxAckLatencyUs = stats_.lastAckLatencyUs;
        }
    }
    if (!port_.write(bytes).ok()) {
        ++stats_.sendErrors;
        recover();
        return Failure{ErrorCode::TransportError};
    }
    return {};
}

rb::core::Result<void> SioLineTransport::receiveAfterAck(Span<uint8_t> frame)
{
    const uint8_t ack = code::kAck;
    port_.flushInput();  // only what follows the ACK is the data frame
    if (!send(Span<const uint8_t>{&ack, 1}).ok()) {
        return Failure{ErrorCode::TransportError};
    }
    const Result<std::size_t> received = port_.read(frame, byteTimeUs(frame.size()) + kDataMarginUs);
    if (!received.ok() || received.value() != frame.size()) {
        ++stats_.receiveErrors;
        recover();
        return Failure{ErrorCode::TransportError};
    }
    return {};
}

void SioLineTransport::releaseDataLine()
{
    port_.idleLine();
    ++stats_.releases;
}

void SioLineTransport::recover() noexcept
{
    replyPending_ = false;
    port_.flushInput();
    port_.idleLine();
    ++stats_.releases;
}

}  // namespace rb::sio
