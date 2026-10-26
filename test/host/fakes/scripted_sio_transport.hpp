#pragma once

// Scripted SioTransport for host tests. Incoming events (frames, timeouts,
// receive errors) are queued by the test; every send() is recorded with its
// start and end time and advances the FakeClock by the bytes' time on the wire,
// so send() returns "after the last stop bit" as the port contract requires.
// Fixed-capacity storage only: it allocates nothing while a session runs.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "fakes/fake_clock.hpp"
#include "rb/app/ports/sio_transport.hpp"
#include "rb/core/assert.hpp"

namespace rb::test {

class ScriptedSioTransport final : public rb::app::SioTransport {
public:
    static constexpr std::size_t kMaxEvents = 64;
    static constexpr std::size_t kMaxSends = 8;
    static constexpr std::size_t kMaxSendBytes = 1 + 256 + 1;  // code + largest sector + checksum
    static constexpr uint64_t kDefaultFrameTimeUs = 3000;      // t0 + 5 bytes + t1, roughly
    static constexpr std::size_t kNoFailure = SIZE_MAX;

    struct Sent {
        std::array<uint8_t, kMaxSendBytes> bytes;
        std::size_t length;
        uint64_t startUs;
        uint64_t endUs;
    };

    explicit ScriptedSioTransport(FakeClock& clock) : clock_(clock) {}

    // --- Script ---------------------------------------------------------------------
    void push_frame(const std::array<uint8_t, 5>& bytes, uint8_t count = 5, uint64_t arrivalUs = kDefaultFrameTimeUs)
    {
        push(Event{Kind::Frame, bytes, count, arrivalUs});
    }
    void push_timeout() { push(Event{Kind::Timeout, {}, 0, 0}); }
    void push_receive_error() { push(Event{Kind::ReceiveError, {}, 0, 0}); }

    // The `index`-th call to send() (0-based) fails without transmitting.
    void fail_send(std::size_t index) { fail_send_index_ = index; }

    // --- Observations -----------------------------------------------------------------
    std::size_t sends() const { return send_count_; }
    const Sent& sent(std::size_t index) const
    {
        RB_ASSERT(index < send_count_);
        return sent_[index];
    }
    unsigned releases() const { return releases_; }
    unsigned waits() const { return waits_; }
    uint64_t last_command_released_at() const { return last_released_at_; }

    // --- SioTransport --------------------------------------------------------------
    rb::core::Result<rb::app::CommandBytes> waitCommandFrame(uint32_t timeoutUs) override
    {
        ++waits_;
        if (next_ == size_) {
            clock_.advance(timeoutUs);
            return rb::core::Failure{rb::core::ErrorCode::Timeout};
        }
        const Event event = events_[next_++];
        switch (event.kind) {
        case Kind::Timeout:
            clock_.advance(timeoutUs);
            return rb::core::Failure{rb::core::ErrorCode::Timeout};
        case Kind::ReceiveError:
            return rb::core::Failure{rb::core::ErrorCode::TransportError};
        case Kind::Frame:
        default: {
            // This double does not model COMMAND edges: the wait start stands
            // in for COMMAND going low.
            const uint64_t asserted_at = clock_.nowMicros();
            clock_.advance(event.arrivalUs);
            last_released_at_ = clock_.nowMicros();
            return rb::app::CommandBytes{event.bytes, event.count, last_released_at_, asserted_at};
        }
        }
    }

    rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) override
    {
        const std::size_t attempt = send_attempts_++;
        if (attempt == fail_send_index_) {
            return rb::core::Failure{rb::core::ErrorCode::TransportError};
        }
        RB_ASSERT(send_count_ < kMaxSends && bytes.size() <= kMaxSendBytes);
        Sent& record = sent_[send_count_++];
        std::copy(bytes.begin(), bytes.end(), record.bytes.begin());
        record.length = bytes.size();
        record.startUs = clock_.nowMicros();
        clock_.advance(sio_byte_time_us(bytes.size()));
        record.endUs = clock_.nowMicros();
        return {};
    }

    void releaseDataLine() override { ++releases_; }

private:
    enum class Kind { Frame, Timeout, ReceiveError };
    struct Event {
        Kind kind;
        std::array<uint8_t, 5> bytes;
        uint8_t count;
        uint64_t arrivalUs;
    };

    void push(const Event& event)
    {
        RB_ASSERT(size_ < kMaxEvents);
        events_[size_++] = event;
    }

    FakeClock& clock_;
    std::array<Event, kMaxEvents> events_{};
    std::size_t size_ = 0;
    std::size_t next_ = 0;
    std::array<Sent, kMaxSends> sent_{};
    std::size_t send_count_ = 0;
    std::size_t send_attempts_ = 0;
    std::size_t fail_send_index_ = kNoFailure;
    unsigned releases_ = 0;
    unsigned waits_ = 0;
    uint64_t last_released_at_ = 0;
};

}  // namespace rb::test
