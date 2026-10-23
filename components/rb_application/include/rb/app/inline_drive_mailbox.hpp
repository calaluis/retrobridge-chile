#pragma once

#include <optional>
#include <utility>

#include "rb/app/ports/drive_mailbox.hpp"
#include "rb/core/assert.hpp"

namespace rb::app {

// DriveMailbox for when the storage and drive contexts run in the same task,
// strictly one after the other (H3-7: the diagnostic console in H3; the host
// tests). One exchange in flight: Idle -> Requested -> Processing -> Replied.
// It has no synchronisation, so it must never be shared between tasks; H5
// replaces it with an RTOS-queue implementation of the same contract (ADR-014).
class InlineDriveMailbox final : public DriveMailbox {
public:
    InlineDriveMailbox() = default;

    rb::core::Result<void> submit(DriveRequest& request) override
    {
        if (state_ != State::Idle) {
            return rb::core::Failure{rb::core::ErrorCode::MailboxBusy};
        }
        request_.emplace(std::move(request));
        state_ = State::Requested;
        return {};
    }

    std::optional<DriveReply> takeReply() override
    {
        if (state_ != State::Replied) {
            return std::nullopt;
        }
        std::optional<DriveReply> reply = reply_;
        reply_.reset();
        state_ = State::Idle;
        return reply;
    }

    std::optional<DriveRequest> takeRequest() override
    {
        if (state_ != State::Requested) {
            return std::nullopt;
        }
        std::optional<DriveRequest> request = std::move(request_);
        request_.reset();
        state_ = State::Processing;
        return request;
    }

    void putReply(const DriveReply& reply) override
    {
        RB_ASSERT(state_ == State::Processing);  // a reply always answers a taken request
        reply_.emplace(reply);
        state_ = State::Replied;
    }

    bool idle() const noexcept { return state_ == State::Idle; }

private:
    enum class State { Idle, Requested, Processing, Replied };

    State state_ = State::Idle;
    std::optional<DriveRequest> request_;
    std::optional<DriveReply> reply_;
};

}  // namespace rb::app
