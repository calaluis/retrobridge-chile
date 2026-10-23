#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "rb/app/image_location.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/domain/image_byte_source.hpp"
#include "rb/domain/virtual_drive.hpp"

namespace rb::app {

// The configured drives D1..Dn (n = 1 in v1) and the location mounted in each.
// Owned and used exclusively by the drive context (ADR-014). Storage is inline:
// no allocation. Mounting keeps the drive and its location consistent.
class DriveBay {
public:
    explicit DriveBay(uint8_t driveCount);

    DriveBay(const DriveBay&) = delete;
    DriveBay& operator=(const DriveBay&) = delete;
    DriveBay(DriveBay&&) = delete;
    DriveBay& operator=(DriveBay&&) = delete;

    uint8_t driveCount() const noexcept { return driveCount_; }

    // nullptr if the drive is not configured.
    rb::domain::VirtualDrive* find(rb::domain::DriveNumber drive) noexcept;
    const rb::domain::VirtualDrive* find(rb::domain::DriveNumber drive) const noexcept;
    const ImageLocation* mountedLocation(rb::domain::DriveNumber drive) const noexcept;

    // As VirtualDrive::mount/unmount: return the displaced (detached) source.
    // DriveNotConfigured leaves `image` untouched, still owned by the caller.
    rb::core::Result<rb::domain::ImageByteSource*> mount(rb::domain::DriveNumber drive, rb::domain::DiskImage&& image,
                                                         const ImageLocation& location);
    rb::core::Result<rb::domain::ImageByteSource*> unmount(rb::domain::DriveNumber drive);

private:
    struct Slot {
        std::optional<rb::domain::VirtualDrive> drive;
        std::optional<ImageLocation> location;
    };

    Slot* slot(rb::domain::DriveNumber drive) noexcept;
    const Slot* slot(rb::domain::DriveNumber drive) const noexcept;

    std::array<Slot, rb::domain::DriveNumber::kMax> slots_{};
    uint8_t driveCount_;
};

}  // namespace rb::app
