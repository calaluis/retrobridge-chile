// CommandLineTracker: classification of COMMAND lows from edge timestamps
// (H5, first host-only deliverable; docs/diseno/h5-escucha-solo-recepcion.md
// sections 3 and 6). Pure logic: these tests say nothing about the electrical
// behaviour of GPIO39, which belongs to the H4 measurements (M3-M5).
//
// The limits are test values, not the real thresholds (pending P6/M5). A
// 5-byte frame at 19200 baud lasts about 2.6 ms; t0 <= 1600 us and t1 <= 950 us
// (Atari C016555 §9), so a whole window is about 5.2 ms at most.

#include <type_traits>

#include "rb/sio/command_line_tracker.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::sio::CommandLineEvent;
using rb::sio::CommandLineEventKind;
using rb::sio::CommandLineLimits;
using rb::sio::CommandLineTracker;

namespace {

constexpr CommandLineLimits kLimits{100, 6000, 50000};  // min, max frame, stuck (test values)

CommandLineTracker make_tracker()
{
    auto created = CommandLineTracker::create(kLimits);
    TEST_ASSERT_TRUE(created.ok());
    return created.value();
}

// Value type with no owned resources: nothing to allocate or free (ADR-011).
static_assert(std::is_trivially_copyable<CommandLineTracker>::value, "CommandLineTracker must stay a plain value");

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_rejects_incoherent_limits(void)
{
    TEST_ASSERT_TRUE(CommandLineTracker::create(CommandLineLimits{0, 6000, 50000}).error() ==
                     ErrorCode::InvalidArgument);  // min must be positive
    TEST_ASSERT_TRUE(CommandLineTracker::create(CommandLineLimits{7000, 6000, 50000}).error() ==
                     ErrorCode::InvalidArgument);  // min > max
    TEST_ASSERT_TRUE(CommandLineTracker::create(CommandLineLimits{100, 6000, 6000}).error() ==
                     ErrorCode::InvalidArgument);  // max must be below stuck
    TEST_ASSERT_TRUE(CommandLineTracker::create(CommandLineLimits{100, 100, 101}).ok());  // tightest valid set
}

static void test_short_low_is_a_glitch(void)
{
    CommandLineTracker t = make_tracker();
    TEST_ASSERT_TRUE(t.onFall(1000).kind == CommandLineEventKind::None);
    const CommandLineEvent e = t.onRise(1099);  // 99 us < 100 us
    TEST_ASSERT_TRUE(e.kind == CommandLineEventKind::Glitch);
    TEST_ASSERT_EQUAL_UINT64(1000, e.fallUs);
    TEST_ASSERT_EQUAL_UINT64(1099, e.riseUs);
    TEST_ASSERT_FALSE(t.isLow());
}

static void test_normal_low_is_a_frame_window(void)
{
    CommandLineTracker t = make_tracker();
    t.onFall(10000);
    const CommandLineEvent e = t.onRise(10000 + 2605 + 1600);  // 5 bytes + t0 max
    TEST_ASSERT_TRUE(e.kind == CommandLineEventKind::FrameWindow);
    TEST_ASSERT_EQUAL_UINT64(10000, e.fallUs);
    TEST_ASSERT_EQUAL_UINT64(14205, e.riseUs);
}

static void test_window_limits_are_inclusive(void)
{
    CommandLineTracker t = make_tracker();
    t.onFall(0);
    TEST_ASSERT_TRUE(t.onRise(100).kind == CommandLineEventKind::FrameWindow);  // exactly minLowUs
    t.onFall(1000);
    TEST_ASSERT_TRUE(t.onRise(7000).kind == CommandLineEventKind::FrameWindow);  // exactly maxFrameLowUs
}

static void test_long_low_is_an_overlong_window(void)
{
    CommandLineTracker t = make_tracker();
    t.onFall(0);
    const CommandLineEvent e = t.onRise(6001);
    TEST_ASSERT_TRUE(e.kind == CommandLineEventKind::OverlongWindow);
    TEST_ASSERT_EQUAL_UINT64(0, e.fallUs);
    TEST_ASSERT_EQUAL_UINT64(6001, e.riseUs);
}

static void test_permanent_low_is_reported_once_then_recovers(void)
{
    CommandLineTracker t = make_tracker();
    t.onFall(1000);
    TEST_ASSERT_TRUE(t.poll(50999).kind == CommandLineEventKind::None);  // 49 999 us: not yet
    const CommandLineEvent stuck = t.poll(51000);                       // exactly stuckLowUs
    TEST_ASSERT_TRUE(stuck.kind == CommandLineEventKind::StuckLow);
    TEST_ASSERT_EQUAL_UINT64(1000, stuck.fallUs);
    TEST_ASSERT_TRUE(t.isStuck());
    TEST_ASSERT_TRUE(t.poll(90000).kind == CommandLineEventKind::None);  // only once

    const CommandLineEvent up = t.onRise(120000);
    TEST_ASSERT_TRUE(up.kind == CommandLineEventKind::Recovered);  // not a frame window
    TEST_ASSERT_EQUAL_UINT64(1000, up.fallUs);
    TEST_ASSERT_EQUAL_UINT64(120000, up.riseUs);
    TEST_ASSERT_FALSE(t.isLow());

    t.onFall(130000);  // normal operation resumes
    TEST_ASSERT_TRUE(t.onRise(133000).kind == CommandLineEventKind::FrameWindow);
}

static void test_poll_while_high_or_inside_a_window_reports_nothing(void)
{
    CommandLineTracker t = make_tracker();
    TEST_ASSERT_TRUE(t.poll(5000).kind == CommandLineEventKind::None);
    t.onFall(10000);
    TEST_ASSERT_TRUE(t.poll(12000).kind == CommandLineEventKind::None);
    TEST_ASSERT_TRUE(t.onRise(13000).kind == CommandLineEventKind::FrameWindow);
}

static void test_lost_edges_resync_without_a_window(void)
{
    CommandLineTracker t = make_tracker();
    const CommandLineEvent rise_first = t.onRise(500);  // no fall seen
    TEST_ASSERT_TRUE(rise_first.kind == CommandLineEventKind::Resync);
    TEST_ASSERT_FALSE(t.isLow());

    t.onFall(1000);
    const CommandLineEvent second_fall = t.onFall(2000);  // the rise between them was lost
    TEST_ASSERT_TRUE(second_fall.kind == CommandLineEventKind::Resync);
    TEST_ASSERT_TRUE(t.isLow());
    const CommandLineEvent e = t.onRise(4000);
    TEST_ASSERT_TRUE(e.kind == CommandLineEventKind::FrameWindow);
    TEST_ASSERT_EQUAL_UINT64(2000, e.fallUs);  // measured from the latest fall
}

static void test_fall_while_stuck_resyncs(void)
{
    CommandLineTracker t = make_tracker();
    t.onFall(0);
    TEST_ASSERT_TRUE(t.poll(60000).kind == CommandLineEventKind::StuckLow);
    TEST_ASSERT_TRUE(t.onFall(70000).kind == CommandLineEventKind::Resync);  // lost rise
    TEST_ASSERT_FALSE(t.isStuck());
    TEST_ASSERT_TRUE(t.onRise(72000).kind == CommandLineEventKind::FrameWindow);
}

static void test_time_going_back_is_rejected_without_losing_state(void)
{
    CommandLineTracker t = make_tracker();
    t.onFall(10000);
    TEST_ASSERT_TRUE(t.onRise(9000).kind == CommandLineEventKind::Rejected);
    TEST_ASSERT_TRUE(t.poll(9999).kind == CommandLineEventKind::Rejected);
    TEST_ASSERT_TRUE(t.onFall(5000).kind == CommandLineEventKind::Rejected);
    TEST_ASSERT_TRUE(t.isLow());
    const CommandLineEvent e = t.onRise(12000);
    TEST_ASSERT_TRUE(e.kind == CommandLineEventKind::FrameWindow);
    TEST_ASSERT_EQUAL_UINT64(10000, e.fallUs);
}

static void test_many_windows_keep_no_accumulated_state(void)
{
    CommandLineTracker t = make_tracker();
    uint64_t now = 0;
    for (int i = 0; i < 10000; ++i) {
        t.onFall(now);
        TEST_ASSERT_TRUE(t.onRise(now + 3000).kind == CommandLineEventKind::FrameWindow);
        now += 20000;
    }
    t.onFall(now);
    TEST_ASSERT_TRUE(t.onRise(now + 50).kind == CommandLineEventKind::Glitch);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rejects_incoherent_limits);
    RUN_TEST(test_short_low_is_a_glitch);
    RUN_TEST(test_normal_low_is_a_frame_window);
    RUN_TEST(test_window_limits_are_inclusive);
    RUN_TEST(test_long_low_is_an_overlong_window);
    RUN_TEST(test_permanent_low_is_reported_once_then_recovers);
    RUN_TEST(test_poll_while_high_or_inside_a_window_reports_nothing);
    RUN_TEST(test_lost_edges_resync_without_a_window);
    RUN_TEST(test_fall_while_stuck_resyncs);
    RUN_TEST(test_time_going_back_is_rejected_without_losing_state);
    RUN_TEST(test_many_windows_keep_no_accumulated_state);
    return UNITY_END();
}
