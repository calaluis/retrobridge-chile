#pragma once

// Test helper: builds ATR headers byte by byte from the documented layout
// (magic $0296, size in 16-byte paragraphs, sector size, paragraph high byte).

#include <array>
#include <cstdint>

namespace rb::test {

constexpr uint32_t kAtrHeaderSize = 16;

// Header with an explicit paragraph count (lets tests declare sizes that disagree
// with the real file, for D-8).
inline std::array<uint8_t, 16> make_atr_header_paragraphs(uint32_t paragraphs, uint16_t sector_size)
{
    std::array<uint8_t, 16> header{};
    header[0] = 0x96;
    header[1] = 0x02;
    header[2] = static_cast<uint8_t>(paragraphs & 0xFF);
    header[3] = static_cast<uint8_t>((paragraphs >> 8) & 0xFF);
    header[4] = static_cast<uint8_t>(sector_size & 0xFF);
    header[5] = static_cast<uint8_t>((sector_size >> 8) & 0xFF);
    header[6] = static_cast<uint8_t>((paragraphs >> 16) & 0xFF);
    return header;
}

// Header whose declared size matches `data_size` bytes after the header.
inline std::array<uint8_t, 16> make_atr_header(uint32_t data_size, uint16_t sector_size)
{
    return make_atr_header_paragraphs(data_size / 16, sector_size);
}

// Data sizes of the standard layouts.
constexpr uint32_t sd_data_size(uint32_t sectors) { return sectors * 128; }
constexpr uint32_t dd_data_size(uint32_t sectors)
{
    return sectors <= 3 ? sectors * 128 : 3 * 128 + (sectors - 3) * 256;
}

}  // namespace rb::test
