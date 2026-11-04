#pragma once

// Scripted DatagramPort for host tests. The test queues what the peer (hub or
// emulator) sends: datagrams, timeouts and receive errors, each delivered after
// an optional delay on the FakeClock. Every datagram the transport sends is
// recorded. An empty queue behaves as silence: receive() advances the clock by
// the whole timeout and returns Timeout. Fixed-capacity storage: it allocates
// nothing while a session runs.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "fakes/fake_clock.hpp"
#include "rb/core/assert.hpp"
#include "rb/netsio/datagram_port.hpp"
#include "rb/netsio/netsio_codec.hpp"

namespace rb::test {

class ScriptedDatagramPort final : public rb::netsio::DatagramPort {
public:
    static constexpr std::size_t kMaxEvents = 96;
    static constexpr std::size_t kMaxSent = 96;
    static constexpr std::size_t kMaxBytes = rb::netsio::kMaxDatagram;
    static constexpr std::size_t kNoFailure = SIZE_MAX;

    struct Datagram {
        std::array<uint8_t, kMaxBytes> bytes;
        std::size_t length;
        uint64_t atUs;  // clock when sent or delivered
    };

    explicit ScriptedDatagramPort(FakeClock& clock) : clock_(clock) {}

    // --- Script -----------------------------------------------------------------
    void push(std::initializer_list<uint8_t> bytes, uint64_t delayUs = 0)
    {
        RB_ASSERT(bytes.size() <= kMaxBytes);
        Event& event = next_event(Kind::Datagram, delayUs);
        std::copy(bytes.begin(), bytes.end(), event.bytes.begin());
        event.length = bytes.size();
    }
    void push_bytes(const uint8_t* bytes, std::size_t length, uint64_t delayUs = 0)
    {
        RB_ASSERT(length <= kMaxBytes);
        Event& event = next_event(Kind::Datagram, delayUs);
        std::copy(bytes, bytes + length, event.bytes.begin());
        event.length = length;
    }
    void push_timeout() { next_event(Kind::Timeout, 0); }
    void push_receive_error() { next_event(Kind::ReceiveError, 0); }

    // The `index`-th call to send() (0-based) fails without transmitting.
    void fail_send(std::size_t index) { fail_send_index_ = index; }

    // --- Observations -------------------------------------------------------------
    std::size_t sent_count() const { return sent_count_; }
    const Datagram& sent(std::size_t index) const
    {
        RB_ASSERT(index < sent_count_);
        return sent_[index];
    }
    std::size_t pending_events() const { return size_ - next_; }
    unsigned receive_calls() const { return receive_calls_; }
    void clear_sent() { sent_count_ = 0; }

    // --- DatagramPort ---------------------------------------------------------------
    rb::core::Result<void> send(rb::core::Span<const uint8_t> datagram) override
    {
        const std::size_t attempt = send_attempts_++;
        if (attempt == fail_send_index_) {
            return rb::core::Failure{rb::core::ErrorCode::TransportError};
        }
        RB_ASSERT(sent_count_ < kMaxSent && datagram.size() <= kMaxBytes);
        Datagram& record = sent_[sent_count_++];
        std::copy(datagram.begin(), datagram.end(), record.bytes.begin());
        record.length = datagram.size();
        record.atUs = clock_.nowMicros();
        return {};
    }

    rb::core::Result<std::size_t> receive(rb::core::Span<uint8_t> out, uint32_t timeoutUs) override
    {
        ++receive_calls_;
        if (next_ == size_) {
            clock_.advance(timeoutUs);
            return rb::core::Failure{rb::core::ErrorCode::Timeout};
        }
        Event& event = events_[next_];
        if (event.delayUs > timeoutUs) {
            // Not yet: the wait expires first and the event stays queued.
            event.delayUs -= timeoutUs;
            clock_.advance(timeoutUs);
            return rb::core::Failure{rb::core::ErrorCode::Timeout};
        }
        ++next_;
        clock_.advance(event.delayUs);
        switch (event.kind) {
        case Kind::Timeout:
            clock_.advance(timeoutUs - event.delayUs);
            return rb::core::Failure{rb::core::ErrorCode::Timeout};
        case Kind::ReceiveError:
            return rb::core::Failure{rb::core::ErrorCode::TransportError};
        case Kind::Datagram:
        default:
            if (event.length > out.size()) {
                return rb::core::Failure{rb::core::ErrorCode::TransportError};
            }
            std::copy(event.bytes.begin(), event.bytes.begin() + event.length, out.begin());
            return event.length;
        }
    }

private:
    enum class Kind { Datagram, Timeout, ReceiveError };
    struct Event {
        Kind kind;
        std::array<uint8_t, kMaxBytes> bytes;
        std::size_t length;
        uint64_t delayUs;
    };

    Event& next_event(Kind kind, uint64_t delayUs)
    {
        RB_ASSERT(size_ < kMaxEvents);
        Event& event = events_[size_++];
        event.kind = kind;
        event.length = 0;
        event.delayUs = delayUs;
        return event;
    }

    FakeClock& clock_;
    std::array<Event, kMaxEvents> events_{};
    std::size_t size_ = 0;
    std::size_t next_ = 0;
    std::array<Datagram, kMaxSent> sent_{};
    std::size_t sent_count_ = 0;
    std::size_t send_attempts_ = 0;
    std::size_t fail_send_index_ = kNoFailure;
    unsigned receive_calls_ = 0;
};

}  // namespace rb::test
