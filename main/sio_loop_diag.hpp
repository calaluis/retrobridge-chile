#pragma once

// Diagnostics of the SIO loop (fix-cinta-data-in-65xe, PR #124): the menu shows "EL HOST NO
// RESPONDE" when its SIOV fails, and the console said nothing. This says which steps failed while
// the 65XE was on, and which phase of the loop kept the bus unattended for too long. Pure logic,
// tested on the host; it decides only what is logged, never what the loop does.

#include <array>
#include <cstdint>

namespace rb::menusio {

// A step outcome as the diagnostics see it (rb::sio::StepOutcome mapped in main/menu_sio.cpp).
enum class StepKind : uint8_t {
    Idle,
    Completed,
    Ignored,          // a frame not for us or unreadable
    Nacked,           // NAK sent
    TransportFailed,  // a receive or send failed
    Other,            // anything else (a mailbox request applied)
};

// The phases of one turn of the SIO loop, timed separately.
enum class LoopPhase : uint8_t {
    DriveMemory,  // serviceDriveMemory: may write the microSD
    Step,         // the bus transaction and the menu controller's service()
    Pump,         // the tape deck's pump()
    DataIn,       // the DATA IN arbiter step
};
constexpr std::size_t kLoopPhases = 4;

class SioLoopDiag {
public:
    static constexpr int64_t kSlowUs = 50000;  // the Atari retries a command for a few hundred ms
    static constexpr uint32_t kMaxLines = 64;  // reports per power-on; the counts go on
    // A step carries a whole menu reply (964 bytes at 19200 baud, about 502 ms): only above this it is
    // slow (issue #125: the 337-553 ms steps on the 65XE were replies, not faults).
    static constexpr int64_t kSlowStepUs = 700000;

    // A new power-on of the 65XE: the counts and the line budget start again.
    void poweredOn() noexcept
    {
        ignored_ = 0;
        nacked_ = 0;
        failed_ = 0;
        lines_ = 0;
        slowLines_ = 0;
        slowest_.fill(0);
    }

    // True when this step must be logged: a failure while the 65XE is on, within the budget.
    // With the 65XE off every step fails (COMMAND low), which is not a fault.
    bool noteStep(StepKind kind, bool atariOn) noexcept
    {
        if (!atariOn) {
            return false;
        }
        switch (kind) {
        case StepKind::Ignored:
            ++ignored_;
            break;
        case StepKind::Nacked:
            ++nacked_;
            break;
        case StepKind::TransportFailed:
            ++failed_;
            break;
        default:
            return false;
        }
        return take();
    }

    // True when this phase must be logged: it took longer than kSlowUs, within the budget. The
    // slowest run of each phase is kept in any case (review PR #124, M2): no line is not a healthy bus.
    bool notePhase(LoopPhase phase, int64_t elapsedUs) noexcept
    {
        int64_t& slowest = slowest_[static_cast<std::size_t>(phase)];
        if (elapsedUs > slowest) {
            slowest = elapsedUs;
        }
        const int64_t limit = phase == LoopPhase::Step ? kSlowStepUs : kSlowUs;
        if (elapsedUs > limit && take()) {
            ++slowLines_;
            return true;
        }
        return false;
    }

    // The slowest run of a phase since the last power-on, for the summary at power-off.
    int64_t slowest(LoopPhase phase) const noexcept { return slowest_[static_cast<std::size_t>(phase)]; }
    uint32_t slowLines() const noexcept { return slowLines_; }
    uint32_t lines() const noexcept { return lines_; }

    uint32_t count(StepKind kind) const noexcept
    {
        switch (kind) {
        case StepKind::Ignored:
            return ignored_;
        case StepKind::Nacked:
            return nacked_;
        case StepKind::TransportFailed:
            return failed_;
        default:
            return 0;
        }
    }

    static const char* name(LoopPhase phase) noexcept
    {
        switch (phase) {
        case LoopPhase::DriveMemory:
            return "drive-memory";
        case LoopPhase::Step:
            return "step";
        case LoopPhase::Pump:
            return "pump";
        case LoopPhase::DataIn:
            return "data-in";
        }
        return "?";
    }

    static const char* name(StepKind kind) noexcept
    {
        switch (kind) {
        case StepKind::Ignored:
            return "ignored";
        case StepKind::Nacked:
            return "nak";
        case StepKind::TransportFailed:
            return "transport";
        default:
            return "ok";
        }
    }

private:
    bool take() noexcept
    {
        if (lines_ >= kMaxLines) {
            return false;
        }
        ++lines_;
        return true;
    }

    uint32_t ignored_ = 0;
    uint32_t nacked_ = 0;
    uint32_t failed_ = 0;
    uint32_t lines_ = 0;
    uint32_t slowLines_ = 0;
    std::array<int64_t, kLoopPhases> slowest_{};
};

}  // namespace rb::menusio
