#include "rb/sio/command_frame_assembler.hpp"

#include "rb/sio/saturating.hpp"

namespace rb::sio {

using rb::app::CommandBytes;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

constexpr uint8_t kMaxCount = 255;  // CommandBytes::count saturates here

void bump(uint32_t& counter) noexcept
{
    counter = saturating_increment(counter);
}

}  // namespace

Result<CommandFrameAssembler> CommandFrameAssembler::create(const CommandLineLimits& limits) noexcept
{
    const auto tracker = CommandLineTracker::create(limits);
    if (!tracker.ok()) {
        return Failure{tracker.error()};
    }
    return CommandFrameAssembler{tracker.value()};
}

bool CommandFrameAssembler::rejected(CommandLineEventKind kind) noexcept
{
    if (kind == CommandLineEventKind::Rejected) {
        bump(stats_.rejectedTimestamps);  // the event is ignored; the window state is kept
        return true;
    }
    return false;
}

void CommandFrameAssembler::startWindow(uint64_t fallUs) noexcept
{
    window_ = Window::Open;
    reason_ = DropReason::None;
    receiveError_ = false;
    bytes_ = {};
    count_ = 0;  // never reuse bytes from an earlier window
    fallUs_ = fallUs;
    riseUs_ = 0;
}

void CommandFrameAssembler::onFall(uint64_t atUs) noexcept
{
    const CommandLineEvent event = tracker_.onFall(atUs);
    if (rejected(event.kind)) {
        return;
    }
    if (window_ == Window::Open) {
        bump(stats_.resyncs);  // the rise of the previous window was lost: drop it
    } else if (window_ == Window::Closed) {
        bump(stats_.superseded);  // complete() never came for the previous window: drop it
    } else if (event.kind == CommandLineEventKind::Resync) {
        bump(stats_.resyncs);
    }
    startWindow(atUs);
}

void CommandFrameAssembler::onRise(uint64_t atUs) noexcept
{
    const CommandLineEvent event = tracker_.onRise(atUs);
    if (rejected(event.kind)) {
        return;
    }
    if (window_ != Window::Open) {
        bump(stats_.resyncs);  // a rise without a fall: no window to close
        return;
    }
    switch (event.kind) {
    case CommandLineEventKind::Glitch:
        bump(stats_.glitches);
        reason_ = DropReason::Glitch;
        break;
    case CommandLineEventKind::OverlongWindow:
        bump(stats_.overlongWindows);
        reason_ = DropReason::OverlongWindow;
        break;
    case CommandLineEventKind::Recovered:
        bump(stats_.recoveries);
        reason_ = DropReason::StuckLow;
        break;
    case CommandLineEventKind::FrameWindow:
    default:
        break;  // keep any reason set earlier (e.g. a queue overflow)
    }
    riseUs_ = atUs;
    window_ = Window::Closed;
}

void CommandFrameAssembler::onBytes(Span<const uint8_t> bytes) noexcept
{
    if (window_ == Window::Idle) {
        stats_.strayBytes = saturating_add(stats_.strayBytes, bytes.size());  // outside any window: never reused
        return;
    }
    for (const uint8_t byte : bytes) {
        if (count_ < bytes_.size()) {
            bytes_[count_] = byte;
        }
        if (count_ < kMaxCount) {
            ++count_;
        }
    }
}

void CommandFrameAssembler::onReceiveError() noexcept
{
    bump(stats_.receiveErrors);
    if (window_ != Window::Idle) {
        receiveError_ = true;
    }
}

void CommandFrameAssembler::onQueueOverflow() noexcept
{
    bump(stats_.queueOverflows);
    if (window_ != Window::Idle) {
        reason_ = DropReason::QueueOverflow;
    }
}

void CommandFrameAssembler::poll(uint64_t nowUs) noexcept
{
    const CommandLineEvent event = tracker_.poll(nowUs);
    if (rejected(event.kind)) {
        return;
    }
    if (event.kind == CommandLineEventKind::StuckLow) {
        bump(stats_.stuckLows);
        if (window_ == Window::Open) {
            reason_ = DropReason::StuckLow;
        }
    }
}

AssemblyResult CommandFrameAssembler::complete() noexcept
{
    if (window_ != Window::Closed) {
        return {AssemblyOutcome::NothingToDeliver, DropReason::None, CommandBytes{}};
    }
    window_ = Window::Idle;
    if (reason_ != DropReason::None) {
        return {AssemblyOutcome::Dropped, reason_, CommandBytes{}};
    }
    if (receiveError_) {
        return {AssemblyOutcome::ReceiveError, DropReason::None, CommandBytes{}};
    }
    bump(stats_.frames);
    return {AssemblyOutcome::Frame, DropReason::None, CommandBytes{bytes_, count_, riseUs_, fallUs_}};
}

}  // namespace rb::sio
