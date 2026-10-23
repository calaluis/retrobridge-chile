#pragma once

#include <cstdint>

#include "rb/core/result.hpp"

namespace rb::domain {

// Logical sector number, 1-based. Invariant: value >= 1.
// Whether the sector exists on a given disk is checked by DiskGeometry.
class SectorNumber {
public:
    static rb::core::Result<SectorNumber> from(uint16_t value)
    {
        if (value == 0) {
            return rb::core::Failure{rb::core::ErrorCode::InvalidSectorNumber};
        }
        return SectorNumber{value};
    }

    constexpr uint16_t value() const noexcept { return value_; }

    friend constexpr bool operator==(SectorNumber a, SectorNumber b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(SectorNumber a, SectorNumber b) noexcept { return a.value_ != b.value_; }

private:
    constexpr explicit SectorNumber(uint16_t value) noexcept : value_(value) {}

    uint16_t value_;
};

}  // namespace rb::domain
