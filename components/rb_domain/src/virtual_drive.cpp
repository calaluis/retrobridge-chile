#include "rb/domain/virtual_drive.hpp"

#include <utility>

namespace rb::domain {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

Result<ImageByteSource*> VirtualDrive::mount(DiskImage&& image)
{
    if (!image.isValid()) {
        return Failure{ErrorCode::InvalidDiskImage};
    }
    ImageByteSource* displaced = disk_ ? disk_->source() : nullptr;
    disk_ = std::move(image);  // move-assignment detaches the displaced source
    lastOperationFailed_ = false;
    return displaced;
}

ImageByteSource* VirtualDrive::unmount() noexcept
{
    lastOperationFailed_ = false;
    if (!disk_) {
        return nullptr;
    }
    ImageByteSource* displaced = disk_->source();
    disk_.reset();  // the destructor detaches the source
    return displaced;
}

Result<uint16_t> VirtualDrive::readSector(SectorNumber sector, Span<uint8_t> out)
{
    if (!disk_) {
        lastOperationFailed_ = true;
        return Failure{ErrorCode::NoDiskMounted};
    }
    Result<uint16_t> result = disk_->readSector(sector, out);
    lastOperationFailed_ = !result.ok();
    return result;
}

Result<uint16_t> VirtualDrive::writeSector(SectorNumber sector, Span<const uint8_t> data)
{
    if (!disk_) {
        lastOperationFailed_ = true;
        return Failure{ErrorCode::NoDiskMounted};
    }
    Result<uint16_t> result = disk_->writeSector(sector, data);
    lastOperationFailed_ = !result.ok();
    return result;
}

Result<void> VirtualDrive::format(Density density)
{
    if (!disk_) {
        lastOperationFailed_ = true;
        return Failure{ErrorCode::NoDiskMounted};
    }
    Result<void> result = disk_->format(density);
    lastOperationFailed_ = !result.ok();
    return result;
}

Result<uint16_t> VirtualDrive::validateRead(SectorNumber sector) const noexcept
{
    if (!disk_) {
        return Failure{ErrorCode::NoDiskMounted};
    }
    const DiskGeometry& geometry = disk_->geometry();
    if (!geometry.contains(sector)) {
        return Failure{ErrorCode::SectorOutOfRange};
    }
    return geometry.sizeOf(sector);
}

DriveStatus VirtualDrive::status() const noexcept
{
    // An empty drive reports protection, as before; a disk reports its own.
    if (!disk_) {
        return DriveStatus{false, true, lastOperationFailed_, Density::Unknown};
    }
    return DriveStatus{true, disk_->readOnly(), lastOperationFailed_, disk_->geometry().density()};
}

}  // namespace rb::domain
