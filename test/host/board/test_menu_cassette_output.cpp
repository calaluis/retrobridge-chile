// rb_board: validate_for_menu_cassette_output() (cas-salida-data-in). The menu-sio cassette drives DATA IN
// either with H4 signed or with the owner's risk acceptance of R2, R6 and R7 for DATA IN
// (docs/mediciones/aceptacion-data-in-casete-2026-11-26.md), and always follows MOTOR (M3a). H4 stays
// unsigned: validate_for_sio() and validate_for_cassette_output() still refuse the real profile.

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

void expectError(ErrorCode expected, const BoardProfile& profile)
{
    const auto result = validate_for_menu_cassette_output(profile);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(result.error()));
}

}  // namespace

static void test_the_real_profile_records_the_acceptance_and_h4_unsigned(void)
{
    const BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_TRUE(p.dataInRiskAccepted);
    TEST_ASSERT_FALSE(p.sioGateApproved);
    TEST_ASSERT_TRUE(p.motorGateApproved);
}

static void test_the_real_profile_grants_data_in_and_motor_to_the_menu_cassette(void)
{
    const auto result = validate_for_menu_cassette_output(fujinet_atari_v1x_profile());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(21, result.value().dataIn());
    TEST_ASSERT_EQUAL_INT8(36, result.value().motor());
}

static void test_the_acceptance_does_not_open_the_other_gates(void)
{
    TEST_ASSERT_FALSE(validate_for_sio(fujinet_atari_v1x_profile(), SioMode::ReceiveTransmit).ok());
    TEST_ASSERT_FALSE(validate_for_cassette_output(fujinet_atari_v1x_profile()).ok());
}

static void test_without_the_acceptance_or_h4_it_is_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.dataInRiskAccepted = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
    p.sioGateApproved = true;  // test-only copy: H4 signed is enough on its own
    TEST_ASSERT_TRUE(validate_for_menu_cassette_output(p).ok());
}

static void test_without_m3a_it_is_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.motorGateApproved = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
}

static void test_unconfirmed_hardware_is_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.hardwareConfirmed = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
    expectError(ErrorCode::BoardNotConfirmed, unconfirmed_profile());
}

static void test_bad_pins_are_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.sio.tx = 36;  // input-only GPIO: cannot drive DATA IN
    expectError(ErrorCode::InvalidPinAssignment, p);
    p = fujinet_atari_v1x_profile();
    p.sio.tx = 18;  // the microSD clock
    expectError(ErrorCode::InvalidPinAssignment, p);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_real_profile_records_the_acceptance_and_h4_unsigned);
    RUN_TEST(test_the_real_profile_grants_data_in_and_motor_to_the_menu_cassette);
    RUN_TEST(test_the_acceptance_does_not_open_the_other_gates);
    RUN_TEST(test_without_the_acceptance_or_h4_it_is_refused);
    RUN_TEST(test_without_m3a_it_is_refused);
    RUN_TEST(test_unconfirmed_hardware_is_refused);
    RUN_TEST(test_bad_pins_are_refused);
    return UNITY_END();
}
