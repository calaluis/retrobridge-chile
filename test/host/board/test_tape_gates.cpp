// tapeGatesOpen (main/tape_gates.hpp, spec puertas-aceptacion-riesgo T3b): the menu offers a tape only
// when both board gates are signed AND the tape output is on. The real profile has M3a only.

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

static void test_the_real_profile_keeps_the_tape_closed(void)
{
    TEST_ASSERT_FALSE(tapeGatesOpen(false, fujinet_atari_v1x_profile()));
    TEST_ASSERT_FALSE(tapeGatesOpen(true, fujinet_atari_v1x_profile()));  // H4 unsigned
}

static void test_signed_gates_with_the_output_off_keep_the_tape_closed(void)
{
    TEST_ASSERT_FALSE(tapeGatesOpen(false, bothSigned()));
}

static void test_signed_gates_with_the_output_on_open_the_tape(void)
{
    TEST_ASSERT_TRUE(tapeGatesOpen(true, bothSigned()));
}

static void test_an_unsigned_gate_keeps_the_tape_closed_even_with_the_output_on(void)
{
    BoardProfile p = bothSigned();
    p.sioGateApproved = false;
    TEST_ASSERT_FALSE(tapeGatesOpen(true, p));
    p = bothSigned();
    p.motorGateApproved = false;
    TEST_ASSERT_FALSE(tapeGatesOpen(true, p));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_real_profile_keeps_the_tape_closed);
    RUN_TEST(test_signed_gates_with_the_output_off_keep_the_tape_closed);
    RUN_TEST(test_signed_gates_with_the_output_on_open_the_tape);
    RUN_TEST(test_an_unsigned_gate_keeps_the_tape_closed_even_with_the_output_on);
    return UNITY_END();
}
