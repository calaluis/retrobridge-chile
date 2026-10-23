// LatencyStats: min / mean / max and nearest-rank percentiles over a caller-owned
// fixed buffer (no allocation).

#include <array>
#include <cstdint>

#include "rb/app/diagnostics/latency_stats.hpp"
#include "unity.h"

using rb::app::LatencyStats;
using rb::app::LatencySummary;
using rb::core::Span;

void setUp(void) {}
void tearDown(void) {}

static void test_empty_summary_is_all_zero(void)
{
    std::array<uint32_t, 4> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    const LatencySummary s = stats.summarize();
    TEST_ASSERT_EQUAL_UINT32(0, s.count);
    TEST_ASSERT_EQUAL_UINT32(0, s.min);
    TEST_ASSERT_EQUAL_UINT32(0, s.max);
    TEST_ASSERT_EQUAL_UINT32(0, s.p99);
}

static void test_single_sample(void)
{
    std::array<uint32_t, 4> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    stats.add(1234);
    const LatencySummary s = stats.summarize();
    TEST_ASSERT_EQUAL_UINT32(1, s.count);
    TEST_ASSERT_EQUAL_UINT32(1234, s.min);
    TEST_ASSERT_EQUAL_UINT32(1234, s.mean);
    TEST_ASSERT_EQUAL_UINT32(1234, s.p50);
    TEST_ASSERT_EQUAL_UINT32(1234, s.max);
}

static void test_nearest_rank_percentiles_on_unsorted_input(void)
{
    std::array<uint32_t, 100> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    for (uint32_t i = 0; i < 100; ++i) {
        stats.add(((i * 37) % 100) + 1);  // 1..100 in scrambled order
    }
    const LatencySummary s = stats.summarize();
    TEST_ASSERT_EQUAL_UINT32(100, s.count);
    TEST_ASSERT_EQUAL_UINT32(1, s.min);
    TEST_ASSERT_EQUAL_UINT32(100, s.max);
    TEST_ASSERT_EQUAL_UINT32(50, s.mean);  // 5050 / 100, rounded down
    TEST_ASSERT_EQUAL_UINT32(50, s.p50);
    TEST_ASSERT_EQUAL_UINT32(95, s.p95);
    TEST_ASSERT_EQUAL_UINT32(99, s.p99);
}

static void test_small_sample_percentiles(void)
{
    std::array<uint32_t, 8> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    stats.add(30);
    stats.add(10);
    stats.add(20);
    const LatencySummary s = stats.summarize();
    TEST_ASSERT_EQUAL_UINT32(20, s.p50);  // rank ceil(0.50 * 3) = 2
    TEST_ASSERT_EQUAL_UINT32(30, s.p95);  // rank ceil(0.95 * 3) = 3
    TEST_ASSERT_EQUAL_UINT32(30, s.p99);
}

static void test_samples_beyond_capacity_are_counted_as_dropped(void)
{
    std::array<uint32_t, 3> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    for (uint32_t i = 1; i <= 5; ++i) {
        stats.add(i * 10);
    }
    const LatencySummary s = stats.summarize();
    TEST_ASSERT_EQUAL_UINT32(3, s.count);
    TEST_ASSERT_EQUAL_UINT32(2, s.dropped);
    TEST_ASSERT_EQUAL_UINT32(50, s.max);  // the maximum still covers every sample
}

static void test_reset(void)
{
    std::array<uint32_t, 3> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    stats.add(5);
    stats.reset();
    TEST_ASSERT_EQUAL_UINT32(0, stats.summarize().count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_summary_is_all_zero);
    RUN_TEST(test_single_sample);
    RUN_TEST(test_nearest_rank_percentiles_on_unsorted_input);
    RUN_TEST(test_small_sample_percentiles);
    RUN_TEST(test_samples_beyond_capacity_are_counted_as_dropped);
    RUN_TEST(test_reset);
    return UNITY_END();
}
