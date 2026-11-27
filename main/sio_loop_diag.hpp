#pragma once

// Diagnostics of the SIO loop (fix-cinta-data-in-65xe, PR #124): the menu shows "EL HOST NO
// RESPONDE" when its SIOV fails, and the console said nothing. This says which steps failed while
// the 65XE was on, and which phase of the loop kept the bus unattended for too long. Pure logic,
// tested on the host; it decides only what is logged, never what the loop does.

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

class SioLoopDiag {
public:
    static constexpr int64_t kSlowUs = 50000;  // the Atari retries a command for a few hundred ms
    static constexpr uint32_t kMaxLines = 64;  // reports per power-on; the counts go on

    // A new power-on of the 65XE: the counts and the line budget start again.
    void poweredOn() noexcept
    {
        ignored_ = 0;
        nacked_ = 0;
        failed_ = 0;
        lines_ = 0;
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

    // True when this phase must be logged: it took longer than kSlowUs, within the budget.
    bool notePhase(LoopPhase, int64_t elapsedUs) noexcept { return elapsedUs > kSlowUs && take(); }

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
};

}  // namespace rb::menusio
