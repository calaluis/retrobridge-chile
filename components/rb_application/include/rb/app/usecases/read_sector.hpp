#pragma once

#include <cstdint>

#include "rb/app/drive_bay.hpp"
#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::app {

// Drive context: reads one sector of the disk in a configured drive into `out`.
// Returns the sector size; the drive records whether the operation failed.
class ReadSector {
public:
    explicit ReadSector(DriveBay& bay) noexcept : bay_(bay) {}

    rb::core::Result<uint16_t> execute(rb::domain::DriveNumber drive, rb::domain::SectorNumber sector,
                                       rb::core::Span<uint8_t> out)
    {
        rb::domain::VirtualDrive* found = bay_.find(drive);
        if (found == nullptr) {
            return rb::core::Failure{rb::core::ErrorCode::DriveNotConfigured};
        }
        return found->readSector(sector, out);
    }

private:
    DriveBay& bay_;
};

}  // namespace rb::app
