#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/sio_command_source.hpp"
#include "rb/core/assert.hpp"

namespace rb::sio {

enum class ListenOutcome : uint8_t {
    Frame,             // 5 bytes with a valid checksum
    ChecksumMismatch,  // 5 bytes, wrong checksum
    Incomplete,        // 0 to 4 bytes
    Overlong,          // more than 5 bytes (only the first 5 are kept)
    Timeout,           // no frame within the given time; counted, never recorded
    TransportError,    // receive failure (framing, overrun)
};

struct ListenRecord {
    uint32_t sequence;              // 1, 2, 3...; gaps show overwritten records (wraps after 2^32)
    ListenOutcome outcome;          // never Timeout
    uint8_t count;                  // bytes received; 0 for TransportError
    std::array<uint8_t, 5> bytes;   // the first 5 bytes, as received
    uint64_t atUs;                  // COMMAND release, or Clock::nowMicros() for TransportError
};

// Saturating counters: they stop at UINT32_MAX instead of wrapping.
struct ListenStats {
    uint32_t frames;
    uint32_t checksumMismatches;
    uint32_t incomplete;
    uint32_t overlong;
    uint32_t timeouts;
    uint32_t transportErrors;
    uint32_t overwritten;  // records lost because the ring was full
};

// Adds one to a counter, stopping at UINT32_MAX.
constexpr uint32_t saturating_increment(uint32_t value) noexcept
{
    return value == UINT32_MAX ? value : value + 1;
}

// Passive SIO listener (H5, second host-only deliverable; H5 design section 5).
// Its single reason to change is how received command frames are classified
// and recorded.
//
// Strictly passive by construction: it holds only the receive-only port
// SioCommandSource, so it cannot transmit (ACK, NAK, COMPLETE, data) or touch
// DATA IN. It shares CommandFrame with SioSession but none of the responding
// flow (dispatcher, mailbox).
//
// Deterministic and allocation-free: a fixed ring keeps the last kCapacity
// records; when full, the oldest is overwritten and counted. Timeouts are only
// counted, so an idle bus does not flush the ring. The wait time is the
// caller's: this class sets no bus timing. Not synchronised: one task only.
// Nothing uses it yet (no main, no driver).
class SioListener {
public:
    static constexpr std::size_t kCapacity = 32;

    SioListener(rb::app::SioCommandSource& source, const rb::app::Clock& clock) noexcept
        : source_(source), clock_(clock)
    {
    }

    // Waits for one frame and classifies it.
    ListenOutcome step(uint32_t timeoutUs) noexcept;

    const ListenStats& stats() const noexcept { return stats_; }
    void resetStats() noexcept { stats_ = ListenStats{}; }  // keeps the records
    void clearRecords() noexcept { size_ = 0; }              // keeps the statistics and the sequence

    std::size_t size() const noexcept { return size_; }
    // i = 0 is the oldest record kept. i >= size() is a programming error: it
    // aborts instead of silently returning a stale record (ADR-003).
    const ListenRecord& at(std::size_t i) const noexcept
    {
        RB_ASSERT(i < size_);
        return records_[(next_ + kCapacity - size_ + i) % kCapacity];
    }

private:
    void record(ListenOutcome outcome, uint8_t count, const std::array<uint8_t, 5>& bytes, uint64_t atUs) noexcept;

    rb::app::SioCommandSource& source_;
    const rb::app::Clock& clock_;
    std::array<ListenRecord, kCapacity> records_{};
    std::size_t next_ = 0;
    std::size_t size_ = 0;
    uint32_t sequence_ = 0;
    ListenStats stats_{};
};

}  // namespace rb::sio
