#pragma once

// FskWave (spec cassette-cas, C102): turns the values of one FSK run into the signal the
// cassette plays: a level and a duration in 1 us ticks, never longer than one hardware
// slot (32767 ticks). The run's values arrive in pieces (one per chunk, or one per loaded
// block); the level keeps alternating across pieces.
//
// Rules adapted from the owner's own code (FujiNet PR #1756, commits 6ea2070 and 5180b1d:
// casFSK fsk_level_for_index / fsk_ticks_for_value / fsk_next_portion, and cassetteFSK
// cassette_fsk_pull_portion), under ADR-012 amendment 1:
//   - one value = 1/10 ms = 100 ticks;
//   - value index even -> level 0 (space), odd -> level 1 (mark), counted over the run;
//   - a value of 0 gives no portion but still uses its index;
//   - a value longer than 32767 ticks goes out as several portions of the same level.
// Pure: no RMT, no allocation, no I/O.

#include <cstdint>

#include "rb/core/span.hpp"

namespace rb::cassette {

struct FskPortion {
    bool level = false;   // false = space (0), true = mark (1)
    uint32_t ticks = 0;   // 1 tick = 1 us; 1..kMaxPortionTicks
};

class FskWave {
public:
    static constexpr uint32_t kTicksPerUnit = 100;
    static constexpr uint32_t kMaxPortionTicks = 32767;

    // Starts a new run: the next value is index 0.
    void reset() noexcept;

    // The next piece of the same run: little-endian 16-bit values, whole (even length).
    // The bytes must stay valid until next() returns false.
    void setValues(rb::core::Span<const uint8_t> le16Values) noexcept;

    // The next portion of the signal. False when the current piece is used up. Values never
    // span pieces, but the rest of a value split by size stays pending and plays first
    // when the next piece arrives.
    bool next(FskPortion& out) noexcept;

    // Values consumed since reset().
    uint64_t valuesDone() const noexcept { return index_; }

    // Exact length of a piece's signal, in ticks.
    static uint64_t sumTicks(rb::core::Span<const uint8_t> le16Values) noexcept;

private:
    rb::core::Span<const uint8_t> piece_;
    std::size_t at_ = 0;          // next byte of piece_
    uint64_t index_ = 0;          // values consumed in this run
    uint32_t remaining_ = 0;      // ticks left of the value being played
    bool level_ = false;
};

}  // namespace rb::cassette
