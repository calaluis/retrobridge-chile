// tapeGatesOpen (main/tape_gates.hpp, spec puertas-aceptacion-riesgo T3b): the menu offers a tape only
// when the build carries the owner's acceptance for DATA IN (tapeOutput), the board grants DATA IN and
// MOTOR, AND start-up really configured both (review PR #123, H2: a permission is not a capability).
// The generic profile has M3a only; H4 stays unsigned.

#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "tape_gates.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::fujinet_atari_v1x_profile;
using rb::menusio::tapeGatesOpen;

void setUp() {}
void tearDown() {}

static BoardProfile bothSigned()
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.sioGateApproved = true;
    p.motorGateApproved = true;
    return p;
}

// H1: a build without the acceptance (every build but menu-sio-cinta) keeps the tape closed.
static void test_without_the_build_acceptance_the_tape_stays_closed(void)
{
    TEST_ASSERT_FALSE(tapeGatesOpen(false, fujinet_atari_v1x_profile(), true, true));
    TEST_ASSERT_FALSE(tapeGatesOpen(false, bothSigned(), true, true));
}

static void test_the_build_acceptance_with_everything_ready_opens_the_tape(void)
{
    TEST_ASSERT_TRUE(tapeGatesOpen(true, fujinet_atari_v1x_profile(), true, true));
}

// H2: MOTOR not configured (gpio_config failed) or DATA IN not shared: closed, «AUN NO PUEDE CARGARSE».
static void test_motor_not_configured_keeps_the_tape_closed(void)
{
    TEST_ASSERT_FALSE(tapeGatesOpen(true, fujinet_atari_v1x_profile(), false, true));
}

static void test_data_in_not_shared_keeps_the_tape_closed(void)
{
    TEST_ASSERT_FALSE(tapeGatesOpen(true, fujinet_atari_v1x_profile(), true, false));
}

static void test_m3a_unsigned_keeps_the_tape_closed(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.motorGateApproved = false;
    TEST_ASSERT_FALSE(tapeGatesOpen(true, p, true, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_without_the_build_acceptance_the_tape_stays_closed);
    RUN_TEST(test_the_build_acceptance_with_everything_ready_opens_the_tape);
    RUN_TEST(test_motor_not_configured_keeps_the_tape_closed);
    RUN_TEST(test_data_in_not_shared_keeps_the_tape_closed);
    RUN_TEST(test_m3a_unsigned_keeps_the_tape_closed);
    return UNITY_END();
}
