#pragma once

#include "rb/app/drive_bay.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/domain/drive_status.hpp"

namespace rb::app {

// Drive context: status of a configured drive (DriveNotConfigured otherwise).
class GetDriveStatus {
public:
    explicit GetDriveStatus(const DriveBay& bay) noexcept : bay_(bay) {}

    rb::core::Result<rb::domain::DriveStatus> execute(rb::domain::DriveNumber drive) const
    {
        const rb::domain::VirtualDrive* found = bay_.find(drive);
        if (found == nullptr) {
            return rb::core::Failure{rb::core::ErrorCode::DriveNotConfigured};
        }
        return found->status();
    }

private:
    const DriveBay& bay_;
};

}  // namespace rb::app
