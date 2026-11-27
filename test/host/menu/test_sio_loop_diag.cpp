// fix-cinta-data-in-65xe: "EL HOST NO RESPONDE" with the menu (PR #124). The SIO loop says on the
// console which steps failed while the 65XE was on, and which phase of the loop was slow. Pure logic.
#include "sio_loop_diag.hpp"

#include "unity.h"

using rb::menusio::LoopPhase;
using rb::menusio::SioLoopDiag;
using rb::menusio::StepKind;

void setUp(void) {}
void tearDown(void) {}

static void test_a_failed_step_with_the_atari_on_is_reported(void)
{
    SioLoopDiag diag;
    diag.poweredOn();
    TEST_ASSERT_TRUE(diag.noteStep(StepKind::Ignored, true));
    TEST_ASSERT_TRUE(diag.noteStep(StepKind::Nacked, true));
    TEST_ASSERT_TRUE(diag.noteStep(StepKind::TransportFailed, true));
    TEST_ASSERT_EQUAL_UINT32(1, diag.count(StepKind::Ignored));
    TEST_ASSERT_EQUAL_UINT32(1, diag.count(StepKind::Nacked));
    TEST_ASSERT_EQUAL_UINT32(1, diag.count(StepKind::TransportFailed));
}

static void test_a_good_or_idle_step_is_not_reported(void)
{
    SioLoopDiag diag;
    diag.poweredOn();
    TEST_ASSERT_FALSE(diag.noteStep(StepKind::Idle, true));
    TEST_ASSERT_FALSE(diag.noteStep(StepKind::Completed, true));
    TEST_ASSERT_FALSE(diag.noteStep(StepKind::Other, true));
}

// With the 65XE off every step ends in TransportFailed (COMMAND low): that is not a fault.
static void test_a_failed_step_with_the_atari_off_is_not_reported(void)
{
    SioLoopDiag diag;
    TEST_ASSERT_FALSE(diag.noteStep(StepKind::TransportFailed, false));
    TEST_ASSERT_EQUAL_UINT32(0, diag.count(StepKind::TransportFailed));
}

// The console must stay readable: at most kMaxLines reports per power-on; the counts go on.
static void test_reports_stop_at_the_limit_and_start_again_at_power_on(void)
{
    SioLoopDiag diag;
    diag.poweredOn();
    for (uint32_t i = 0; i < SioLoopDiag::kMaxLines; ++i) {
        TEST_ASSERT_TRUE(diag.noteStep(StepKind::Ignored, true));
    }
    TEST_ASSERT_FALSE(diag.noteStep(StepKind::Ignored, true));
    TEST_ASSERT_EQUAL_UINT32(SioLoopDiag::kMaxLines + 1, diag.count(StepKind::Ignored));
    diag.poweredOn();
    TEST_ASSERT_TRUE(diag.noteStep(StepKind::Ignored, true));
    TEST_ASSERT_EQUAL_UINT32(1, diag.count(StepKind::Ignored));
}

// A phase is slow above kSlowUs: the Atari's command retries last a few hundred milliseconds.
static void test_a_phase_is_slow_only_above_the_limit(void)
{
    SioLoopDiag diag;
    diag.poweredOn();
    TEST_ASSERT_FALSE(diag.notePhase(LoopPhase::Step, SioLoopDiag::kSlowUs));
    TEST_ASSERT_TRUE(diag.notePhase(LoopPhase::Step, SioLoopDiag::kSlowUs + 1));
    TEST_ASSERT_TRUE(diag.notePhase(LoopPhase::DriveMemory, 154000));
}

// Slow phases share the line budget with failed steps.
static void test_slow_phases_share_the_line_limit(void)
{
    SioLoopDiag diag;
    diag.poweredOn();
    for (uint32_t i = 0; i < SioLoopDiag::kMaxLines; ++i) {
        TEST_ASSERT_TRUE(diag.notePhase(LoopPhase::Pump, SioLoopDiag::kSlowUs + 1));
    }
    TEST_ASSERT_FALSE(diag.notePhase(LoopPhase::Pump, SioLoopDiag::kSlowUs + 1));
    TEST_ASSERT_FALSE(diag.noteStep(StepKind::Nacked, true));
}

static void test_phase_names_are_stable(void)
{
    TEST_ASSERT_EQUAL_STRING("step", SioLoopDiag::name(LoopPhase::Step));
    TEST_ASSERT_EQUAL_STRING("drive-memory", SioLoopDiag::name(LoopPhase::DriveMemory));
    TEST_ASSERT_EQUAL_STRING("pump", SioLoopDiag::name(LoopPhase::Pump));
    TEST_ASSERT_EQUAL_STRING("data-in", SioLoopDiag::name(LoopPhase::DataIn));
    TEST_ASSERT_EQUAL_STRING("ignored", SioLoopDiag::name(StepKind::Ignored));
    TEST_ASSERT_EQUAL_STRING("nak", SioLoopDiag::name(StepKind::Nacked));
    TEST_ASSERT_EQUAL_STRING("transport", SioLoopDiag::name(StepKind::TransportFailed));
}

// Review PR #124 (M2): no slow line does not mean a healthy bus. The slowest run of each phase is
// kept, also under the limit and after the line budget, for the summary at power-off.
static void test_the_slowest_run_of_each_phase_is_kept_under_the_limit_and_past_the_budget(void)
{
    SioLoopDiag diag;
    diag.poweredOn();
    TEST_ASSERT_EQUAL_INT64(0, diag.slowest(LoopPhase::Step));
    diag.notePhase(LoopPhase::Step, 12000);
    diag.notePhase(LoopPhase::Step, 30000);
    diag.notePhase(LoopPhase::Step, 20000);
    TEST_ASSERT_EQUAL_INT64(30000, diag.slowest(LoopPhase::Step));
    TEST_ASSERT_EQUAL_INT64(0, diag.slowest(LoopPhase::Pump));
    for (uint32_t i = 0; i < SioLoopDiag::kMaxLines; ++i) {
        diag.notePhase(LoopPhase::Pump, SioLoopDiag::kSlowUs + 1);
    }
    TEST_ASSERT_FALSE(diag.notePhase(LoopPhase::DriveMemory, 154000));
    TEST_ASSERT_EQUAL_INT64(154000, diag.slowest(LoopPhase::DriveMemory));
    TEST_ASSERT_EQUAL_UINT32(SioLoopDiag::kMaxLines, diag.slowLines());
    diag.poweredOn();
    TEST_ASSERT_EQUAL_INT64(0, diag.slowest(LoopPhase::DriveMemory));
    TEST_ASSERT_EQUAL_UINT32(0, diag.slowLines());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_step_with_the_atari_on_is_reported);
    RUN_TEST(test_a_good_or_idle_step_is_not_reported);
    RUN_TEST(test_a_failed_step_with_the_atari_off_is_not_reported);
    RUN_TEST(test_reports_stop_at_the_limit_and_start_again_at_power_on);
    RUN_TEST(test_a_phase_is_slow_only_above_the_limit);
    RUN_TEST(test_slow_phases_share_the_line_limit);
    RUN_TEST(test_phase_names_are_stable);
    RUN_TEST(test_the_slowest_run_of_each_phase_is_kept_under_the_limit_and_past_the_budget);
    return UNITY_END();
}
