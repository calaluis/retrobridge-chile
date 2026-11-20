#include "xex_image.hpp"

#include <cstring>
#include <utility>

namespace rb::host {

namespace {

#include "xex_loader.inc"

static_assert(kXexLoaderSectors * XexImage::kSectorSize == sizeof kXexLoaderBlock, "the loader is whole sectors");
static_assert(kXexLoaderLengthOffset + 3 <= sizeof kXexLoaderBlock, "the length fits in the loader");
static_assert(kXexLoaderLow <= kXexLoaderHigh, "the loader zone is not empty");

uint32_t word(const uint8_t* at) noexcept { return at[0] | (static_cast<uint32_t>(at[1]) << 8); }

// Walks the segments. Ok, Invalid or Incompatible; `segments` counts the complete ones.
XexWrap check(rb::core::Span<const uint8_t> xex) noexcept
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
        if (size - at >= 2 && bytes[at] == 0xFF && bytes[at + 1] == 0xFF) {
            at += 2;
        }
        if (size - at < 4) {
            result.segments = 0;
            return result;  // Invalid: a cut header (or a $FFFF with nothing after it)
        }
        const uint32_t start = word(bytes + at);
        const uint32_t end = word(bytes + at + 2);
        at += 4;
        if (start > end || size - at < end - start + 1) {
            result.segments = 0;
            return result;  // Invalid: start after end, or cut data
        }
        at += end - start + 1;
        if (start <= kXexLoaderHigh && end >= kXexLoaderLow) {
            incompatible = true;
        }
        ++result.segments;
    }
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
    const XexWrap checked = check(xex);
    if (checked.result != XexResult::Ok) {
        return checked;
    }
    const uint32_t sectors = kXexLoaderSectors + static_cast<uint32_t>(dataSectors);
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
    const uint32_t length = static_cast<uint32_t>(xex.size());
    loader[kXexLoaderLengthOffset] = static_cast<uint8_t>(length & 0xFF);
    loader[kXexLoaderLengthOffset + 1] = static_cast<uint8_t>((length >> 8) & 0xFF);
    loader[kXexLoaderLengthOffset + 2] = static_cast<uint8_t>((length >> 16) & 0xFF);
    std::memcpy(loader + sizeof kXexLoaderBlock, xex.data(), xex.size());
    out = std::move(image);
    return checked;
}

}  // namespace rb::host
