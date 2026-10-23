#pragma once

#include <cstddef>
#include <cstdint>

#include "rb/core/span.hpp"

namespace rb::app {

struct LatencySummary {
    uint32_t count;    // samples kept
    uint32_t dropped;  // samples beyond the storage capacity (still counted in min/max)
    uint32_t min;
    uint32_t max;
    uint32_t mean;     // of the kept samples, rounded down
    uint32_t p50;
    uint32_t p95;
    uint32_t p99;
};

// Latency samples (microseconds) over caller-owned fixed storage: no allocation
// (H3-12). Percentiles use the nearest-rank method on the kept samples.
class LatencyStats {
public:
    explicit LatencyStats(rb::core::Span<uint32_t> storage) noexcept : storage_(storage) {}

    void add(uint32_t micros) noexcept;
    void reset() noexcept;

    // Sorts the kept samples in place.
    LatencySummary summarize() noexcept;

private:
    rb::core::Span<uint32_t> storage_;
    std::size_t count_ = 0;
    uint32_t dropped_ = 0;
    uint32_t min_ = UINT32_MAX;
    uint32_t max_ = 0;
};

}  // namespace rb::app
