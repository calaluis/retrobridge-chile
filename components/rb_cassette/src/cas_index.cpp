#include "rb/cassette/cas_index.hpp"

namespace rb::cassette {

namespace {

uint16_t le16(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

bool isType(const uint8_t* p, const char (&type)[5]) noexcept
{
    return p[0] == static_cast<uint8_t>(type[0]) && p[1] == static_cast<uint8_t>(type[1]) &&
           p[2] == static_cast<uint8_t>(type[2]) && p[3] == static_cast<uint8_t>(type[3]);
}

bool isPwm(const uint8_t* p) noexcept
{
    return p[0] == 'p' && p[1] == 'w' && p[2] == 'm';
}

CasIndexResult fail(CasStatus status) noexcept
{
    return CasIndexResult{status, 0};
}

}  // namespace

CasIndexResult CasIndex::build(TapeSource& source, rb::core::Span<CasSegment> out)
{
    const uint32_t size = source.size();
    uint8_t header[kHeaderSize];
    rb::core::Span<uint8_t> headerSpan(header);

    uint32_t count = 0;
    uint32_t baud = kDefaultBaud;
    bool playable = false;      // a record or an fsk run with bytes
    bool lastWasFsk = false;    // the previous chunk was `fsk ` (a zero-gap one may join it)
    uint32_t at = 0;
    while (at < size) {
        if (size - at < kHeaderSize) {
            return fail(CasStatus::Invalid);  // a header cut short
        }
        if (!source.read(at, headerSpan)) {
            return fail(CasStatus::ReadError);
        }
        if (at == 0 && !isType(header, "FUJI")) {
            return fail(CasStatus::Invalid);
        }
        const uint16_t length = le16(header + 4);
        const uint16_t aux = le16(header + 6);
        const uint32_t body = at + kHeaderSize;
        if (length > size - body) {
            return fail(CasStatus::Invalid);  // the chunk runs past the end of the file
        }

        const bool isFsk = isType(header, "fsk ");
        if (isType(header, "baud")) {
            if (aux == 0) {
                return fail(CasStatus::Invalid);  // a rate of 0 cannot be played
            }
            baud = aux;
        } else if (isType(header, "data")) {
            if (count == out.size()) {
                return fail(CasStatus::Full);
            }
            CasSegment& segment = out[count++];
            segment = CasSegment{};
            segment.kind = SegmentKind::Data;
            segment.chunks = 1;
            segment.gapMs = aux;
            segment.offset = body;
            segment.length = length;
            segment.baud = baud;
            playable = playable || length > 0;
        } else if (isFsk) {
            if ((length & 1u) != 0) {
                return fail(CasStatus::Invalid);  // durations are whole 16-bit values
            }
            CasSegment* run = count > 0 ? &out[count - 1] : nullptr;
            if (lastWasFsk && aux == 0 && run != nullptr && run->chunks < kMaxRunChunks) {
                ++run->chunks;  // a continuation of the same waveform
                run->length += length;
            } else {
                if (count == out.size()) {
                    return fail(CasStatus::Full);
                }
                CasSegment& segment = out[count++];
                segment = CasSegment{};
                segment.kind = SegmentKind::Fsk;
                segment.chunks = 1;
                segment.gapMs = aux;
                segment.offset = at;
                segment.length = length;
                segment.baud = baud;
            }
            playable = playable || length > 0;
        } else if (isPwm(header)) {
            return fail(CasStatus::Unsupported);
        }
        // Anything else (FUJI labels, unknown types) is skipped, and ends an fsk run.
        lastWasFsk = isFsk;
        at = body + length;
    }

    if (!playable) {
        return fail(CasStatus::Invalid);  // nothing to play (also an empty file)
    }
    return CasIndexResult{CasStatus::Ok, count};
}

}  // namespace rb::cassette
