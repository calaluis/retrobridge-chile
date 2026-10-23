#pragma once

#include "rb/app/drive_bay.hpp"
#include "rb/app/ports/drive_mailbox.hpp"

namespace rb::app {

// Drive context (ADR-014): applies at most one pending mount/unmount in O(1),
// between SIO transactions. Never touches the ImageRepository and never blocks:
// sources to close travel back in the reply (displaced or rejected).
class ApplyDriveRequest {
public:
    ApplyDriveRequest(DriveBay& bay, DriveMailbox& mailbox) noexcept : bay_(bay), mailbox_(mailbox) {}

    // true if a request was applied and answered.
    bool execute();

private:
    DriveBay& bay_;
    DriveMailbox& mailbox_;
};

}  // namespace rb::app
