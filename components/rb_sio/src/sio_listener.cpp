#include "rb/sio/sio_listener.hpp"

#include "rb/core/span.hpp"
#include "rb/sio/command_frame.hpp"

namespace rb::sio {

using rb::app::CommandBytes;
using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;

ListenOutcome SioListener::step(uint32_t timeoutUs) noexcept
{
    const Result<CommandBytes> received = source_.waitCommandFrame(timeoutUs);
    if (!received.ok()) {
        if (received.error() == ErrorCode::Timeout) {
            stats_.timeouts = saturating_increment(stats_.timeouts);
            return ListenOutcome::Timeout;
        }
        stats_.transportErrors = saturating_increment(stats_.transportErrors);
        record(ListenOutcome::TransportError, 0, std::array<uint8_t, 5>{}, clock_.nowMicros());
        return ListenOutcome::TransportError;
    }

    const CommandBytes& frame = received.value();
    ListenOutcome outcome = ListenOutcome::Frame;
    if (frame.count < CommandFrame::kSize) {
        outcome = ListenOutcome::Incomplete;
        stats_.incomplete = saturating_increment(stats_.incomplete);
    } else if (frame.count > CommandFrame::kSize) {
        outcome = ListenOutcome::Overlong;
        stats_.overlong = saturating_increment(stats_.overlong);
    } else if (!CommandFrame::parse(Span<const uint8_t>{frame.bytes}).ok()) {
        outcome = ListenOutcome::ChecksumMismatch;  // the only failure left with exactly 5 bytes
        stats_.checksumMismatches = saturating_increment(stats_.checksumMismatches);
    } else {
        stats_.frames = saturating_increment(stats_.frames);
    }
    record(outcome, frame.count, frame.bytes, frame.commandReleasedAtUs);
    return outcome;
}

void SioListener::record(ListenOutcome outcome, uint8_t count, const std::array<uint8_t, 5>& bytes,
                         uint64_t atUs) noexcept
{
    records_[next_] = ListenRecord{++sequence_, outcome, count, bytes, atUs};
    next_ = (next_ + 1) % kCapacity;
    if (size_ < kCapacity) {
        ++size_;
    } else {
        stats_.overwritten = saturating_increment(stats_.overwritten);
    }
}

}  // namespace rb::sio
