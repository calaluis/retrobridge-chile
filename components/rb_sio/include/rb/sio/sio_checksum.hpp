#pragma once

#include <cstdint>

#include "rb/core/span.hpp"

namespace rb::sio {

// SIO frame checksum: arithmetic sum of the bytes with the carry added back
// after every addition (Atari C016555 §9). Used for command and data frames.
inline uint8_t sio_checksum(rb::core::Span<const uint8_t> bytes) noexcept
{
    unsigned sum = 0;
    for (uint8_t byte : bytes) {
        sum += byte;
        if (sum > 0xFF) {
            sum = (sum & 0xFF) + 1;
        }
    }
    return static_cast<uint8_t>(sum);
}

}  // namespace rb::sio
