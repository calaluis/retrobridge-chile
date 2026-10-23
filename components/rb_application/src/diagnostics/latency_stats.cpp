#include "rb/app/diagnostics/latency_stats.hpp"

#include <algorithm>

namespace rb::app {

namespace {

// Nearest-rank percentile of `sorted` (n > 0): the value at rank ceil(p * n / 100).
uint32_t percentile(const uint32_t* sorted, std::size_t n, unsigned p) noexcept
{
    std::size_t rank = (p * n + 99) / 100;
    if (rank == 0) {
        rank = 1;
    }
    return sorted[rank - 1];
}

}  // namespace

void LatencyStats::add(uint32_t micros) noexcept
{
    min_ = std::min(min_, micros);
    max_ = std::max(max_, micros);
    if (count_ < storage_.size()) {
        storage_[count_++] = micros;
    } else {
        ++dropped_;
    }
}

void LatencyStats::reset() noexcept
{
    count_ = 0;
    dropped_ = 0;
    min_ = UINT32_MAX;
    max_ = 0;
}

LatencySummary LatencyStats::summarize() noexcept
{
    if (count_ == 0) {
        return LatencySummary{0, dropped_, 0, 0, 0, 0, 0, 0};
    }
    uint32_t* samples = storage_.data();
    std::sort(samples, samples + count_);
    uint64_t sum = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        sum += samples[i];
    }
    return LatencySummary{static_cast<uint32_t>(count_),
                          dropped_,
                          min_,
                          max_,
                          static_cast<uint32_t>(sum / count_),
                          percentile(samples, count_, 50),
                          percentile(samples, count_, 95),
                          percentile(samples, count_, 99)};
}

}  // namespace rb::app
