// FskSymbolFeed (cassette-cas C107): the queue between the cassette loop (push, one producer) and
// the RMT refill interrupt (fill, one consumer). fill() packs two portions per RMT symbol; an odd
// last portion ends the symbol with duration1 = 0, which also ends the transmission (as the
// owner's own cassette_fsk_encode_cb, FujiNet PR #1756, under ADR-012 amendment 1).

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "rb/cassette/fsk_symbol_feed.hpp"
#include "unity.h"

using rb::cassette::FskPortion;
using rb::cassette::FskSymbol;
using rb::cassette::FskSymbolFeed;
using rb::core::Span;

void setUp() {}
void tearDown() {}

static FskPortion portion(bool level, uint32_t ticks)
{
    FskPortion p;
    p.level = level;
    p.ticks = ticks;
    return p;
}

static void test_pairs_portions_into_symbols(void)
{
    std::vector<uint32_t> slots(8);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    TEST_ASSERT_TRUE(feed.push(portion(false, 500)));
    TEST_ASSERT_TRUE(feed.push(portion(true, 250)));
    TEST_ASSERT_TRUE(feed.push(portion(false, 32767)));
    TEST_ASSERT_TRUE(feed.push(portion(true, 1)));
    TEST_ASSERT_EQUAL_UINT32(4, feed.size());
    FskSymbol out[4];
    bool done = false;
    TEST_ASSERT_EQUAL_size_t(2, feed.fill(Span<FskSymbol>(out, 4), done));
    TEST_ASSERT_FALSE(done);  // both pairs whole: the transmission may go on
    TEST_ASSERT_EQUAL_UINT16(500, out[0].duration0);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].level0);
    TEST_ASSERT_EQUAL_UINT16(250, out[0].duration1);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].level1);
    TEST_ASSERT_EQUAL_UINT16(32767, out[1].duration0);
    TEST_ASSERT_EQUAL_UINT16(1, out[1].duration1);
    TEST_ASSERT_TRUE(feed.empty());
    TEST_ASSERT_EQUAL_size_t(0, feed.fill(Span<FskSymbol>(out, 4), done));
    TEST_ASSERT_TRUE(done);  // the next call ends it
}

// Three portions: the call writes the whole pair and leaves the odd one; it is not cut yet.
static void test_odd_portion_waits_after_a_whole_pair(void)
{
    std::vector<uint32_t> slots(8);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    feed.push(portion(false, 300));
    feed.push(portion(true, 300));
    feed.push(portion(false, 400));
    FskSymbol out[4];
    bool done = false;
    TEST_ASSERT_EQUAL_size_t(1, feed.fill(Span<FskSymbol>(out, 4), done));
    TEST_ASSERT_FALSE(done);
    TEST_ASSERT_EQUAL_UINT32(1, feed.size());
    feed.push(portion(true, 500));  // the producer caught up
    TEST_ASSERT_EQUAL_size_t(1, feed.fill(Span<FskSymbol>(out, 4), done));
    TEST_ASSERT_EQUAL_UINT16(400, out[0].duration0);
    TEST_ASSERT_EQUAL_UINT16(500, out[0].duration1);
}

static void test_empty_feed_ends_the_transmission(void)
{
    std::vector<uint32_t> slots(4);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    FskSymbol out[2];
    bool done = false;
    TEST_ASSERT_EQUAL_size_t(0, feed.fill(Span<FskSymbol>(out, 2), done));
    TEST_ASSERT_TRUE(done);
}

static void test_odd_tail_ends_with_zero_duration(void)
{
    std::vector<uint32_t> slots(4);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    feed.push(portion(true, 700));
    FskSymbol out[2];
    bool done = false;
    TEST_ASSERT_EQUAL_size_t(1, feed.fill(Span<FskSymbol>(out, 2), done));
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_UINT16(700, out[0].duration0);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].level0);
    TEST_ASSERT_EQUAL_UINT16(0, out[0].duration1);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].level1);  // same level held: no spurious edge
}

static void test_fill_stops_at_the_space_given(void)
{
    std::vector<uint32_t> slots(8);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    for (uint32_t i = 0; i < 6; ++i) {
        feed.push(portion((i % 2) != 0, 100 + i));
    }
    FskSymbol out[1];
    bool done = false;
    TEST_ASSERT_EQUAL_size_t(1, feed.fill(Span<FskSymbol>(out, 1), done));
    TEST_ASSERT_FALSE(done);
    TEST_ASSERT_EQUAL_UINT32(4, feed.size());
    TEST_ASSERT_EQUAL_size_t(0, feed.fill(Span<FskSymbol>(), done));
    TEST_ASSERT_FALSE(done);  // no space is not the end
}

static void test_full_feed_refuses_and_wraps(void)
{
    std::vector<uint32_t> slots(4);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    TEST_ASSERT_EQUAL_UINT32(4, feed.capacity());
    for (uint32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(feed.push(portion(false, 100 + i)));
    }
    TEST_ASSERT_FALSE(feed.push(portion(false, 999)));
    FskSymbol out[1];
    bool done = false;
    feed.fill(Span<FskSymbol>(out, 1), done);
    TEST_ASSERT_TRUE(feed.push(portion(true, 200)));
    TEST_ASSERT_TRUE(feed.push(portion(false, 201)));
    FskSymbol rest[3];
    TEST_ASSERT_EQUAL_size_t(2, feed.fill(Span<FskSymbol>(rest, 3), done));
    TEST_ASSERT_EQUAL_UINT16(102, rest[0].duration0);
    TEST_ASSERT_EQUAL_UINT16(103, rest[0].duration1);
    TEST_ASSERT_EQUAL_UINT16(200, rest[1].duration0);
    TEST_ASSERT_EQUAL_UINT16(201, rest[1].duration1);
}

static void test_bad_portions_are_refused(void)
{
    std::vector<uint32_t> slots(4);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    TEST_ASSERT_FALSE(feed.push(portion(false, 0)));      // 0 would end the transmission
    TEST_ASSERT_FALSE(feed.push(portion(false, 32768)));  // does not fit an RMT half
    TEST_ASSERT_TRUE(feed.empty());
}

static void test_clear_drops_everything(void)
{
    std::vector<uint32_t> slots(4);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    feed.push(portion(false, 100));
    feed.push(portion(true, 100));
    feed.clear();
    TEST_ASSERT_TRUE(feed.empty());
    TEST_ASSERT_EQUAL_UINT32(0, feed.size());
}

// One producer, one consumer, as the cassette loop and the RMT interrupt: every portion comes out
// once, in order.
static void test_producer_and_consumer_threads(void)
{
    std::vector<uint32_t> slots(64);
    FskSymbolFeed feed(Span<uint32_t>(slots.data(), slots.size()));
    constexpr uint32_t kCount = 200000;  // even: whole pairs
    std::atomic<bool> producing{true};
    std::thread producer([&] {
        for (uint32_t i = 0; i < kCount;) {
            if (feed.push(portion((i % 2) != 0, 1 + (i % 30000)))) {
                ++i;
            }
        }
        producing = false;
    });
    uint32_t expected = 0;
    uint32_t wrong = 0;
    FskSymbol out[8];
    while (expected < kCount) {
        bool done = false;
        const std::size_t n = feed.fill(Span<FskSymbol>(out, 8), done);
        for (std::size_t k = 0; k < n; ++k) {
            const uint16_t halves[2] = {out[k].duration0, out[k].duration1};
            for (const uint16_t d : halves) {
                if (d == 0) {
                    continue;  // an odd tail while the producer was behind
                }
                wrong += d != 1 + (expected % 30000) ? 1u : 0u;
                ++expected;
            }
        }
    }
    producer.join();
    TEST_ASSERT_FALSE(producing.load());
    TEST_ASSERT_EQUAL_UINT32(0, wrong);
    TEST_ASSERT_EQUAL_UINT32(kCount, expected);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pairs_portions_into_symbols);
    RUN_TEST(test_odd_portion_waits_after_a_whole_pair);
    RUN_TEST(test_empty_feed_ends_the_transmission);
    RUN_TEST(test_odd_tail_ends_with_zero_duration);
    RUN_TEST(test_fill_stops_at_the_space_given);
    RUN_TEST(test_full_feed_refuses_and_wraps);
    RUN_TEST(test_bad_portions_are_refused);
    RUN_TEST(test_clear_drops_everything);
    RUN_TEST(test_producer_and_consumer_threads);
    return UNITY_END();
}
