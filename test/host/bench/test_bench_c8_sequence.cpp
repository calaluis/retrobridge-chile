// Bench C8a, phase F1: the sequence of main/bench_c8.cpp on the host, against fake_idf's GPIO matrix. GPIO21 goes
// UART2 -> RMT -> UART2 (a run that plays to its end), UART2 -> RMT -> UART2 (a run cancelled with stop()), then it
// is released. run_c8 says PASS only when every step reads as expected and no ESP-IDF call failed; each test below
// that breaks one step makes it say FAIL. Ownership only: no electrical level (that is C8b).
#include <unity.h>

#include <new>

#include "bench_c8.hpp"
#include "fake_idf.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"

using fake_idf::Owner;
using rb::esp::EspDataIn;

namespace {

alignas(EspDataIn) unsigned char g_storage[sizeof(EspDataIn)];
EspDataIn* g_dataIn = nullptr;
unsigned g_firstRunDelays = 0;

// The RMT plays the first run (run A) after a few ticks of the cassette loop, as the bench expects. The second run
// (run B) never ends on its own: run_c8 cancels it with stop().
void playFirstRun()
{
    auto& s = fake_idf::state();
    if (s.rmtBusy && s.rmtTransmits == 1 && ++g_firstRunDelays >= 5) {
        (void)fake_idf::play_all();
    }
}

rb::board::BoardProfile profile() { return rb::board::fujinet_atari_v1x_profile(); }  // the board as signed today

bool run()
{
    g_dataIn = new (g_storage) EspDataIn();
    return rb::bench::run_c8(profile(), *g_dataIn);
}

}  // namespace

void setUp(void)
{
    fake_idf::reset();
    fake_idf::state().onDelay = &playFirstRun;
    g_firstRunDelays = 0;
}

void tearDown(void) {}

static void test_the_sequence_passes_and_leaves_gpio21_released(void)
{
    TEST_ASSERT_TRUE(run());
    TEST_ASSERT_FALSE(g_dataIn->enabled());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);  // reset: output off
    TEST_ASSERT_FALSE(fake_idf::state().uartInstalled);
    TEST_ASSERT_EQUAL_UINT32(2, fake_idf::state().rmtNewChannels);  // run A and run B, one channel each
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
}

static void test_the_grant_holds_with_h4_not_approved(void)
{
    const auto board = profile();
    TEST_ASSERT_FALSE(board.sioGateApproved);  // H4 not approved: the bench grant ignores the gates
    TEST_ASSERT_TRUE(run());
}

static void test_a_failed_reset_at_the_end_fails_the_bench(void)
{
    fake_idf::state().onDelay = [] {
        playFirstRun();
        if (fake_idf::state().rmtTransmits == 2) {
            fake_idf::state().failGpioReset = true;  // from run B on: the release at step 8 cannot reset GPIO21
        }
    };
    TEST_ASSERT_FALSE(run());
    TEST_ASSERT_TRUE(g_dataIn->faulted());
}

static void test_an_rmt_that_cannot_start_fails_the_bench(void)
{
    fake_idf::state().failRmtNewChannel = true;
    TEST_ASSERT_FALSE(run());
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
}

static void test_a_run_that_never_ends_fails_the_bench(void)
{
    fake_idf::state().onDelay = nullptr;  // the RMT never reports run A done
    TEST_ASSERT_FALSE(run());
}

static void test_a_failed_call_the_driver_tolerates_still_fails_the_bench(void)
{
    fake_idf::state().failRmtDelEncoder = true;  // traced as failed; no step reads it, only the call count
    TEST_ASSERT_FALSE(run());
}

static void test_without_sio_pins_gpio21_is_never_touched(void)
{
    auto board = profile();
    board.hardwareConfirmed = false;  // the grant refuses: nothing is configured
    g_dataIn = new (g_storage) EspDataIn();
    TEST_ASSERT_FALSE(rb::bench::run_c8(board, *g_dataIn));
    TEST_ASSERT_EQUAL_UINT32(0, fake_idf::state().uartInstalls);
    TEST_ASSERT_EQUAL_UINT32(0, fake_idf::state().uartSetPinCalls);
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::None);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_sequence_passes_and_leaves_gpio21_released);
    RUN_TEST(test_the_grant_holds_with_h4_not_approved);
    RUN_TEST(test_a_failed_reset_at_the_end_fails_the_bench);
    RUN_TEST(test_an_rmt_that_cannot_start_fails_the_bench);
    RUN_TEST(test_a_run_that_never_ends_fails_the_bench);
    RUN_TEST(test_a_failed_call_the_driver_tolerates_still_fails_the_bench);
    RUN_TEST(test_without_sio_pins_gpio21_is_never_touched);
    return UNITY_END();
}
