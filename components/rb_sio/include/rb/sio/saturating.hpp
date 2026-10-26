#pragma once

#include <cstddef>
#include <cstdint>

namespace rb::sio {

// Saturating counter arithmetic for the SIO statistics (SioListener,
// CommandFrameAssembler): counters stop at UINT32_MAX instead of wrapping.

// Adds one, stopping at UINT32_MAX.
constexpr uint32_t saturating_increment(uint32_t value) noexcept
{
    return value == UINT32_MAX ? value : value + 1;
}

// Adds `amount`, stopping at UINT32_MAX.
constexpr uint32_t saturating_add(uint32_t value, std::size_t amount) noexcept
{
    return amount > UINT32_MAX - value ? UINT32_MAX : value + static_cast<uint32_t>(amount);
}

}  // namespace rb::sio
