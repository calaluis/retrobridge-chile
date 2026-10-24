#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/hwdiag/diag_types.hpp"

namespace rb::hwdiag {

struct EventRecord {
    uint32_t sequence;  // 1, 2, 3... since boot; gaps show overwritten records
    uint64_t timeUs;
    DiagEvent event;
    uint32_t value;
};

// Fixed-capacity ring of the last N events: no allocation, constant memory
// (ADR-011). When full, the oldest record is overwritten and counted as lost.
// Not synchronised: used from one task (the storage context, ADR-014).
template <std::size_t N>
class EventLog {
    static_assert(N > 0, "EventLog needs room for at least one record");

public:
    static constexpr std::size_t kCapacity = N;

    void record(uint64_t timeUs, DiagEvent event, uint32_t value) noexcept
    {
        records_[next_] = EventRecord{++sequence_, timeUs, event, value};
        next_ = (next_ + 1) % N;
        if (size_ < N) {
            ++size_;
        } else {
            ++overwritten_;
        }
    }

    std::size_t size() const noexcept { return size_; }
    uint32_t overwritten() const noexcept { return overwritten_; }
    uint32_t total() const noexcept { return sequence_; }

    // i = 0 is the oldest record kept.
    const EventRecord& at(std::size_t i) const noexcept { return records_[(next_ + N - size_ + i) % N]; }

private:
    std::array<EventRecord, N> records_{};
    std::size_t next_ = 0;
    std::size_t size_ = 0;
    uint32_t sequence_ = 0;
    uint32_t overwritten_ = 0;
};

}  // namespace rb::hwdiag
