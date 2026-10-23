#include "rb/domain/disk_geometry.hpp"

namespace rb::domain {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;

Result<DiskGeometry> DiskGeometry::create(uint32_t sectorCount, SectorSize sectorSize)
{
    if (sectorCount == 0 || sectorCount > kMaxSectorCount) {
        return Failure{ErrorCode::InvalidGeometry};
    }
    if (sectorSize != SectorSize::Bytes128 && sectorSize != SectorSize::Bytes256) {
        return Failure{ErrorCode::InvalidGeometry};
    }
    return DiskGeometry{static_cast<uint16_t>(sectorCount), sectorSize};
}

uint16_t DiskGeometry::sizeOf(SectorNumber sector) const noexcept
{
    if (sector.value() <= kBootSectorCount) {
        return kBootSectorSize;
    }
    return static_cast<uint16_t>(sectorSize_);
}

Density DiskGeometry::density() const noexcept
{
    if (sectorSize_ == SectorSize::Bytes128) {
        if (sectorCount_ == 720) {
            return Density::Single;
        }
        if (sectorCount_ == 1040) {
            return Density::Enhanced;
        }
    } else if (sectorCount_ == 720) {
        return Density::Double;
    }
    return Density::NonStandard;
}

}  // namespace rb::domain
