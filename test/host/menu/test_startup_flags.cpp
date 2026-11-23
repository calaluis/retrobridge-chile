// StartupFlags (menu-cinta T6, review of PR #85): the SIO task never sees the drive-memory plan
// without the tape deck that was published before it, whatever happens between its two reads.

#include "rb/menu/startup_flags.hpp"
#include "unity.h"

using rb::host::StartupFlags;

void setUp(void) {}
void tearDown(void) {}

// Nothing published: nothing seen.
void test_nothing_is_seen_before_start_up_publishes()
{
    StartupFlags flags;
    const StartupFlags::Seen seen = flags.read();
    TEST_ASSERT_FALSE(seen.tape);
    TEST_ASSERT_FALSE(seen.plan);
}

// The review's order: finish() publishes the deck and then the plan exactly between the SIO task's
// two reads. The plan may be seen only together with the deck.
void test_both_published_between_the_reads_never_shows_the_plan_without_the_deck()
{
    StartupFlags flags;
    const StartupFlags::Seen seen = flags.read([&flags] {
        flags.publishTape();
        flags.publishPlan();
    });
    TEST_ASSERT_FALSE_MESSAGE(seen.plan && !seen.tape, "the restore would run without the deck");
}

// Only the deck so far: it is seen, the plan is not (the deck is applied, the restore waits).
void test_the_deck_alone_is_seen_and_the_plan_waits()
{
    StartupFlags flags;
    flags.publishTape();
    const StartupFlags::Seen seen = flags.read();
    TEST_ASSERT_TRUE(seen.tape);
    TEST_ASSERT_FALSE(seen.plan);
}

// No microSD: no deck is published, only the plan. The plan is seen and applied without a deck.
void test_a_plan_with_no_deck_is_seen_alone()
{
    StartupFlags flags;
    flags.publishPlan();
    const StartupFlags::Seen seen = flags.read();
    TEST_ASSERT_TRUE(seen.plan);
    TEST_ASSERT_FALSE(seen.tape);
}

// Both published before the read: both seen.
void test_both_published_are_both_seen()
{
    StartupFlags flags;
    flags.publishTape();
    flags.publishPlan();
    const StartupFlags::Seen seen = flags.read();
    TEST_ASSERT_TRUE(seen.tape);
    TEST_ASSERT_TRUE(seen.plan);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_seen_before_start_up_publishes);
    RUN_TEST(test_both_published_between_the_reads_never_shows_the_plan_without_the_deck);
    RUN_TEST(test_the_deck_alone_is_seen_and_the_plan_waits);
    RUN_TEST(test_a_plan_with_no_deck_is_seen_alone);
    RUN_TEST(test_both_published_are_both_seen);
    return UNITY_END();
}
