// FujiTapeLoop — RetroBridge adapter (see the header). Written for RetroBridge.
#include "rb/fujinet/fuji_tape_loop.hpp"

#include "rb/core/span.hpp"
#include "../lib/media/atari/casFSK.h"
#include "../lib/media/atari/casTape.h"

namespace rb::fujinet {

namespace {

constexpr uint32_t kHeader = 8;

bool isType(const uint8_t* header, const char (&name)[5]) noexcept
{
    return header[0] == static_cast<uint8_t>(name[0]) && header[1] == static_cast<uint8_t>(name[1]) &&
           header[2] == static_cast<uint8_t>(name[2]) && header[3] == static_cast<uint8_t>(name[3]);
}

uint16_t le16(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

bool FujiTapeLoop::enable() noexcept
{
    return bus_.setBaud(kCassetteBaud);
}

FujiStep FujiTapeLoop::ended() noexcept
{
    offset_ = 0;
    FujiStep step;
    step.kind = FujiStepKind::End;
    step.offset = 0;
    return step;
}

FujiStep FujiTapeLoop::replay(uint32_t start) noexcept
{
    offset_ = start;
    FujiStep step;
    step.kind = FujiStepKind::Replay;
    step.offset = start;
    return step;
}

FujiStep FujiTapeLoop::step() noexcept
{
    const uint32_t start = offset_;
    const uint32_t size = source_.size();
    uint32_t at = offset_;
    uint8_t header[kHeader] = {};
    bool fsk = false;
    bool found = false;

    while (at < size && size - at >= kHeader) {
        if (!source_.read(at, rb::core::Span<uint8_t>(header, kHeader))) {
            return ended();
        }
        const uint16_t length = le16(header + 4);
        if (isType(header, "data")) {
            found = true;
            break;
        }
        if (fsk_is_chunk_type(header)) {  // owner's casFSK.h
            found = true;
            fsk = true;
            break;
        }
        if (isType(header, "baud")) {
            baud_ = le16(header + 6);
            if (!bus_.setBaud(baud_)) {
                return replay(start);
            }
        }
        at += kHeader + length;
    }
    if (!found) {
        return ended();
    }

    const uint16_t length = le16(header + 4);
    const uint16_t gapMs = le16(header + 6);
    for (uint32_t left = gapMs; left > 0;) {
        --left;
        bus_.waitOneMs();
        if (bus_.stopWanted() || (left > kMotorGraceMs && bus_.motorDropped())) {
            return replay(start);
        }
    }

    FujiStep step;
    if (fsk) {
        const FskRunOutcome played = player_.play(at, length);
        if (!played.ok) {
            return replay(start);
        }
        offset_ = played.nextOffset;
        step.kind = FujiStepKind::Fsk;
        step.offset = offset_;
        step.recordMs = played.waveformMs;
        return step;
    }

    uint32_t from = at + kHeader;
    if (length > size - from) {
        return ended();  // a cut record: nothing of it goes out
    }
    uint8_t piece[kPieceBytes];
    for (uint32_t left = length; left > 0;) {
        const uint32_t n = left > kPieceBytes ? kPieceBytes : left;
        if (!source_.read(from, rb::core::Span<uint8_t>(piece, n))) {
            return ended();
        }
        if (!bus_.write(piece, n) || !bus_.flushOutput()) {
            return replay(start);  // the record plays again from its start: nothing of it is skipped
        }
        from += n;
        left -= n;
    }
    offset_ = from;
    step.kind = FujiStepKind::Data;
    step.offset = offset_;
    step.recordMs = cas_data_record_ms(gapMs, length, static_cast<unsigned short>(baud_));  // owner's casTape.h
    return step;
}

}  // namespace rb::fujinet
