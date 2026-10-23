#pragma once

// Test-only CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320, init and final
// XOR 0xFFFFFFFF), bitwise. Independent of zlib (used by tools/make_atr.py) and
// of the ESP32 ROM routine used on target: Meta A compares the three.

#include <cstddef>
#include <cstdint>

namespace rb::test {

inline uint32_t crc32(const uint8_t* data, std::size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace rb::test
