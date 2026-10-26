// CommandFrameAssembler: tying received bytes to COMMAND windows (H5, third
// host-only deliverable; docs/diseno/h5-escucha-solo-recepcion.md section 9,
// decisions D1-D4). Pure logic: these tests say nothing about UART latency,
// interrupt latency or the electrical side of the bus (H4 M3-M5), and they do
// not choose the physical capture mechanism of D1.
//
// The limits are test values, not the real thresholds (pending P6/M5).

#include <array>
#include <cstdint>

#include "rb/core/span.hpp"
#include "rb/sio/command_frame_assembler.hpp"
#include "rb/sio/saturating.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Span;
using rb::sio::AssemblyOutcome;
using rb::sio::AssemblyResult;
using rb::sio::CommandFrameAssembler;
using rb::sio::CommandLineLimits;
using rb::sio::DropReason;

namespace {

constexpr CommandLineLimits kLimits{100, 6000, 50000};  // min, max frame, stuck (test values)

CommandFrameAssembler make_assembler()
{
    auto created = CommandFrameAssembler::create(kLimits);
    TEST_ASSERT_TRUE(created.ok());
    return created.value();
}

// Five bytes with a correct checksum: invalid windows must be dropped even then (D4).
std::array<uint8_t, 5> valid_frame(uint8_t aux1)
{
    std::array<uint8_t, 5> bytes{0x31, 0x52, aux1, 0, 0};
    bytes[4] = rb::sio::sio_checksum(Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

void feed(CommandFrameAssembler& a, const std::array<uint8_t, 5>& bytes, std::size_t from = 0, std::size_t to = 5)
{
    a.onBytes(Span<const uint8_t>{bytes.data() + from, to - from});
}

void assert_dropped(const AssemblyResult& r, DropReason reason)
{
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Dropped);
    TEST_ASSERT_TRUE(r.reason == reason);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// AssemblerStats counters go through these helpers (shared with SioListener).
static void test_saturating_helpers_stop_at_the_limit(void)
{
    static_assert(rb::sio::saturating_increment(UINT32_MAX) == UINT32_MAX, "increment must not wrap");
    static_assert(rb::sio::saturating_add(UINT32_MAX - 1, 5) == UINT32_MAX, "add must not wrap");
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rb::sio::saturating_increment(UINT32_MAX - 1));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rb::sio::saturating_increment(UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(7, rb::sio::saturating_add(2, 5));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rb::sio::saturating_add(UINT32_MAX - 5, 5));  // exactly reaches it
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rb::sio::saturating_add(UINT32_MAX - 5, 6));  // would wrap
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rb::sio::saturating_add(UINT32_MAX, 1));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rb::sio::saturating_add(0, static_cast<std::size_t>(UINT32_MAX) + 1));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 1, rb::sio::saturating_add(UINT32_MAX - 1, 0));
}

static void test_rejects_incoherent_limits(void)
{
    TEST_ASSERT_TRUE(CommandFrameAssembler::create(CommandLineLimits{0, 6000, 50000}).error() ==
                     ErrorCode::InvalidArgument);
    TEST_ASSERT_TRUE(CommandFrameAssembler::create(CommandLineLimits{100, 6000, 6000}).error() ==
                     ErrorCode::InvalidArgument);
}

static void test_valid_window_delivers_bytes_and_both_edges(void)
{
    CommandFrameAssembler a = make_assembler();
    const auto bytes = valid_frame(1);
    a.onFall(1000);
    feed(a, bytes);
    a.onRise(5000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8(5, r.frame.count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), r.frame.bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT64(1000, r.frame.commandAssertedAtUs);
    TEST_ASSERT_EQUAL_UINT64(5000, r.frame.commandReleasedAtUs);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().frames);
}

static void test_late_bytes_before_complete_belong_to_the_window(void)
{
    CommandFrameAssembler a = make_assembler();
    const auto bytes = valid_frame(2);
    a.onFall(0);
    feed(a, bytes, 0, 3);
    a.onRise(4000);
    feed(a, bytes, 3, 5);  // delivered after COMMAND rose (D1: capture latency)
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8(5, r.frame.count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), r.frame.bytes.data(), 5);
}

static void test_bytes_outside_a_window_are_never_reused(void)
{
    CommandFrameAssembler a = make_assembler();
    const auto stray = valid_frame(0xEE);
    feed(a, stray, 0, 2);  // before any fall
    TEST_ASSERT_EQUAL_UINT32(2, a.stats().strayBytes);

    a.onFall(1000);
    feed(a, valid_frame(3));
    a.onRise(4000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Frame);
    feed(a, stray, 0, 3);  // after complete()
    TEST_ASSERT_EQUAL_UINT32(5, a.stats().strayBytes);

    const auto next = valid_frame(4);
    a.onFall(20000);
    feed(a, next);
    a.onRise(23000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8(5, r.frame.count);  // only this window's bytes
    TEST_ASSERT_EQUAL_UINT8_ARRAY(next.data(), r.frame.bytes.data(), 5);
}

static void test_glitch_window_is_dropped_even_with_valid_bytes(void)
{
    CommandFrameAssembler a = make_assembler();
    a.onFall(1000);
    feed(a, valid_frame(5));
    a.onRise(1050);
    assert_dropped(a.complete(), DropReason::Glitch);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().glitches);
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().frames);
}

static void test_overlong_window_is_dropped_even_with_valid_bytes(void)
{
    CommandFrameAssembler a = make_assembler();
    a.onFall(0);
    feed(a, valid_frame(6));
    a.onRise(6001);  // longer than maxFrameLowUs: a COMMAND problem, not a byte count (D4)
    assert_dropped(a.complete(), DropReason::OverlongWindow);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().overlongWindows);
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().frames);
}

static void test_more_than_five_bytes_in_a_valid_window_are_delivered_for_the_listener(void)
{
    CommandFrameAssembler a = make_assembler();
    const auto bytes = valid_frame(7);
    a.onFall(0);
    feed(a, bytes);
    feed(a, bytes, 0, 2);
    a.onRise(5000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);  // SioListener classifies it as Overlong
    TEST_ASSERT_EQUAL_UINT8(7, r.frame.count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), r.frame.bytes.data(), 5);  // first 5 only
}

static void test_stuck_window_is_dropped(void)
{
    CommandFrameAssembler a = make_assembler();
    a.onFall(0);
    feed(a, valid_frame(8));
    a.poll(50000);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().stuckLows);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::NothingToDeliver);  // still low
    a.onRise(80000);
    assert_dropped(a.complete(), DropReason::StuckLow);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().recoveries);
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().frames);
}

static void test_lost_rise_drops_the_old_window_and_starts_clean(void)
{
    CommandFrameAssembler a = make_assembler();
    a.onFall(0);
    feed(a, valid_frame(9));
    a.onFall(2000);  // the rise between them was lost
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().resyncs);
    const auto next = valid_frame(10);
    feed(a, next);
    a.onRise(5000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8(5, r.frame.count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(next.data(), r.frame.bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT64(2000, r.frame.commandAssertedAtUs);
}

static void test_rise_without_fall_delivers_nothing(void)
{
    CommandFrameAssembler a = make_assembler();
    feed(a, valid_frame(11));  // stray
    a.onRise(3000);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().resyncs);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::NothingToDeliver);
}

static void test_new_fall_before_complete_supersedes_the_window(void)
{
    CommandFrameAssembler a = make_assembler();
    a.onFall(0);
    feed(a, valid_frame(12));
    a.onRise(4000);
    a.onFall(20000);  // complete() never came for the previous window
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().superseded);
    a.onRise(24000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8(0, r.frame.count);  // nothing inherited from the dropped window
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().frames);
}

static void test_late_bytes_of_a_superseded_window_never_reach_the_next_one(void)
{
    CommandFrameAssembler a = make_assembler();
    const auto frame_a = valid_frame(0xA0);
    const auto frame_b = valid_frame(0xB0);
    a.onFall(0);              // window A
    feed(a, frame_a, 0, 3);
    a.onRise(4000);
    feed(a, frame_a, 3, 5);   // late bytes of A
    a.onFall(20000);          // window B, before complete(A)
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().superseded);
    feed(a, frame_b, 0, 2);
    a.onRise(24000);
    feed(a, frame_b, 2, 5);   // late bytes of B
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8(5, r.frame.count);  // A's 5 bytes are gone entirely
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame_b.data(), r.frame.bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT64(20000, r.frame.commandAssertedAtUs);
    TEST_ASSERT_EQUAL_UINT64(24000, r.frame.commandReleasedAtUs);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().frames);  // A was never delivered
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().strayBytes);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::NothingToDeliver);
}

static void test_uart_error_in_a_window_is_a_receive_error(void)
{
    CommandFrameAssembler a = make_assembler();
    a.onReceiveError();  // outside a window: counted only
    a.onFall(0);
    feed(a, valid_frame(13));
    a.onReceiveError();
    a.onRise(4000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::ReceiveError);
    TEST_ASSERT_EQUAL_UINT32(2, a.stats().receiveErrors);
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().frames);

    a.onFall(20000);  // the error does not leak into the next window
    feed(a, valid_frame(14));
    a.onRise(24000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Frame);
}

static void test_queue_overflow_drops_the_window(void)
{
    CommandFrameAssembler a = make_assembler();
    a.onQueueOverflow();  // while idle: counted only
    a.onFall(0);
    feed(a, valid_frame(15));
    a.onQueueOverflow();
    a.onRise(4000);
    assert_dropped(a.complete(), DropReason::QueueOverflow);
    TEST_ASSERT_EQUAL_UINT32(2, a.stats().queueOverflows);

    a.onFall(20000);
    feed(a, valid_frame(16));
    a.onRise(24000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Frame);
}

static void test_complete_needs_a_closed_window(void)
{
    CommandFrameAssembler a = make_assembler();
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::NothingToDeliver);  // idle
    a.onFall(0);
    feed(a, valid_frame(17));
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::NothingToDeliver);  // still open
    a.onRise(4000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Frame);              // kept its bytes
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::NothingToDeliver);  // delivered once
}

static void test_byte_count_saturates_and_keeps_the_first_five(void)
{
    CommandFrameAssembler a = make_assembler();
    const auto first = valid_frame(18);
    std::array<uint8_t, 100> filler{};
    filler.fill(0xAA);
    a.onFall(0);
    feed(a, first);
    for (int i = 0; i < 3; ++i) {
        a.onBytes(Span<const uint8_t>{filler});
    }
    a.onRise(5000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8(255, r.frame.count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first.data(), r.frame.bytes.data(), 5);
}

static void test_out_of_order_times_are_rejected_without_losing_the_window(void)
{
    CommandFrameAssembler a = make_assembler();
    const auto bytes = valid_frame(19);
    a.onFall(10000);
    feed(a, bytes);
    a.onRise(9000);   // earlier than the fall
    a.poll(9500);
    a.onFall(5000);
    TEST_ASSERT_EQUAL_UINT32(3, a.stats().rejectedTimestamps);
    a.onRise(13000);
    const AssemblyResult r = a.complete();
    TEST_ASSERT_TRUE(r.outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT64(10000, r.frame.commandAssertedAtUs);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), r.frame.bytes.data(), 5);
}

static void test_reset_stats_keeps_the_window(void)
{
    CommandFrameAssembler a = make_assembler();
    feed(a, valid_frame(20));  // stray
    a.onFall(0);
    feed(a, valid_frame(21));
    a.resetStats();
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().strayBytes);
    a.onRise(4000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().frames);
}

static void test_same_events_give_the_same_results(void)
{
    CommandFrameAssembler x = make_assembler();
    CommandFrameAssembler y = make_assembler();
    for (CommandFrameAssembler* a : {&x, &y}) {
        a->onFall(0);
        feed(*a, valid_frame(22), 0, 4);
        a->onRise(4000);
        feed(*a, valid_frame(22), 4, 5);
    }
    const AssemblyResult rx = x.complete();
    const AssemblyResult ry = y.complete();
    TEST_ASSERT_TRUE(rx.outcome == ry.outcome);
    TEST_ASSERT_EQUAL_UINT8(rx.frame.count, ry.frame.count);
    TEST_ASSERT_EQUAL_UINT64(rx.frame.commandAssertedAtUs, ry.frame.commandAssertedAtUs);
    TEST_ASSERT_EQUAL_UINT64(rx.frame.commandReleasedAtUs, ry.frame.commandReleasedAtUs);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx.frame.bytes.data(), ry.frame.bytes.data(), 5);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_saturating_helpers_stop_at_the_limit);
    RUN_TEST(test_rejects_incoherent_limits);
    RUN_TEST(test_valid_window_delivers_bytes_and_both_edges);
    RUN_TEST(test_late_bytes_before_complete_belong_to_the_window);
    RUN_TEST(test_bytes_outside_a_window_are_never_reused);
    RUN_TEST(test_glitch_window_is_dropped_even_with_valid_bytes);
    RUN_TEST(test_overlong_window_is_dropped_even_with_valid_bytes);
    RUN_TEST(test_more_than_five_bytes_in_a_valid_window_are_delivered_for_the_listener);
    RUN_TEST(test_stuck_window_is_dropped);
    RUN_TEST(test_lost_rise_drops_the_old_window_and_starts_clean);
    RUN_TEST(test_rise_without_fall_delivers_nothing);
    RUN_TEST(test_new_fall_before_complete_supersedes_the_window);
    RUN_TEST(test_late_bytes_of_a_superseded_window_never_reach_the_next_one);
    RUN_TEST(test_uart_error_in_a_window_is_a_receive_error);
    RUN_TEST(test_queue_overflow_drops_the_window);
    RUN_TEST(test_complete_needs_a_closed_window);
    RUN_TEST(test_byte_count_saturates_and_keeps_the_first_five);
    RUN_TEST(test_out_of_order_times_are_rejected_without_losing_the_window);
    RUN_TEST(test_reset_stats_keeps_the_window);
    RUN_TEST(test_same_events_give_the_same_results);
    return UNITY_END();
}
