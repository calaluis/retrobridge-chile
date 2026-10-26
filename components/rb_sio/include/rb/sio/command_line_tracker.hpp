#pragma once

#include <cstdint>

#include "rb/core/result.hpp"

namespace rb::sio {

// Timing limits for the COMMAND line (H5 design, docs/diseno/h5-escucha-solo-recepcion.md
// section 3). No defaults on purpose: the real values depend on measurements
// still pending (P6, M5), so every caller must state them.
struct CommandLineLimits {
    uint32_t minLowUs;       // a shorter low is a glitch (e.g. the 80 ns pulses of erratum GPIO-3.11)
    uint32_t maxFrameLowUs;  // a longer low is not a command frame window
    uint32_t stuckLowUs;     // a low this long is reported once as StuckLow (risk R12)
};

enum class CommandLineEventKind : uint8_t {
    None,            // nothing to report
    Glitch,          // low shorter than minLowUs; ignored
    FrameWindow,     // low within [minLowUs, maxFrameLowUs]: a command frame window
    OverlongWindow,  // low longer than maxFrameLowUs, released before being reported as stuck
    StuckLow,        // still low after stuckLowUs; reported once, no windows until it rises
    Recovered,       // rose after a StuckLow
    Resync,          // edge out of sequence (a lost edge); no window reported
    Rejected,        // timestamp earlier than the previous one; state unchanged
};

struct CommandLineEvent {
    CommandLineEventKind kind;
    uint64_t fallUs;  // start of the low, when the event has one; otherwise 0
    uint64_t riseUs;  // end of the low, for events reported on a rise; otherwise 0
};

// Follows the COMMAND line from edge timestamps and classifies each low
// (H5, first host-only deliverable). Its single reason to change is the
// interpretation of COMMAND timing.
//
// Pure logic: no ESP-IDF, GPIO, interrupts or heap, and nothing uses it yet.
// The future EspSioTransport would feed it the times captured by the COMMAND
// ISR (onFall/onRise) and call poll() periodically from its task; whatever the
// ISR mechanism chosen for P2, only the timestamps reach this class.
class CommandLineTracker {
public:
    // InvalidArgument unless 0 < minLowUs <= maxFrameLowUs < stuckLowUs.
    static rb::core::Result<CommandLineTracker> create(const CommandLineLimits& limits) noexcept;

    CommandLineEvent onFall(uint64_t atUs) noexcept;
    CommandLineEvent onRise(uint64_t atUs) noexcept;
    CommandLineEvent poll(uint64_t nowUs) noexcept;  // reports StuckLow once

    bool isLow() const noexcept { return state_ != State::High; }
    bool isStuck() const noexcept { return state_ == State::Stuck; }

private:
    enum class State : uint8_t { High, Low, Stuck };

    explicit CommandLineTracker(const CommandLineLimits& limits) noexcept : limits_(limits) {}

    bool goesBack(uint64_t atUs) noexcept;  // records atUs unless it is earlier than the last time

    CommandLineLimits limits_;
    State state_ = State::High;
    uint64_t fallUs_ = 0;
    uint64_t lastUs_ = 0;
};

}  // namespace rb::sio
