// SioPowerWatch (menu-unidades-persistentes T4, C10): with the FujiNet on USB, the +5 V of the SIO
// tells when the 65XE was switched off (D1 goes back to the menu then) and on again.

#include <cstdint>

#include "rb/menu/sio_power_watch.hpp"
#include "unity.h"

using rb::host::SioPowerWatch;
using rb::sio::StepOutcome;
using Event = rb::host::SioPowerWatch::Event;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr int kOn = 4800;
constexpr int kOff = 0;
constexpr uint64_t kStepUs = 50000;  // the SIO task samples every 50 ms

// Feeds `mv` from `from` for `durationUs`, one sample per step; returns the first event seen.
Event feed(SioPowerWatch& watch, int mv, uint64_t& now, uint64_t durationUs)
{
    Event first = Event::None;
    for (uint64_t end = now + durationUs; now < end; now += kStepUs) {
        const Event e = watch.sample(mv, now);
        if (first == Event::None) {
            first = e;
        }
    }
    return first;
}

}  // namespace

static void test_steady_power_gives_no_event(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    TEST_ASSERT_EQUAL(Event::None, feed(watch, kOn, now, 5000000));
}

static void test_power_below_1000_mv_for_100_ms_is_a_power_off(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    feed(watch, kOn, now, 1000000);
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOff, now));  // t = 0 of the low run
    now += kStepUs;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOff, now));  // 50 ms
    now += kStepUs;
    TEST_ASSERT_EQUAL(Event::PoweredOff, watch.sample(kOff, now));  // 100 ms
    now += kStepUs;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOff, now));  // reported once
}

static void test_a_dip_shorter_than_100_ms_is_ignored(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    feed(watch, kOn, now, 1000000);
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOff, now));
    now += 60000;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOn, now));  // back before 100 ms
    now += 60000;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOff, now));  // a new low run starts from zero
    now += 60000;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOff, now));
}

static void test_a_value_between_the_thresholds_breaks_a_low_run_without_an_event(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    feed(watch, kOn, now, 1000000);
    TEST_ASSERT_EQUAL(Event::None, feed(watch, kOff, now, 50000));
    TEST_ASSERT_EQUAL(Event::None, feed(watch, 2500, now, 500000));
    TEST_ASSERT_EQUAL(Event::None, feed(watch, kOff, now, 50000));
}

static void test_power_on_is_reported_only_after_a_power_off(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    feed(watch, kOn, now, 1000000);
    TEST_ASSERT_EQUAL(Event::PoweredOff, feed(watch, kOff, now, 2000000));
    TEST_ASSERT_EQUAL(Event::None, feed(watch, 2500, now, 100000));  // still rising
    TEST_ASSERT_EQUAL(Event::PoweredOn, watch.sample(4000, now));
    now += kStepUs;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(kOn, now));  // reported once
}

static void test_a_short_cycle_of_300_ms_is_detected(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    feed(watch, kOn, now, 1000000);
    TEST_ASSERT_EQUAL(Event::PoweredOff, feed(watch, kOff, now, 300000));
    TEST_ASSERT_EQUAL(Event::PoweredOn, feed(watch, kOn, now, 100000));
}

static void test_the_thresholds_are_exact(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    feed(watch, kOn, now, 1000000);
    TEST_ASSERT_EQUAL(Event::None, feed(watch, 1000, now, 1000000));  // 1000 mV is not low
    TEST_ASSERT_EQUAL(Event::None, watch.sample(999, now));
    now += 99999;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(999, now));  // 99.999 ms low
    now += 1;
    TEST_ASSERT_EQUAL(Event::PoweredOff, watch.sample(999, now));  // 100 ms
    now += kStepUs;
    TEST_ASSERT_EQUAL(Event::None, watch.sample(3999, now));  // not yet on
    now += kStepUs;
    TEST_ASSERT_EQUAL(Event::PoweredOn, watch.sample(4000, now));
}

static void test_starting_with_the_atari_off_reports_a_power_off(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    TEST_ASSERT_EQUAL(Event::PoweredOff, feed(watch, kOff, now, 500000));
    TEST_ASSERT_EQUAL(Event::PoweredOn, feed(watch, kOn, now, 100000));
}

static void test_the_5v_is_read_only_after_steps_that_sent_nothing(void)
{
    TEST_ASSERT_TRUE(rb::host::powerSampleAfter(StepOutcome::Idle));
    TEST_ASSERT_TRUE(rb::host::powerSampleAfter(StepOutcome::MailboxApplied));
    TEST_ASSERT_TRUE(rb::host::powerSampleAfter(StepOutcome::Ignored));
    TEST_ASSERT_TRUE(rb::host::powerSampleAfter(StepOutcome::TransportFailed));
    TEST_ASSERT_FALSE(rb::host::powerSampleAfter(StepOutcome::Nacked));
    TEST_ASSERT_FALSE(rb::host::powerSampleAfter(StepOutcome::Completed));
}

// Bench 2552703 (C10): the 65XE running a game is switched off with the FujiNet on USB. Its COMMAND line
// stays low, so every step of the SIO task ends in TransportFailed (stuck low) and the +5 V reads 835 mV.
static void test_an_atari_off_with_command_stuck_low_is_seen(void)
{
    SioPowerWatch watch;
    uint64_t now = 0;
    for (int i = 0; i < 4; ++i, now += kStepUs) {  // the game runs: frames answered
        if (rb::host::powerSampleAfter(StepOutcome::Completed)) {
            TEST_FAIL_MESSAGE("no read right after an answer");
        }
        TEST_ASSERT_EQUAL(Event::None, watch.sample(kOn, now));
    }
    Event seen = Event::None;
    for (uint64_t end = now + 500000; now < end && seen == Event::None; now += kStepUs) {
        if (rb::host::powerSampleAfter(StepOutcome::TransportFailed)) {  // what step() returns with COMMAND low
            seen = watch.sample(835, now);
        }
    }
    TEST_ASSERT_EQUAL(Event::PoweredOff, seen);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_steady_power_gives_no_event);
    RUN_TEST(test_power_below_1000_mv_for_100_ms_is_a_power_off);
    RUN_TEST(test_a_dip_shorter_than_100_ms_is_ignored);
    RUN_TEST(test_a_value_between_the_thresholds_breaks_a_low_run_without_an_event);
    RUN_TEST(test_power_on_is_reported_only_after_a_power_off);
    RUN_TEST(test_a_short_cycle_of_300_ms_is_detected);
    RUN_TEST(test_the_thresholds_are_exact);
    RUN_TEST(test_starting_with_the_atari_off_reports_a_power_off);
    RUN_TEST(test_the_5v_is_read_only_after_steps_that_sent_nothing);
    RUN_TEST(test_an_atari_off_with_command_stuck_low_is_seen);
    return UNITY_END();
}
