#pragma once

// CassettePlayer (spec cassette-cas, C104, AC5): plays an indexed tape like a real
// cassette, through three ports the platform provides (MotorIn, ByteOut, WaveOut).
//
// - The tape moves only while MOTOR is on. Each segment waits its gap first.
// - Data: the rate is sent when it changes, then the record's bytes paced at 10 bits per
//   byte by the clock (start bit, 8 data bits, stop bit), at most 512 per send, read from
//   the tape as they go. MOTOR off freezes the record where it is; it goes on from there.
//   A refused send does not move the tape and the refused time is not owed.
//   (Logic of C002, by the same author.)
// - Fsk: the run's loader starts as the gap begins; after the gap the signal waits for the
//   loader's start gate, then its portions are pushed to WaveOut, which plays them at its
//   own pace. MOTOR off in the gap or before the gate opens freezes, as for a record, and
//   the loader keeps loading. If MOTOR drops once the signal has started, the signal stops
//   at once, DATA IN goes back to idle mark (WaveOut::stop) and the loader stops; when
//   MOTOR comes back, the same run is played again from its gap. Adapted from the owner's own code (FujiNet PR #1804, commit
//   7d84b91, cassette_fsk_stop_tx; PR #1756, cassette.cpp: "replay the same run from the
//   same point next call"), under ADR-012 amendment 1.
//
// tick() runs on the cassette loop and never allocates (ADR-011). The loader's producer
// (FskLoader::step) runs elsewhere: another task on the ESP32, the test in the host.

#include <cstdint>

#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/cassette/fsk_wave.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/core/span.hpp"

namespace rb::cassette {

// The Atari's MOTOR line.
class MotorIn {
public:
    virtual bool on() const = 0;

protected:
    ~MotorIn() = default;
};

// Bytes on DATA IN at a rate (UART on the ESP32).
class ByteOut {
public:
    virtual bool setBaud(uint32_t baud) = 0;                     // false: try again later
    virtual bool send(rb::core::Span<const uint8_t> bytes) = 0;  // false: busy, nothing taken

protected:
    ~ByteOut() = default;
};

// The FSK signal on DATA IN (RMT on the ESP32).
class WaveOut {
public:
    virtual bool push(const FskPortion& portion) = 0;  // false: queue full, nothing taken
    virtual bool idle() const = 0;                      // everything pushed has played
    virtual void stop() = 0;                            // drop the queue, DATA IN to idle mark

protected:
    ~WaveOut() = default;
};

enum class PlayerPhase : uint8_t { Gap, Bytes, FskWait, FskPlay, FskDrain, End };

class CassettePlayer {
public:
    static constexpr uint32_t kMaxSend = 512;
    static constexpr uint64_t kBitsPerByte = 10;
    static constexpr uint64_t kUsPerSecond = 1000000;

    explicit CassettePlayer(FskLoader& loader) noexcept : loader_(loader) {}

    // `source` and `segments` (from CasIndex) stay owned by the caller while loaded.
    void load(TapeSource& source, rb::core::Span<const CasSegment> segments, WaveOut& wave) noexcept;
    void unload(WaveOut& wave) noexcept;
    void rewind(WaveOut& wave) noexcept;

    void tick(uint64_t nowUs, const MotorIn& motor, ByteOut& bytes, WaveOut& wave) noexcept;

    // The owner passes other outputs to tick() from now on (menu-cinta T9a.3). Called between two
    // ticks, so no send() or push() is ever in progress: every byte goes whole to one output.
    // `previousDrove`: the old outputs reached DATA IN (false for discard).
    // - The rate is sent again to the new ByteOut before its first byte.
    // - From outputs that drove DATA IN: a record goes on from the byte where it is (nothing lost or
    //   sent twice); an FSK signal already started on `previous` is cut there (stop(), DATA IN to idle
    //   mark) and the run is played again from its gap, as for a MOTOR drop; a run that had already
    //   played to its end is not played again.
    // - From outputs that did not (review of PR #102): the Atari got nothing of the current segment, so
    //   a record or run already begun is played again, whole, from its gap, even if discard "played"
    //   it to its end.
    void changeOutputs(WaveOut& previous, bool previousDrove) noexcept;

    bool loaded() const noexcept { return source_ != nullptr; }
    bool atEnd() const noexcept { return segment_ >= segments_.size(); }
    uint32_t segment() const noexcept { return segment_; }
    PlayerPhase phase() const noexcept { return atEnd() ? PlayerPhase::End : phase_; }
    uint32_t fskRestarts() const noexcept { return fskRestarts_; }
    // A run that does not fit the loader's bank: the tape cannot go past it.
    bool blocked() const noexcept { return blocked_; }

private:
    void enterSegment() noexcept;
    void nextSegment() noexcept;
    enum class FskProgress : uint8_t { More, Done, Failed };

    void cutFsk(WaveOut& wave) noexcept;
    void restartRun(WaveOut& wave) noexcept;
    bool startLoader(const CasSegment& segment) noexcept;
    bool playBytes(const CasSegment& segment, ByteOut& bytes) noexcept;  // true: segment done
    FskProgress playFsk(WaveOut& wave) noexcept;

    FskLoader& loader_;
    TapeSource* source_ = nullptr;
    rb::core::Span<const CasSegment> segments_;
    uint32_t segment_ = 0;
    PlayerPhase phase_ = PlayerPhase::Gap;
    uint64_t phaseUs_ = 0;   // time already spent in the gap
    uint64_t budgetUs_ = 0;  // tape time not yet used
    uint64_t scaled_ = 0;    // in Bytes: unused time times the rate (us * baud)
    uint32_t sentInRecord_ = 0;
    uint32_t sentBaud_ = 0;  // 0: no rate sent since load or rewind
    uint64_t lastNowUs_ = 0;
    bool haveNow_ = false;
    bool motorWasOn_ = false;
    bool loaderStarted_ = false;
    FskWave wave_;
    uint32_t valueAt_ = 0;     // next run value not handed to wave_ yet
    bool pieceOpen_ = false;   // wave_ holds a piece not fully played
    bool havePortion_ = false; // portion_ was refused by WaveOut: push it first
    FskPortion portion_{};
    uint32_t fskRestarts_ = 0;
    bool blocked_ = false;
    uint8_t buffer_[kMaxSend] = {};
};

}  // namespace rb::cassette
