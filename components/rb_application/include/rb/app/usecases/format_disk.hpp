#pragma once

#include "rb/app/drive_bay.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/density.hpp"
#include "rb/domain/drive_number.hpp"

namespace rb::app {

// Drive context: formats the disk in a configured drive, every sector to $00
// (menu-formatear-disco, M46-1). The drive records whether the operation
// failed. WriteProtected when the disk is; DensityMismatch when `density` is
// not the disk's own Single or Enhanced density.
class FormatDisk {
public:
    explicit FormatDisk(DriveBay& bay) noexcept : bay_(bay) {}

    rb::core::Result<void> execute(rb::domain::DriveNumber drive, rb::domain::Density density)
    {
        rb::domain::VirtualDrive* found = bay_.find(drive);
        if (found == nullptr) {
            return rb::core::Failure{rb::core::ErrorCode::DriveNotConfigured};
        }
        return found->format(density);
    }

private:
    DriveBay& bay_;
};

}  // namespace rb::app
