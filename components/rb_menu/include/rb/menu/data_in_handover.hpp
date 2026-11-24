#pragma once

// DataInHandover (spec menu-cinta, plan §2.4, T9a.1): the permission to drive DATA IN (GPIO21) passes
// between two tasks. The SIO task asks; the cassette task confirms on its own tick, once its hardware
// is free. Atomics only: no lock, no allocation, safe on two cores.
//
// The plan names one state (Disk | TakeRequested | Tape | ReleaseRequested | Fault | Released). It is
// kept as two words so neither task overwrites the other's last write:
//   - request: written only by the SIO task (Take or Release, with a new sequence each time);
//   - ack:     written only by the cassette task (Disk, Tape, Released or Refused, with the sequence it
//              answers).
// TakeRequested/ReleaseRequested = a request whose ack has not arrived; Fault is the arbiter's state
// (a release that expired); Released is an ack. A confirmation counts only for the sequence the SIO
// task is waiting for: an old or abandoned one changes nothing.

#include <atomic>
#include <cstdint>

#include "rb/menu/data_in_arbiter.hpp"

namespace rb::host {

enum class HandoverRequest : uint8_t { Release = 0, Take = 1 };

// Disk: GPIO21 is back on UART2 TX, verified, and the TX is empty. Tape: the cassette drives DATA IN.
// Released: the cassette could not give GPIO21 back to UART2, so it let the line go (no driver at
// all). Refused: the cassette did not act on the request.
enum class HandoverAck : uint8_t { Disk = 0, Tape = 1, Released = 2, Refused = 3 };

class DataInHandover {
public:
    static constexpr uint32_t kSeqMask = 0x3FFFFFFFu;  // 30 bits; the low 2 carry the kind

    struct Pending {
        HandoverRequest kind;
        uint32_t seq;
    };

    // SIO task. Publishes a new request and returns its sequence (never 0: 0 is the boot state).
    uint32_t request(HandoverRequest kind) noexcept;

    // SIO task. True, with the ack, once the cassette has answered `seq`.
    bool ackFor(uint32_t seq, HandoverAck& ack) const noexcept;

    // Cassette task. The latest request (at boot: Release with sequence 0, already confirmed Disk).
    Pending pending() const noexcept;

    // Cassette task. Answers the request with sequence `seq`.
    void confirm(uint32_t seq, HandoverAck ack) noexcept;

private:
    static constexpr uint32_t pack(uint32_t seq, uint32_t value) noexcept
    {
        return ((seq & kSeqMask) << 2) | (value & 3u);
    }

    std::atomic<uint32_t> request_{pack(0, static_cast<uint32_t>(HandoverRequest::Release))};
    std::atomic<uint32_t> ack_{pack(0, static_cast<uint32_t>(HandoverAck::Disk))};
    uint32_t lastSeq_ = 0;  // SIO task only
};

// Only aligned 32-bit loads and stores are used (no read-modify-write). On the host they are always
// lock-free. The ESP32 GCC (Xtensa) does not report them as always lock-free, so the check is host
// only; how the target compiles them (plain l32i/s32i with memw, or libatomic) is checked in T9a.7.
#ifndef ESP_PLATFORM
static_assert(std::atomic<uint32_t>::is_always_lock_free, "the handover must not take a lock");
#endif

// The SIO-side TapeLine over a DataInHandover. take()/release() publish a request the first time and
// poll its ack after that; asking for the other kind starts a new request (a new sequence), so an
// ack for the old one is ignored. abandon() stops waiting: a late ack for that request is ignored.
class HandoverTapeLine final : public TapeLine {
public:
    explicit HandoverTapeLine(DataInHandover& handover) noexcept : handover_(handover) {}

    LineResult take() noexcept override { return drive(HandoverRequest::Take); }
    LineResult release() noexcept override { return drive(HandoverRequest::Release); }
    void abandon() noexcept override { waiting_ = false; }

private:
    LineResult drive(HandoverRequest kind) noexcept;

    DataInHandover& handover_;
    HandoverRequest kind_ = HandoverRequest::Release;
    uint32_t seq_ = 0;
    bool waiting_ = false;
};

}  // namespace rb::host
