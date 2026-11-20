#include "cas_image.hpp"

#include <cstddef>

namespace rb::host {

namespace {

constexpr uint64_t kTenthMsUs = 100;  // one fsk duration unit, 1/10 ms

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

CasResult fail(CasTape& tape, CasResult result)
{
    tape.blocks.clear();
    return result;
}

}  // namespace

CasResult CasImage::index(rb::core::Span<const uint8_t> cas, CasTape& tape)
{
    tape.blocks.clear();
    const uint8_t* bytes = cas.data();
    const std::size_t size = cas.size();
    if (size < kHeaderSize || !isType(bytes, "FUJI")) {
        return CasResult::Invalid;
    }

    uint32_t baud = kDefaultBaud;
    bool anyData = false;
    bool anyFsk = false;  // an fsk chunk with durations
    bool anyPwm = false;
    std::size_t at = 0;
    while (at < size) {
        if (size - at < kHeaderSize) {
            return fail(tape, CasResult::Invalid);  // a header cut short
        }
        const uint8_t* header = bytes + at;
        const uint16_t length = le16(header + 4);
        const uint16_t aux = le16(header + 6);
        const std::size_t body = at + kHeaderSize;
        if (length > size - body) {
            return fail(tape, CasResult::Invalid);  // the chunk runs past the end of the file
        }

        if (isType(header, "baud")) {
            if (aux == 0) {
                return fail(tape, CasResult::Invalid);  // a rate of 0 cannot be played
            }
            baud = aux;
        } else if (isType(header, "data") && length == 0) {
            CasBlock block;  // an empty record: only its gap remains, as silence
            block.gapMs = aux;
            tape.blocks.push_back(block);
        } else if (isType(header, "data")) {
            CasBlock block;
            block.isData = true;
            block.offset = static_cast<uint32_t>(body);
            block.length = length;
            block.gapMs = aux;
            block.baud = baud;
            tape.blocks.push_back(block);
            anyData = true;
        } else if (isType(header, "fsk ")) {
            if ((length & 1u) != 0) {
                return fail(tape, CasResult::Invalid);  // durations are whole 16-bit values
            }
            CasBlock block;  // level 1 plays it as silence of the same length
            block.gapMs = aux;
            for (std::size_t i = 0; i < length; i += 2) {
                block.silenceUs += static_cast<uint64_t>(le16(bytes + body + i)) * kTenthMsUs;  // no wrap
            }
            tape.blocks.push_back(block);
            anyFsk = anyFsk || length > 0;
        } else if (isPwm(header)) {
            anyPwm = true;
        }
        // Anything else (later FUJI labels, unknown types) is skipped.
        at = body + length;
    }

    if (anyPwm || (!anyData && anyFsk)) {
        return fail(tape, CasResult::Pulses);
    }
    if (!anyData) {
        return fail(tape, CasResult::Invalid);  // nothing to play
    }
    return CasResult::Ok;
}

}  // namespace rb::host
