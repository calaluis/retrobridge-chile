#pragma once

#include <optional>

#include "rb/app/drive_messages.hpp"
#include "rb/core/result.hpp"

namespace rb::app {

// Port between the storage context and the drive context (ADR-014).
// Exactly one exchange is in flight at a time:
//   Idle --submit--> Requested --takeRequest--> Processing --putReply--> Replied --takeReply--> Idle
// so a reply always has room and is never dropped.
//
// Contract:
//   * submit() moves the request out only on success; on MailboxBusy the caller
//     still owns it (and its DiskImage), so nothing is lost.
//   * putReply() is only valid after takeRequest(); otherwise it aborts.
//   * The implementation provides the ordering and memory visibility needed to
//     hand objects between contexts (an RTOS queue on the ESP32, H3/H5).
class DriveMailbox {
public:
    DriveMailbox(const DriveMailbox&) = delete;
    DriveMailbox& operator=(const DriveMailbox&) = delete;

    // Storage context
    virtual rb::core::Result<void> submit(DriveRequest& request) = 0;
    virtual std::optional<DriveReply> takeReply() = 0;

    // Drive context
    virtual std::optional<DriveRequest> takeRequest() = 0;
    virtual void putReply(const DriveReply& reply) = 0;

protected:
    DriveMailbox() = default;
    ~DriveMailbox() = default;  // never deleted through the interface
};

}  // namespace rb::app
