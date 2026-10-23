#pragma once

#include <cstdint>

#include "rb/core/result.hpp"

namespace rb::domain {

// Drive D1..D8. Invariant: 1 <= value <= 8.
// The mapping to an SIO device ID belongs to the SIO context, not here.
class DriveNumber {
public:
    static constexpr uint8_t kMax = 8;

    static rb::core::Result<DriveNumber> from(uint8_t value)
    {
        if (value < 1 || value > kMax) {
            return rb::core::Failure{rb::core::ErrorCode::InvalidDriveNumber};
        }
        return DriveNumber{value};
    }

    constexpr uint8_t value() const noexcept { return value_; }

    friend constexpr bool operator==(DriveNumber a, DriveNumber b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(DriveNumber a, DriveNumber b) noexcept { return a.value_ != b.value_; }

private:
    constexpr explicit DriveNumber(uint8_t value) noexcept : value_(value) {}

    uint8_t value_;
};

}  // namespace rb::domain
