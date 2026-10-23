#pragma once

#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/domain/density.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::domain {

enum class SectorSize : uint16_t {
    Bytes128 = 128,
    Bytes256 = 256,
};

// Number of sectors and their size. Immutable.
// Invariants: 1 <= sectorCount <= 65535 (SIO sector numbers are 16-bit) and
// sectorSize is 128 or 256. Sectors 1-3 are always 128 bytes.
class DiskGeometry {
public:
    static constexpr uint32_t kMaxSectorCount = 65535;
    static constexpr uint16_t kBootSectorCount = 3;
    static constexpr uint16_t kBootSectorSize = 128;

    static rb::core::Result<DiskGeometry> create(uint32_t sectorCount, SectorSize sectorSize);

    uint16_t sectorCount() const noexcept { return sectorCount_; }
    SectorSize sectorSize() const noexcept { return sectorSize_; }

    bool contains(SectorNumber sector) const noexcept { return sector.value() <= sectorCount_; }

    // Size in bytes of the given sector (128 for boot sectors).
    uint16_t sizeOf(SectorNumber sector) const noexcept;

    Density density() const noexcept;

private:
    DiskGeometry(uint16_t sectorCount, SectorSize sectorSize) noexcept
        : sectorCount_(sectorCount), sectorSize_(sectorSize)
    {
    }

    uint16_t sectorCount_;
    SectorSize sectorSize_;
};

}  // namespace rb::domain
