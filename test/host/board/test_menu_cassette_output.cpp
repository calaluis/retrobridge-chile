// rb_board: validate_for_menu_cassette_output() (cas-salida-data-in). The menu-sio cassette drives DATA IN
// either with H4 signed or with the owner's risk acceptance of R2, R6 and R7 for DATA IN
// (docs/mediciones/aceptacion-data-in-casete-2026-11-26.md), and always follows MOTOR (M3a). The acceptance
// covers one board, so it is not in the generic profile: only the build for that board passes it (review
// PR #123, H1). H4 stays unsigned: validate_for_sio() and validate_for_cassette_output() still refuse.

#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::fujinet_atari_v1x_profile;
using rb::board::SioMode;
using rb::board::unconfirmed_profile;
using rb::board::validate_for_cassette_output;
using rb::board::validate_for_menu_cassette_output;
using rb::board::validate_for_sio;
using rb::core::ErrorCode;

void setUp() {}
void tearDown() {}

namespace {

void expectError(ErrorCode expected, const BoardProfile& profile, bool accepted)
{
    const auto result = validate_for_menu_cassette_output(profile, accepted);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(result.error()));
}

}  // namespace

// H1: the generic profile carries no acceptance; without the build's acceptance nothing is granted.
static void test_the_generic_profile_alone_grants_nothing(void)
{
    const BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_FALSE(p.sioGateApproved);
    TEST_ASSERT_TRUE(p.motorGateApproved);
    expectError(ErrorCode::BoardNotConfirmed, p, false);
}

static void test_the_acceptance_grants_data_in_and_motor(void)
{
    const auto result = validate_for_menu_cassette_output(fujinet_atari_v1x_profile(), true);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(21, result.value().dataIn());
    TEST_ASSERT_EQUAL_INT8(36, result.value().motor());
}

static void test_the_acceptance_does_not_open_the_other_gates(void)
{
    TEST_ASSERT_FALSE(validate_for_sio(fujinet_atari_v1x_profile(), SioMode::ReceiveTransmit).ok());
    TEST_ASSERT_FALSE(validate_for_cassette_output(fujinet_atari_v1x_profile()).ok());
}

static void test_h4_signed_is_enough_on_its_own(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.sioGateApproved = true;  // test-only copy
    TEST_ASSERT_TRUE(validate_for_menu_cassette_output(p, false).ok());
}

static void test_without_m3a_it_is_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.motorGateApproved = false;
    expectError(ErrorCode::BoardNotConfirmed, p, true);
}

static void test_unconfirmed_hardware_is_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.hardwareConfirmed = false;
    expectError(ErrorCode::BoardNotConfirmed, p, true);
    expectError(ErrorCode::BoardNotConfirmed, unconfirmed_profile(), true);
}

static void test_bad_pins_are_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.sio.tx = 36;  // input-only GPIO: cannot drive DATA IN
    expectError(ErrorCode::InvalidPinAssignment, p, true);
    p = fujinet_atari_v1x_profile();
    p.sio.tx = 18;  // the microSD clock
    expectError(ErrorCode::InvalidPinAssignment, p, true);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_generic_profile_alone_grants_nothing);
    RUN_TEST(test_the_acceptance_grants_data_in_and_motor);
    RUN_TEST(test_the_acceptance_does_not_open_the_other_gates);
    RUN_TEST(test_h4_signed_is_enough_on_its_own);
    RUN_TEST(test_without_m3a_it_is_refused);
    RUN_TEST(test_unconfirmed_hardware_is_refused);
    RUN_TEST(test_bad_pins_are_refused);
    return UNITY_END();
}
