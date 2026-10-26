#pragma once

#include <array>
#include <cstdint>

#include "rb/app/ports/sio_command_source.hpp"
#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/sio/command_line_tracker.hpp"

namespace rb::sio {

enum class AssemblyOutcome : uint8_t {
    Frame,             // a valid window: `frame` holds its bytes and both COMMAND edges
    Dropped,           // the window was invalid; `reason` says why (D4: never delivered)
    ReceiveError,      // a UART error during the window; the adapter maps it to TransportError
    NothingToDeliver,  // no closed window (none started, or COMMAND is still low)
};

enum class DropReason : uint8_t {
    None,
    Glitch,          // COMMAND low shorter than minLowUs
    OverlongWindow,  // COMMAND low longer than maxFrameLowUs, even with 5 valid bytes (D4)
    StuckLow,        // the window was reported as stuck before it rose
    QueueOverflow,   // the adapter lost events during the window
};

struct AssemblyResult {
    AssemblyOutcome outcome;
    DropReason reason;            // only for Dropped
    rb::app::CommandBytes frame;  // only for Frame
};

// Line and capture anomalies, kept apart from TransportError (D2). Saturating
// counters: they stop at UINT32_MAX instead of wrapping.
struct AssemblerStats {
    uint32_t frames;
    uint32_t glitches;
    uint32_t overlongWindows;
    uint32_t stuckLows;           // StuckLow reports
    uint32_t recoveries;          // rises after a StuckLow
    uint32_t resyncs;             // edges out of sequence (lost edges)
    uint32_t rejectedTimestamps;  // times earlier than the previous one
    uint32_t superseded;          // a new fall before complete(): previous window dropped
    uint32_t queueOverflows;
    uint32_t receiveErrors;       // UART errors, inside or outside a window
    uint32_t strayBytes;          // bytes outside any window, discarded
};

// Builds command frames from COMMAND edges and received bytes (H5, third
// host-only deliverable; H5 design section 9, decisions D1-D4). Its single
// reason to change is the policy that ties bytes to a COMMAND window.
//
// Pure logic: no ESP-IDF, GPIO, UART or heap, and nothing uses it yet. It
// reuses CommandLineTracker for the line and does not check checksums
// (CommandFrame does that downstream).
//
// Expected order per window: onFall -> onBytes* -> onRise -> onBytes* -> complete().
// complete() closes the window; the adapter calls it once its capture
// mechanism (D1, still pending measurement) guarantees that every byte of the
// window has arrived, so late bytes after the rise are still included.
//
// Safety rules:
//   * bytes are never reused: anything outside [onFall, complete()] is
//     discarded and counted, and a new fall always starts from zero bytes;
//   * a frame from an invalid window (glitch, overlong, stuck, lost edge,
//     queue overflow) is never delivered, whatever its bytes (D4);
//   * a new fall before complete() drops the previous window.
//
// Preconditions the future adapter must meet (not provable in host tests):
// one monotonic clock for all times; edges and bytes delivered to one task in
// arrival order; complete() only after all bytes of the window arrived; RX
// FIFO flushed on COMMAND falling; lost events reported with onQueueOverflow().
// Not synchronised: one task only.
class CommandFrameAssembler {
public:
    // InvalidArgument for incoherent limits (see CommandLineTracker::create).
    static rb::core::Result<CommandFrameAssembler> create(const CommandLineLimits& limits) noexcept;

    void onFall(uint64_t atUs) noexcept;
    void onRise(uint64_t atUs) noexcept;
    void onBytes(rb::core::Span<const uint8_t> bytes) noexcept;
    void onReceiveError() noexcept;
    void onQueueOverflow() noexcept;
    void poll(uint64_t nowUs) noexcept;

    // Delivers the closed window, if any, and returns to idle.
    AssemblyResult complete() noexcept;

    const AssemblerStats& stats() const noexcept { return stats_; }
    void resetStats() noexcept { stats_ = AssemblerStats{}; }

private:
    enum class Window : uint8_t { Idle, Open, Closed };  // no window / COMMAND low / risen, awaiting complete()

    explicit CommandFrameAssembler(const CommandLineTracker& tracker) noexcept : tracker_(tracker) {}

    void startWindow(uint64_t fallUs) noexcept;
    bool rejected(CommandLineEventKind kind) noexcept;

    CommandLineTracker tracker_;
    Window window_ = Window::Idle;
    DropReason reason_ = DropReason::None;
    bool receiveError_ = false;
    std::array<uint8_t, 5> bytes_{};
    uint8_t count_ = 0;
    uint64_t fallUs_ = 0;
    uint64_t riseUs_ = 0;
    AssemblerStats stats_{};
};

}  // namespace rb::sio
