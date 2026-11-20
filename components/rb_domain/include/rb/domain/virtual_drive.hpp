#pragma once

#include <cstdint>
#include <optional>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/domain/drive_status.hpp"
#include "rb/domain/image_byte_source.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::domain {

// Aggregate root: an emulated drive (D1..D8) that is either empty or holds one
// DiskImage. Knows nothing about SIO bytes, timing, UART or files.
//
// Ownership (H1-8): mount() and unmount() return the source that is no longer
// referenced (or nullptr). It is already detached when returned, so the
// application can hand it back to its repository exactly once.
// Not copyable or movable: a drive is an entity with a fixed identity.
class VirtualDrive {
public:
    explicit VirtualDrive(DriveNumber number) noexcept : number_(number) {}

    VirtualDrive(const VirtualDrive&) = delete;
    VirtualDrive& operator=(const VirtualDrive&) = delete;
    VirtualDrive(VirtualDrive&&) = delete;
    VirtualDrive& operator=(VirtualDrive&&) = delete;

    DriveNumber number() const noexcept { return number_; }
    bool hasDisk() const noexcept { return disk_.has_value(); }

    // Inserts `image`, replacing any current disk. Clears the failure flag.
    rb::core::Result<ImageByteSource*> mount(DiskImage&& image);

    // Removes the current disk, if any. Clears the failure flag.
    ImageByteSource* unmount() noexcept;

    // Reads a sector of the current disk and records whether it failed.
    rb::core::Result<uint16_t> readSector(SectorNumber sector, rb::core::Span<uint8_t> out);

    // Writes a sector of the current disk and records whether it failed.
    rb::core::Result<uint16_t> writeSector(SectorNumber sector, rb::core::Span<const uint8_t> data);

    // Formats the mounted disk (DiskImage::format); NoDiskMounted when empty.
    // Records the outcome like a read or a write.
    rb::core::Result<void> format(Density density);

    DriveStatus status() const noexcept;

    // In-memory queries for the SIO dispatcher's first phase (ADR-005): no I/O,
    // and they do not change lastOperationFailed.
    // validateRead: size of `sector` on the current disk, or NoDiskMounted /
    // SectorOutOfRange. geometry: nullptr when the drive is empty.
    rb::core::Result<uint16_t> validateRead(SectorNumber sector) const noexcept;
    // validateWrite: the same check for a write. Protection is not checked here:
    // a protected drive takes the data frame and then reports the error.
    rb::core::Result<uint16_t> validateWrite(SectorNumber sector) const noexcept { return validateRead(sector); }
    const DiskGeometry* geometry() const noexcept { return disk_ ? &disk_->geometry() : nullptr; }

private:
    DriveNumber number_;
    std::optional<DiskImage> disk_;
    bool lastOperationFailed_ = false;
};

}  // namespace rb::domain
