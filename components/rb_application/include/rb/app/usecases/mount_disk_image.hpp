#pragma once

#include <cstdint>

#include "rb/app/image_location.hpp"
#include "rb/app/ports/drive_mailbox.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/disk_geometry.hpp"
#include "rb/domain/drive_number.hpp"

namespace rb::app {

// What the prepared image looks like. The D-8 discrepancies are reported here so
// the caller can log them; the domain never logs.
struct MountReport {
    rb::domain::DiskGeometry geometry;
    bool headerSizeMismatch;
    uint32_t ignoredTrailingBytes;
    bool writeProtected = true;  // the disk refuses writes (menu-escritura-disco)
};

// Storage context (ADR-014). Opens the image and validates its header (may block,
// e.g. TNFS later), then hands the prepared DiskImage to the drive context. The
// mount takes effect when ApplyDriveRequest runs; CollectDriveReply then closes
// whatever source was displaced or rejected.
// On any failure the opened source is closed here and nothing reaches the drive.
class MountDiskImage {
public:
    MountDiskImage(ImageRepository& repository, DriveMailbox& mailbox) noexcept
        : repository_(repository), mailbox_(mailbox)
    {
    }

    // NotFound, NoFreeSlot, ATR errors, IoError or MailboxBusy.
    rb::core::Result<MountReport> execute(rb::domain::DriveNumber drive, const ImageLocation& location);

private:
    ImageRepository& repository_;
    DriveMailbox& mailbox_;
};

}  // namespace rb::app
