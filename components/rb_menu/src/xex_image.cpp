#include "rb/menu/xex_image.hpp"

#include <cstring>
#include <utility>

namespace rb::host {

namespace {

#include "rb/menu/xex_loader.inc"

static_assert(kXexLoaderSectors * XexImage::kSectorSize == sizeof kXexLoaderBlock, "the loader is whole sectors");
static_assert(kXexLoaderLengthOffset + 3 <= sizeof kXexLoaderBlock, "the length fits in the loader");
static_assert(kXexLoaderLow <= kXexLoaderHigh, "the loader zone is not empty");

uint32_t word(const uint8_t* at) noexcept { return at[0] | (static_cast<uint32_t>(at[1]) << 8); }

// One segment at `at`: an optional $FFFF, start and end, then end - start + 1 data bytes. False when the
// header is cut, start is after end, or the data is cut.
bool readSegment(const uint8_t* bytes, std::size_t size, std::size_t at, uint32_t& start, uint32_t& end,
                 std::size_t& next) noexcept
{
    if (size - at >= 2 && bytes[at] == 0xFF && bytes[at + 1] == 0xFF) {
        at += 2;
    }
    if (size - at < 4) {
        return false;
    }
    start = word(bytes + at);
    end = word(bytes + at + 2);
    at += 4;
    if (start > end || size - at < end - start + 1) {
        return false;
    }
    next = at + end - start + 1;
    return true;
}

// The fill of an XMODEM transfer (deviation 3, rule A, decided by the owner on 2026-11-20). After at least one
// complete segment, the rest is padding when it is less than one 128-byte block, the file is a whole number of
// blocks, its last byte is $1A (the CP/M end-of-file mark), and the rest is no segment: all $1A, or a header
// that is cut or has start after end and does not begin with $FFFF (BACTERION V1 ends in $20 and 29 x $1A, which
// reads as start $1A20 and end $1A1A). A segment cut inside its data, like a cut RUNAD, stays Invalid.
bool isPadding(const uint8_t* bytes, std::size_t at, std::size_t size) noexcept
{
    const std::size_t count = size - at;
    if (count == 0 || count >= 128 || size % 128 != 0 || bytes[size - 1] != 0x1A) {
        return false;
    }
    bool allFill = true;
    for (std::size_t i = at; i < size; ++i) {
        allFill = allFill && bytes[i] == 0x1A;
    }
    if (allFill) {
        return true;
    }
    if (count >= 2 && bytes[at] == 0xFF && bytes[at + 1] == 0xFF) {
        return false;  // a segment marker: a cut segment, not fill
    }
    return count < 4 || word(bytes + at) > word(bytes + at + 2);
}

// Walks the segments. Ok, Invalid or Incompatible; `segments` counts the complete ones and
// `used` is the length without a trailing XMODEM fill.
XexWrap check(rb::core::Span<const uint8_t> xex, std::size_t& used) noexcept
{
    XexWrap result;
    const uint8_t* bytes = xex.data();
    const std::size_t size = xex.size();
    if (size < 2 || bytes[0] != 0xFF || bytes[1] != 0xFF) {
        return result;  // Invalid
    }
    bool incompatible = false;
    std::size_t at = 0;
    while (at < size) {
        if (isPadding(bytes, at, size)) {  // never at 0: the file would be under one block
            break;
        }
        uint32_t start = 0;
        uint32_t end = 0;
        if (!readSegment(bytes, size, at, start, end, at)) {
            result.segments = 0;
            return result;  // Invalid: a cut header or data, or start after end
        }
        if ((start <= kXexLoaderHigh && end >= kXexLoaderLow) ||
            (start <= XexImage::kStackHigh && end >= XexImage::kStackLow)) {
            incompatible = true;
        }
        ++result.segments;
    }
    used = at;
    result.result = incompatible ? XexResult::Incompatible : XexResult::Ok;
    return result;
}

}  // namespace

XexWrap XexImage::wrap(rb::core::Span<const uint8_t> xex, ImageBuffer& out) noexcept
{
    const std::size_t dataSectors = (xex.size() + kSectorSize - 1) / kSectorSize;
    if (dataSectors > kMaxSectors - kXexLoaderSectors) {
        return XexWrap{XexResult::TooLarge, 0};
    }
    std::size_t used = 0;
    const XexWrap checked = check(xex, used);
    if (checked.result != XexResult::Ok) {
        return checked;
    }
    const uint32_t sectors = kXexLoaderSectors + static_cast<uint32_t>((used + kSectorSize - 1) / kSectorSize);
    ImageBuffer image = ImageBuffer::allocate(kHeaderSize + sectors * kSectorSize);
    if (!image.valid()) {
        return XexWrap{XexResult::NoMemory, 0};
    }
    // ATR header: magic, size in 16-byte paragraphs (low, high), sector size 128, high size byte.
    uint8_t* atr = image.data();
    const uint32_t paragraphs = sectors * kSectorSize / 16;
    atr[0] = 0x96;
    atr[1] = 0x02;
    atr[2] = static_cast<uint8_t>(paragraphs & 0xFF);
    atr[3] = static_cast<uint8_t>((paragraphs >> 8) & 0xFF);
    atr[4] = static_cast<uint8_t>(kSectorSize & 0xFF);
    atr[5] = static_cast<uint8_t>(kSectorSize >> 8);
    atr[6] = static_cast<uint8_t>((paragraphs >> 16) & 0xFF);
    uint8_t* loader = atr + kHeaderSize;
    std::memcpy(loader, kXexLoaderBlock, sizeof kXexLoaderBlock);
    const uint32_t length = static_cast<uint32_t>(used);
    loader[kXexLoaderLengthOffset] = static_cast<uint8_t>(length & 0xFF);
    loader[kXexLoaderLengthOffset + 1] = static_cast<uint8_t>((length >> 8) & 0xFF);
    loader[kXexLoaderLengthOffset + 2] = static_cast<uint8_t>((length >> 16) & 0xFF);
    std::memcpy(loader + sizeof kXexLoaderBlock, xex.data(), used);
    out = std::move(image);
    return checked;
}

}  // namespace rb::host
