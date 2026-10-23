#pragma once

#include <cstdint>

namespace rb::domain {

// Recording density, derived from the disk geometry.
enum class Density : uint8_t {
    Unknown,      // no disk in the drive
    Single,       // 720 sectors x 128 bytes
    Enhanced,     // 1040 sectors x 128 bytes ("densidad media")
    Double,       // 720 sectors x 256 bytes (boot sectors 1-3 are 128 bytes)
    NonStandard,  // any other combination
};

}  // namespace rb::domain
