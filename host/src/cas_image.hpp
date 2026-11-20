#pragma once

// CasImage of retrobridge_host (spec cassette-cas, C001): checks an A8CAS .cas tape
// and indexes the blocks the virtual cassette plays (level 1: byte records).
//
// Format (docs/fuentes.md, cassette-cas): 8-byte chunk headers (4-byte type,
// 2-byte length and 2-byte aux, little-endian), then `length` data bytes. The
// file starts with `FUJI`. `baud` (aux) sets the rate of the data that follows;
// 600 when none came before. `data` is one record and its aux is the gap before
// it in milliseconds. `fsk ` holds 16-bit durations in 1/10 ms after an aux gap.
// `pwm*` chunks are pulse-width signals.
//
// The header and fsk duration handling is adapted from the owner's own A8CAS
// reader (FujiNet PR #1756, commit 6ea2070, casFSK), under ADR-012 amendment 1.
// Pure function: no files, no network, no SIO.

#include <cstdint>
#include <vector>

#include "rb/core/span.hpp"

namespace rb::host {

enum class CasResult : uint8_t {
    Ok,
    Invalid,  // no FUJI first, a cut header or chunk, an odd fsk length, baud 0, nothing to play (AC9)
    Pulses,   // type B: no data chunk but fsk pulses, or any pwm chunk (AC8)
};

struct CasBlock {
    bool isData = false;     // true: a record; false: silence
    uint32_t offset = 0;     // first data byte in the .cas (records only)
    uint32_t length = 0;     // record bytes (0 for silence)
    uint32_t gapMs = 0;      // gap before the block, in milliseconds
    uint32_t baud = 600;     // rate of the record
    uint64_t silenceUs = 0;  // silence after the gap, in microseconds (silence only)
};

struct CasTape {
    std::vector<CasBlock> blocks;  // in tape order; offsets point into the indexed bytes
};

class CasImage {
public:
    static constexpr uint32_t kDefaultBaud = 600;
    static constexpr uint32_t kHeaderSize = 8;

    // Built when a tape is chosen, outside the SIO path. On anything but Ok, `tape` is empty.
    static CasResult index(rb::core::Span<const uint8_t> cas, CasTape& tape);
};

}  // namespace rb::host
