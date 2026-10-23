#include "rb/app/drive_bay.hpp"

#include <utility>

#include "rb/core/assert.hpp"

namespace rb::app {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::domain::ImageByteSource;
using rb::domain::VirtualDrive;

DriveBay::DriveBay(uint8_t driveCount) : driveCount_(driveCount)
{
    RB_ASSERT(driveCount >= 1 && driveCount <= DriveNumber::kMax);  // configuration error
    for (uint8_t n = 1; n <= driveCount; ++n) {
        slots_[n - 1].drive.emplace(DriveNumber::from(n).value());
    }
}

DriveBay::Slot* DriveBay::slot(DriveNumber drive) noexcept
{
    return drive.value() <= driveCount_ ? &slots_[drive.value() - 1] : nullptr;
}

const DriveBay::Slot* DriveBay::slot(DriveNumber drive) const noexcept
{
    return drive.value() <= driveCount_ ? &slots_[drive.value() - 1] : nullptr;
}

VirtualDrive* DriveBay::find(DriveNumber drive) noexcept
{
    Slot* s = slot(drive);
    return s != nullptr ? &*s->drive : nullptr;
}

const VirtualDrive* DriveBay::find(DriveNumber drive) const noexcept
{
    const Slot* s = slot(drive);
    return s != nullptr ? &*s->drive : nullptr;
}

const ImageLocation* DriveBay::mountedLocation(DriveNumber drive) const noexcept
{
    const Slot* s = slot(drive);
    return s != nullptr && s->location ? &*s->location : nullptr;
}

Result<ImageByteSource*> DriveBay::mount(DriveNumber drive, DiskImage&& image, const ImageLocation& location)
{
    Slot* s = slot(drive);
    if (s == nullptr) {
        return Failure{ErrorCode::DriveNotConfigured};
    }
    Result<ImageByteSource*> result = s->drive->mount(std::move(image));
    if (result.ok()) {
        s->location = location;
    }
    return result;
}

Result<ImageByteSource*> DriveBay::unmount(DriveNumber drive)
{
    Slot* s = slot(drive);
    if (s == nullptr) {
        return Failure{ErrorCode::DriveNotConfigured};
    }
    s->location.reset();
    return s->drive->unmount();
}

}  // namespace rb::app
