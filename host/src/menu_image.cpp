#include "menu_image.hpp"

namespace rb::host {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

static_assert(kMenuLoadSectors * 128 == sizeof kMenuBlock, "the block is whole sectors");
static_assert(kMenuReplySize == 2 + kMenuScreenSize, "action, layout, screen");
static_assert(kMainFooter + 2 * kMenuLineWidth == kMainMessage, "message goes under the footer");
static_assert(kMainMessage + kMenuLineWidth <= kMenuScreenSize, "message fits");
static_assert(kListFooter + kMenuLineWidth == kListMessage, "message goes under the footer");
static_assert(kListMessage + kMenuLineWidth <= kMenuScreenSize, "message fits");

namespace {

void toScreen(const char* text, Span<uint8_t> line, bool upper) noexcept
{
    std::size_t i = 0;
    for (; i < line.size() && text[i] != '\0'; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (upper && c >= 'a' && c <= 'z') {
            c = static_cast<unsigned char>(c - 'a' + 'A');
        }
        if (c < 0x20 || c > 0x7E) {
            c = '?';
        }
        line[i] = static_cast<uint8_t>(c < 0x60 ? c - 0x20 : c);
    }
    for (; i < line.size(); ++i) {
        line[i] = 0;  // space
    }
}

}  // namespace

void screenLine(const char* text, Span<uint8_t> line) noexcept { toScreen(text, line, true); }
void screenText(const char* text, Span<uint8_t> line) noexcept { toScreen(text, line, false); }

Result<void> MenuImage::readAt(uint32_t offset, Span<uint8_t> out)
{
    if (offset > kMenuImageSize || out.size() > kMenuImageSize - offset) {
        return Failure{ErrorCode::OutOfBounds};
    }
    // ATR header: magic, size in 16-byte paragraphs (low, high), sector size 128, high size byte.
    constexpr uint32_t paragraphs = (kMenuSectors * 128) / 16;
    const uint8_t header[kMenuHeaderSize] = {0x96, 0x02, paragraphs & 0xFF, (paragraphs >> 8) & 0xFF, 0x80, 0x00,
                                            (paragraphs >> 16) & 0xFF};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const uint32_t at = offset + static_cast<uint32_t>(i);
        if (at < kMenuHeaderSize) {
            out[i] = header[at];
        } else if (at - kMenuHeaderSize < sizeof kMenuBlock) {
            out[i] = kMenuBlock[at - kMenuHeaderSize];
        } else {
            out[i] = 0;
        }
    }
    return {};
}

}  // namespace rb::host
