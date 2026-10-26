#include "rb/sio/command_line_tracker.hpp"

namespace rb::sio {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;

Result<CommandLineTracker> CommandLineTracker::create(const CommandLineLimits& limits) noexcept
{
    if (limits.minLowUs == 0 || limits.minLowUs > limits.maxFrameLowUs || limits.maxFrameLowUs >= limits.stuckLowUs) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return CommandLineTracker{limits};
}

bool CommandLineTracker::goesBack(uint64_t atUs) noexcept
{
    if (atUs < lastUs_) {
        return true;
    }
    lastUs_ = atUs;
    return false;
}

CommandLineEvent CommandLineTracker::onFall(uint64_t atUs) noexcept
{
    if (goesBack(atUs)) {
        return {CommandLineEventKind::Rejected, 0, 0};
    }
    const bool lostRise = state_ != State::High;
    state_ = State::Low;  // a fall always starts a new low
    fallUs_ = atUs;
    return {lostRise ? CommandLineEventKind::Resync : CommandLineEventKind::None, atUs, 0};
}

CommandLineEvent CommandLineTracker::onRise(uint64_t atUs) noexcept
{
    if (goesBack(atUs)) {
        return {CommandLineEventKind::Rejected, 0, 0};
    }
    const State was = state_;
    state_ = State::High;
    if (was == State::High) {
        return {CommandLineEventKind::Resync, 0, atUs};  // lost fall: no window
    }
    if (was == State::Stuck) {
        return {CommandLineEventKind::Recovered, fallUs_, atUs};
    }
    const uint64_t lowUs = atUs - fallUs_;
    if (lowUs < limits_.minLowUs) {
        return {CommandLineEventKind::Glitch, fallUs_, atUs};
    }
    if (lowUs > limits_.maxFrameLowUs) {
        return {CommandLineEventKind::OverlongWindow, fallUs_, atUs};
    }
    return {CommandLineEventKind::FrameWindow, fallUs_, atUs};
}

CommandLineEvent CommandLineTracker::poll(uint64_t nowUs) noexcept
{
    if (goesBack(nowUs)) {
        return {CommandLineEventKind::Rejected, 0, 0};
    }
    if (state_ == State::Low && nowUs - fallUs_ >= limits_.stuckLowUs) {
        state_ = State::Stuck;
        return {CommandLineEventKind::StuckLow, fallUs_, 0};
    }
    return {CommandLineEventKind::None, 0, 0};
}

}  // namespace rb::sio
