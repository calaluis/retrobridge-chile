#pragma once

// CassetteCommands (spec cassette-cas, C105, AC6, AC8): what `cas ...` does on the console.
//
//   cas mount <path>   indexes the tape; the reply gives its name and its data and fsk blocks.
//                      A file that cannot be opened or indexed leaves the mounted tape as it was.
//                      A good one replaces it and starts at the beginning.
//   cas status         the mounted tape, where it goes and MOTOR.
//   cas eject          no tape.
//   cas rewind         back to the beginning.
//
// Call run() on the same task as CassettePlayer::tick(). The loader's producer runs on another
// task and may still be reading the tape it was loading: a tape that leaves (mount, eject) is
// closed only once the producer has stopped.
//
// Pure: the files come through TapeFiles (the SD card on the ESP32). No allocation (ADR-011):
// the two segment tables are the caller's, reserved once.

#include <cstddef>
#include <cstdint>

#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/core/span.hpp"

namespace rb::cassette {

// Opens tapes by path. Up to three may be open at once: the mounted one, the one being
// mounted, and one that left while the producer was still reading it.
class TapeFiles {
public:
    virtual TapeSource* open(const char* path) = 0;  // nullptr: cannot open
    virtual void close(TapeSource* source) = 0;

protected:
    ~TapeFiles() = default;
};

// Where the tape is when an engine other than CassettePlayer plays it (cassette-motor-fujinet, the FujiNet
// engine's FujiTapeRunner). `cas status` reads it instead of the player.
class TapePosition {
public:
    virtual bool atEnd() const noexcept = 0;
    virtual uint32_t segment() const noexcept = 0;  // from 0: the record that plays, or the next one
    virtual const char* phase() const noexcept = 0;

protected:
    ~TapePosition() = default;
};

class CassetteCommands {
public:
    static constexpr std::size_t kNameMax = 64;  // with the NUL; longer names are cut

    // `tableA` and `tableB` have the same size: the mounted tape's segments and a spare one.
    CassetteCommands(CassettePlayer& player, FskLoader& loader, TapeFiles& files, rb::core::Span<CasSegment> tableA,
                     rb::core::Span<CasSegment> tableB) noexcept;

    // `args`: what follows "cas". Writes one reply line into `out`, NUL-terminated (cut if long).
    void run(const char* args, const MotorIn& motor, WaveOut& wave, rb::core::Span<char> out) noexcept;

    bool mounted() const noexcept { return source_ != nullptr; }
    const char* name() const noexcept { return name_; }
    // The mounted tape (nullptr: none), and a count that moves on each mount, eject and rewind. The
    // experimental FujiNet engine (cassette-motor-fujinet) follows the tape through them.
    TapeSource* source() const noexcept { return source_; }
    uint32_t generation() const noexcept { return generation_; }
    // `cas status` follows `position` (nullptr: the player again). Read on the task that calls run().
    void showPosition(const TapePosition* position) noexcept { position_ = position; }

private:
    void mount(const char* path, WaveOut& wave, rb::core::Span<char> out) noexcept;
    void status(const MotorIn& motor, rb::core::Span<char> out) const noexcept;
    void eject(WaveOut& wave, rb::core::Span<char> out) noexcept;
    void rewind(WaveOut& wave, rb::core::Span<char> out) noexcept;
    bool closeRetired() noexcept;  // false: the producer has not stopped yet

    CassettePlayer& player_;
    FskLoader& loader_;
    TapeFiles& files_;
    rb::core::Span<CasSegment> active_;
    rb::core::Span<CasSegment> spare_;
    TapeSource* source_ = nullptr;
    TapeSource* retired_ = nullptr;  // left, but the producer may still be reading it
    const TapePosition* position_ = nullptr;
    uint32_t count_ = 0;
    uint32_t generation_ = 0;
    char name_[kNameMax] = {};
};

}  // namespace rb::cassette
