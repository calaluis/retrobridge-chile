#pragma once

#include <optional>

#include "rb/app/ports/drive_mailbox.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/core/error_code.hpp"
#include "rb/domain/drive_number.hpp"

namespace rb::app {

struct DriveOutcome {
    DriveRequestKind kind;
    rb::domain::DriveNumber drive;
    std::optional<rb::core::ErrorCode> error;  // nullopt: applied
};

// Storage context (ADR-014): ends an exchange. Closes the displaced and rejected
// sources carried by the reply (each arrives exactly once, already detached), so
// no source is leaked or closed twice. nullopt if no reply is waiting.
class CollectDriveReply {
public:
    CollectDriveReply(ImageRepository& repository, DriveMailbox& mailbox) noexcept
        : repository_(repository), mailbox_(mailbox)
    {
    }

    std::optional<DriveOutcome> execute()
    {
        std::optional<DriveReply> reply = mailbox_.takeReply();
        if (!reply) {
            return std::nullopt;
        }
        repository_.close(reply->displaced);
        repository_.close(reply->rejected);
        return DriveOutcome{reply->kind, reply->drive, reply->error};
    }

private:
    ImageRepository& repository_;
    DriveMailbox& mailbox_;
};

}  // namespace rb::app
