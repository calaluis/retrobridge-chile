#pragma once

#include "rb/app/ports/drive_mailbox.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/drive_number.hpp"

namespace rb::app {

// Storage context (ADR-014): asks the drive context to remove the disk from a
// drive. The source is closed by CollectDriveReply once the drive returns it.
class UnmountDiskImage {
public:
    explicit UnmountDiskImage(DriveMailbox& mailbox) noexcept : mailbox_(mailbox) {}

    // MailboxBusy if another exchange is in flight.
    rb::core::Result<void> execute(rb::domain::DriveNumber drive)
    {
        DriveRequest request = DriveRequest::unmount(drive);
        return mailbox_.submit(request);
    }

private:
    DriveMailbox& mailbox_;
};

}  // namespace rb::app
