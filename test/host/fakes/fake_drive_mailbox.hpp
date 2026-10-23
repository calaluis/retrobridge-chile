#pragma once

// Single-threaded DriveMailbox for host tests: one exchange in flight at a time
// (Idle -> Requested -> Processing -> Replied -> Idle). The ESP32 implementation
// (H3/H5) will keep the same state machine over an RTOS queue.

#include <optional>
#include <utility>

#include "rb/app/ports/drive_mailbox.hpp"
#include "rb/core/assert.hpp"

namespace rb::test {

class FakeDriveMailbox final : public rb::app::DriveMailbox {
public:
    rb::core::Result<void> submit(rb::app::DriveRequest& request) override
    {
        if (state_ != State::Idle) {
            return rb::core::Failure{rb::core::ErrorCode::MailboxBusy};
        }
        request_.emplace(std::move(request));
        state_ = State::Requested;
        return {};
    }

    std::optional<rb::app::DriveReply> takeReply() override
    {
        if (state_ != State::Replied) {
            return std::nullopt;
        }
        std::optional<rb::app::DriveReply> reply = reply_;
        reply_.reset();
        state_ = State::Idle;
        return reply;
    }

    std::optional<rb::app::DriveRequest> takeRequest() override
    {
        if (state_ != State::Requested) {
            return std::nullopt;
        }
        std::optional<rb::app::DriveRequest> request = std::move(request_);
        request_.reset();
        state_ = State::Processing;
        return request;
    }

    void putReply(const rb::app::DriveReply& reply) override
    {
        RB_ASSERT(state_ == State::Processing);  // a reply always answers a taken request
        reply_.emplace(reply);
        state_ = State::Replied;
    }

    bool idle() const { return state_ == State::Idle; }

private:
    enum class State { Idle, Requested, Processing, Replied };

    State state_ = State::Idle;
    std::optional<rb::app::DriveRequest> request_;
    std::optional<rb::app::DriveReply> reply_;
};

}  // namespace rb::test
