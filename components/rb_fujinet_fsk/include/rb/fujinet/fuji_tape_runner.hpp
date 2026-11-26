#pragma once

// FujiTapeRunner — RetroBridge adapter, written for RetroBridge (cassette-motor-fujinet). It runs
// FujiTapeLoop from the cassette task the way the beta's SIO bus runs sioCassette::sio_handle_cassette():
// one record per turn while the cassette is enabled. When the cassette is enabled is decided by FujiNet's
// SIO bus code, which was not read (ADR-012 amendment 2 allows cassette.cpp only), so these rules are
// RetroBridge's and are recorded as assumptions in docs/diseno/cassette-motor-fujinet.md §3:
//   - MOTOR on enables the cassette (600 baud, FujiTapeLoop::enable()); MOTOR off between records disables
//     it, and the tape stays where it is.
//   - A step that leaves the tape at offset 0 (its end, or a replay of the first record) disables it, as
//     the beta does; it is enabled again only after MOTOR goes off and on.
//   - A new mount, an eject or a rewind (the commands' generation) starts the tape over.
//
// Shared DATA IN (menu-sio: UART2 is the disk's; review of PR #122, H1). The runner keeps RetroBridge's
// handover protocol, the one CassetteTick keeps for CassettePlayer (menu-cinta plan §2.4):
//   - the tape moves only while the cassette holds DATA IN (a GatedMotor: not held = MOTOR off);
//   - Take: once the line is free, the runner holds it and confirms Tape;
//   - Release: answered between two steps; a step under way stops first (stopWanted() in the gap, every
//     slice and flush; the owner's engine through stopNow(), its motor_dropped callback), and its record
//     plays again from its start when the line comes back. Then the line goes back to UART2: Disk, or
//     Released when it could not; Busy asks again on the next turn;
//   - the end of the tape is published (EndSink) with the same rule: never while the line may still carry
//     the tape's bytes after a release.
// Bounded (H3): a line that takes no byte, no rate or does not drain for kStallMs (plus the time its bytes
// need at the rate) stalls the runner: the step stops, nothing more plays until a new tape generation or a
// new take, and `cas status` says so. A command other than `cas status` (StepWatch) stops the step too, so
// the cassette task answers it within the request's time.

#include <cstdint>

#include "rb/cassette/cassette_commands.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/cassette_tick.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/fujinet/fuji_tape_loop.hpp"

namespace rb::fujinet {

class Pause {
public:
    virtual void waitOneMs() = 0;  // a gap millisecond, and the beat of every wait for the line

protected:
    ~Pause() = default;
};

// Something other than the tape needs the cassette task now (on the ESP32: a `cas` command other than
// `status`, which is answered on the spot). Checked inside a step, at every wait.
class StepWatch {
public:
    virtual bool stopWanted() noexcept = 0;

protected:
    ~StepWatch() = default;
};

class FujiTapeRunner final : private FujiTapeBus, private FskRunPlayer, public rb::cassette::TapePosition {
public:
    static constexpr uint32_t kStallMs = 2000;  // no progress on the line for this long: stalled
    static constexpr uint32_t kSliceBytes = 16;  // bytes handed to the line at a time (shared: < 32 in flight)

    FujiTapeRunner(rb::cassette::ByteOut& out, const rb::cassette::MotorIn& motor, FskRunPlayer& player,
                   Pause& pause) noexcept;

    // Shared mode: DATA IN is lent and taken back through `lender`; `line` is the live line. Without it the
    // runner holds the line from the start (exclusive mode).
    void lend(rb::cassette::DataInLender& lender, rb::cassette::LiveLine& line) noexcept;
    void report(rb::cassette::EndSink& sink) noexcept { sink_ = &sink; }
    void watch(StepWatch& watch) noexcept { watch_ = &watch; }

    // The tape the commands have mounted (nullptr: none) and their generation. True when either changed:
    // the runner is back at the start of the tape, disabled; the caller then points the FSK engine at it.
    bool follow(rb::cassette::TapeSource* tape, uint32_t generation) noexcept;

    // One turn of the cassette task: at most one step, then the end published, then the lender answered.
    // True: a step ran (`last()` says which).
    bool tick() noexcept;

    // The owner's motor_dropped callback: MOTOR off, or the step must stop (release asked, command waiting).
    bool stopNow() noexcept { return !motor_.on() || interrupted(); }

    const FujiStep& last() const noexcept { return last_; }
    bool enabled() const noexcept { return enabled_; }
    bool holdsLine() const noexcept { return live_; }
    bool stalled() const noexcept { return stalled_; }
    uint32_t offset() const noexcept { return loop_.offset(); }

    // TapePosition (`cas status`)
    bool atEnd() const noexcept override { return atEnd_; }
    uint32_t segment() const noexcept override { return records_; }
    const char* phase() const noexcept override;

private:
    // The tape the loop reads: whatever follow() was given last.
    class Followed final : public rb::cassette::TapeSource {
    public:
        rb::cassette::TapeSource* tape = nullptr;
        uint32_t size() const override { return tape == nullptr ? 0 : tape->size(); }
        bool read(uint32_t offset, rb::core::Span<uint8_t> dst) override
        {
            return tape != nullptr && tape->read(offset, dst);
        }
    };

    // FujiTapeBus
    bool setBaud(uint32_t baud) override;
    bool write(const uint8_t* bytes, uint32_t count) override;
    bool flushOutput() override;
    void waitOneMs() override { pause_.waitOneMs(); }
    bool motorDropped() const override { return !motor_.on(); }
    bool stopWanted() override { return interrupted(); }
    // FskRunPlayer: the owner's engine, with `cas status` saying "fsk" while it plays
    FskRunOutcome play(uint32_t headerOffset, uint16_t chunkLength) override;

    bool interrupted() noexcept;
    uint32_t drainMs() const noexcept;  // what the bytes handed to the line since its last flush need
    bool releaseAsked() const noexcept;
    bool waited(uint32_t& ms, uint32_t limitMs) noexcept;  // one more ms of waiting; false: stop
    void stepOnce() noexcept;
    void answer() noexcept;

    rb::cassette::ByteOut& out_;
    const rb::cassette::MotorIn& motor_;
    FskRunPlayer& player_;
    Pause& pause_;
    Followed followed_;
    FujiTapeLoop loop_;
    rb::cassette::DataInLender* lender_ = nullptr;
    rb::cassette::LiveLine* line_ = nullptr;
    rb::cassette::EndSink* sink_ = nullptr;
    StepWatch* watch_ = nullptr;
    uint32_t generation_ = 0;
    uint32_t answered_ = 0;        // the last lender request confirmed
    uint32_t unflushed_ = 0;       // bytes handed to the line since its last flush
    uint32_t baud_ = FujiTapeLoop::kCassetteBaud;
    uint32_t records_ = 0;         // records played since the start of the tape
    bool live_ = true;             // the cassette holds DATA IN (exclusive mode: always)
    bool returning_ = false;       // released while live: the line may still carry the tape's bytes
    bool enabled_ = false;
    bool waitMotorOff_ = false;
    bool stalled_ = false;
    bool atEnd_ = false;
    bool stepping_ = false;
    bool interruptedStep_ = false;  // this step was stopped by a release or a command, not by MOTOR
    bool restoreRate_ = false;      // the line was the disk's: the tape's rate goes back before the next step
    const char* phase_ = "gap";
    FujiStep last_;
};

}  // namespace rb::fujinet
