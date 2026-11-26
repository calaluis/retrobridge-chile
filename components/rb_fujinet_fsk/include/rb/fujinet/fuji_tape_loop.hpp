#pragma once

// FujiTapeLoop — RetroBridge adapter, written for RetroBridge. It does the job that FujiNet's
// sioCassette::send_FUJI_tape_block() does in the beta v1.6.2-atari-fsk-beta.1 (commit cf3382f): one call
// plays one record of an A8CAS tape. That file is GPL-3.0 code of other authors; it was read to describe
// its behaviour (ADR-012 amendment 2) and nothing of it is copied here. The FSK runs themselves are played
// by the owner's code (cassette_fsk_play_run, behind FskRunPlayer). Behaviour, and where it differs:
// docs/diseno/cassette-motor-fujinet.md.
//
// One step():
//   1. From the offset, skip chunks until a "data" or an "fsk " chunk; a "baud" chunk sets the rate.
//      No such chunk before the end: the tape ends (offset 0).
//   2. Wait the chunk's gap, 1 ms at a time. MOTOR drop with more than 1000 ms of gap left: back to the
//      offset where this step started (the next step replays it).
//   3. "fsk ": the owner's player plays the run. Not ok: back to the start offset.
//      "data": its bytes go out in pieces of at most 256, each one sent before the next.

#include <cstdint>

#include "rb/cassette/tape_source.hpp"

namespace rb::fujinet {

class FujiTapeBus {
public:
    virtual void setBaud(uint32_t baud) = 0;
    virtual void write(const uint8_t* bytes, uint32_t count) = 0;
    virtual void flushOutput() = 0;          // returns once the bytes written so far are on the line
    virtual void waitOneMs() = 0;
    virtual bool motorDropped() const = 0;   // pulldown on and MOTOR low

protected:
    ~FujiTapeBus() = default;
};

struct FskRunOutcome {
    bool ok = false;
    uint32_t nextOffset = 0;   // ok only
    uint32_t waveformMs = 0;
};

class FskRunPlayer {
public:
    virtual FskRunOutcome play(uint32_t headerOffset, uint16_t chunkLength) = 0;

protected:
    ~FskRunPlayer() = default;
};

enum class FujiStepKind : uint8_t {
    Data,    // a data record went out
    Fsk,     // an FSK run played
    Replay,  // MOTOR dropped in the gap, or the run did not play: offset back to the start of the step
    End,     // no record before the end of the file, or the file failed: offset 0
};

struct FujiStep {
    FujiStepKind kind = FujiStepKind::End;
    uint32_t offset = 0;     // where the next step starts; 0 = the tape is at its start (the beta disables it)
    uint32_t recordMs = 0;   // Data: gap + bytes at the rate (casTape.h); Fsk: the run's waveform
};

class FujiTapeLoop {
public:
    static constexpr uint32_t kCassetteBaud = 600;      // the rate when the cassette is enabled
    static constexpr uint32_t kPieceBytes = 256;        // a data record goes out in pieces of this size
    static constexpr uint32_t kMotorGraceMs = 1000;     // a drop with this much gap left or less is ignored

    FujiTapeLoop(rb::cassette::TapeSource& source, FujiTapeBus& bus, FskRunPlayer& player) noexcept
        : source_(source), bus_(bus), player_(player)
    {
    }

    // The cassette is enabled (MOTOR on): the rate goes back to 600 baud. The offset stays.
    void enable() noexcept;
    // A tape is mounted: back to its start.
    void rewind() noexcept { offset_ = 0; }

    FujiStep step() noexcept;

    uint32_t offset() const noexcept { return offset_; }
    uint32_t baud() const noexcept { return baud_; }

private:
    FujiStep ended() noexcept;

    rb::cassette::TapeSource& source_;
    FujiTapeBus& bus_;
    FskRunPlayer& player_;
    uint32_t offset_ = 0;
    uint32_t baud_ = kCassetteBaud;
};

}  // namespace rb::fujinet
