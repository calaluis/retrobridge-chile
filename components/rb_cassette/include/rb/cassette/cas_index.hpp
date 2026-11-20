#pragma once

// CasIndex (spec cassette-cas, C101): walks the chunk headers of an A8CAS .cas and lists
// what the cassette plays, in tape order: byte records (`data`) and FSK signal runs
// (`fsk `). It reads headers only, never a whole tape, and writes into a table the caller
// owns: no allocation (ADR-011).
//
// Format (docs/fuentes.md, cassette-cas): 8-byte chunk headers (4-byte type, LE16 length,
// LE16 aux), then `length` bytes. The file starts with `FUJI`. `baud` (aux) sets the rate
// of the records that follow, 600 when none came before. `data` is one record; aux is the
// gap before it, in ms. `fsk ` holds LE16 durations in 1/10 ms of alternating levels;
// aux is the gap before it, in ms. `pwm*` chunks are a pulse-width format.
//
// The FSK run rule is adapted from the owner's own A8CAS reader (FujiNet PR #1756, commit
// 6ea2070, casFSK: fsk_scan_run), under ADR-012 amendment 1: one `fsk ` chunk plus every
// `fsk ` chunk right after it with a gap of 0 is one run, at most 64 chunks. A longer run
// ends there and the next one starts with no gap, since its chunks still carry 0.

#include <cstdint>

#include "rb/cassette/tape_source.hpp"
#include "rb/core/span.hpp"

namespace rb::cassette {

enum class CasStatus : uint8_t {
    Ok,
    Invalid,      // no FUJI first, a cut header or chunk, an odd fsk length, baud 0, nothing to play
    Unsupported,  // a pwm* chunk
    Full,         // more segments than the table holds
    ReadError,    // the source failed
};

enum class SegmentKind : uint8_t {
    Data,  // a byte record at `baud`; length 0 = only its gap
    Fsk,   // an FSK run: `length` payload bytes over `chunks` chunks
};

struct CasSegment {
    SegmentKind kind = SegmentKind::Data;
    uint8_t chunks = 0;     // Fsk: chunks joined (1..64); Data: 1
    uint16_t gapMs = 0;     // gap before the segment, in ms
    uint32_t offset = 0;    // Data: first record byte; Fsk: header of the first chunk
    uint32_t length = 0;    // Data: record bytes; Fsk: payload bytes of the whole run
    uint32_t baud = 600;    // Data: rate of the record
};

struct CasIndexResult {
    CasStatus status = CasStatus::Invalid;
    uint32_t count = 0;  // segments written; 0 unless Ok
};

class CasIndex {
public:
    static constexpr uint32_t kHeaderSize = 8;
    static constexpr uint32_t kDefaultBaud = 600;
    static constexpr uint8_t kMaxRunChunks = 64;

    // Built when a tape is mounted, outside the play path. On anything but Ok, `out` holds
    // nothing the caller may use and count is 0.
    static CasIndexResult build(TapeSource& source, rb::core::Span<CasSegment> out);
};

}  // namespace rb::cassette
