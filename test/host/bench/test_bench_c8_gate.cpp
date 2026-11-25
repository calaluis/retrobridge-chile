// Bench C8a, phase F1 (docs/hardware/banco-c8a-aislado.md): the SIO +5 V interlock (C-1) and the one-shot console
// order (C-2). The bench takes GPIO21 only when every GPIO35 reading is valid and says no power from the Atari.
#include <unity.h>

#include "bench_c8_gate.hpp"

using rb::bench::C8Trigger;
using rb::bench::classifySio5v;
using rb::bench::kSio5vSamples;
using rb::bench::Sio5v;
using rb::bench::Sio5vSample;

void setUp(void) {}
void tearDown(void) {}

namespace {

Sio5vSample g_samples[kSio5vSamples];

const Sio5vSample* all(int pinMv)
{
    for (auto& s : g_samples) {
        s = Sio5vSample{true, pinMv};
    }
    return g_samples;
}

Sio5v classify(int pinMv) { return classifySio5v(all(pinMv), kSio5vSamples); }

}  // namespace

// --- C-1: the reading ---

static void test_no_power_from_the_atari_is_absent(void)
{
    TEST_ASSERT_TRUE(classify(0) == Sio5v::Absent);
    TEST_ASSERT_TRUE(classify(499) == Sio5v::Absent);
}

static void test_the_atari_powered_is_present(void)
{
    TEST_ASSERT_TRUE(classify(500) == Sio5v::Present);
    TEST_ASSERT_TRUE(classify(3170) == Sio5v::Present);  // about 4.8 V on pin 10
}

static void test_one_failed_read_is_invalid(void)
{
    all(0);
    g_samples[3].ok = false;
    TEST_ASSERT_TRUE(classifySio5v(g_samples, kSio5vSamples) == Sio5v::Invalid);
}

static void test_too_few_samples_are_invalid(void)
{
    TEST_ASSERT_TRUE(classifySio5v(all(0), kSio5vSamples - 1) == Sio5v::Invalid);
    TEST_ASSERT_TRUE(classifySio5v(all(0), 0) == Sio5v::Invalid);
    TEST_ASSERT_TRUE(classifySio5v(nullptr, kSio5vSamples) == Sio5v::Invalid);
}

static void test_a_value_out_of_range_is_invalid(void)
{
    all(0);
    g_samples[0].pinMv = -1;
    TEST_ASSERT_TRUE(classifySio5v(g_samples, kSio5vSamples) == Sio5v::Invalid);
    all(3000);
    g_samples[7].pinMv = 3301;
    TEST_ASSERT_TRUE(classifySio5v(g_samples, kSio5vSamples) == Sio5v::Invalid);
}

static void test_unstable_readings_are_invalid_even_below_the_threshold(void)
{
    all(0);
    g_samples[5].pinMv = 450;  // every sample below 500, but 450 mV apart: not a steady line
    TEST_ASSERT_TRUE(classifySio5v(g_samples, kSio5vSamples) == Sio5v::Invalid);
    all(100);
    g_samples[5].pinMv = 300;  // 200 mV apart: still steady
    TEST_ASSERT_TRUE(classifySio5v(g_samples, kSio5vSamples) == Sio5v::Absent);
}

static void test_one_sample_at_the_threshold_is_present(void)
{
    all(0);
    g_samples[2].pinMv = 150;
    g_samples[6].pinMv = 500 - 1;
    TEST_ASSERT_TRUE(classifySio5v(g_samples, kSio5vSamples) == Sio5v::Invalid);  // 499 mV spread
    all(400);
    g_samples[6].pinMv = 500;
    TEST_ASSERT_TRUE(classifySio5v(g_samples, kSio5vSamples) == Sio5v::Present);
}

// --- C-2: the console order, once per boot ---

static void test_absent_runs_once(void)
{
    C8Trigger trigger;
    TEST_ASSERT_FALSE(trigger.ran());
    TEST_ASSERT_TRUE(trigger.request(Sio5v::Absent) == C8Trigger::Answer::Run);
    TEST_ASSERT_TRUE(trigger.ran());
    TEST_ASSERT_TRUE(trigger.request(Sio5v::Absent) == C8Trigger::Answer::AlreadyRan);
}

static void test_present_refuses_and_can_be_asked_again(void)
{
    C8Trigger trigger;
    TEST_ASSERT_TRUE(trigger.request(Sio5v::Present) == C8Trigger::Answer::RefusedSio5v);
    TEST_ASSERT_FALSE(trigger.ran());
    TEST_ASSERT_TRUE(trigger.request(Sio5v::Absent) == C8Trigger::Answer::Run);
}

static void test_invalid_refuses(void)
{
    C8Trigger trigger;
    TEST_ASSERT_TRUE(trigger.request(Sio5v::Invalid) == C8Trigger::Answer::RefusedInvalid);
    TEST_ASSERT_FALSE(trigger.ran());
}

static void test_after_a_run_nothing_runs_again(void)
{
    C8Trigger trigger;
    (void)trigger.request(Sio5v::Absent);
    TEST_ASSERT_TRUE(trigger.request(Sio5v::Present) == C8Trigger::Answer::AlreadyRan);
    TEST_ASSERT_TRUE(trigger.request(Sio5v::Invalid) == C8Trigger::Answer::AlreadyRan);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_power_from_the_atari_is_absent);
    RUN_TEST(test_the_atari_powered_is_present);
    RUN_TEST(test_one_failed_read_is_invalid);
    RUN_TEST(test_too_few_samples_are_invalid);
    RUN_TEST(test_a_value_out_of_range_is_invalid);
    RUN_TEST(test_unstable_readings_are_invalid_even_below_the_threshold);
    RUN_TEST(test_one_sample_at_the_threshold_is_present);
    RUN_TEST(test_absent_runs_once);
    RUN_TEST(test_present_refuses_and_can_be_asked_again);
    RUN_TEST(test_invalid_refuses);
    RUN_TEST(test_after_a_run_nothing_runs_again);
    return UNITY_END();
}
