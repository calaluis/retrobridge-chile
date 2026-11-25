// rb_board: MotorAssignment and validate_for_motor() (cassette-cas C106, AC9). MOTOR reaches the
// FujiNet's GPIO36 with no buffer: nothing may read it until M3a is signed. Every profile here has
// M3a NOT signed unless a test makes a test-only signed copy; nothing here approves M3a.

#include <cstdint>
#include <type_traits>

#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::fujinet_atari_v1x_profile;
using rb::board::kNoPin;
using rb::board::MotorAssignment;
using rb::board::unconfirmed_profile;
using rb::board::validate_for_motor;
using rb::core::ErrorCode;

// Only validate_for_motor() creates one.
static_assert(!std::is_default_constructible<MotorAssignment>::value, "no unauthorized default value");
static_assert(!std::is_constructible<MotorAssignment, int8_t>::value, "no assignment from a bare pin");

namespace {

BoardProfile signedFujinet()
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.motorGateApproved = true;  // test-only copy
    return p;
}

void expectError(ErrorCode expected, const BoardProfile& profile)
{
    const auto result = validate_for_motor(profile);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(result.error()));
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_fujinet_motor_is_gpio36_and_m3a_is_signed(void)
{
    const BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_EQUAL_INT8(36, p.motor);
    TEST_ASSERT_TRUE(p.motorGateApproved);
    const auto granted = validate_for_motor(p);
    TEST_ASSERT_TRUE(granted.ok());
    TEST_ASSERT_EQUAL_INT8(36, granted.value().pin());
}

static void test_fujinet_motor_with_m3a_unsigned_is_refused(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.motorGateApproved = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
}

static void test_unconfirmed_board_has_no_motor(void)
{
    const BoardProfile p = unconfirmed_profile();
    TEST_ASSERT_EQUAL_INT8(kNoPin, p.motor);
    TEST_ASSERT_FALSE(p.motorGateApproved);
    expectError(ErrorCode::BoardNotConfirmed, p);
}

static void test_signed_m3a_grants_gpio36(void)
{
    const auto result = validate_for_motor(signedFujinet());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(36, result.value().pin());
}

static void test_signed_m3a_on_an_unconfirmed_board_is_refused(void)
{
    BoardProfile p = signedFujinet();
    p.hardwareConfirmed = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
}

static void test_motor_must_be_input_only(void)
{
    BoardProfile p = signedFujinet();
    for (const int8_t pin : {kNoPin, int8_t{4}, int8_t{33}, int8_t{40}}) {
        p.motor = pin;
        expectError(ErrorCode::InvalidPinAssignment, p);
    }
    for (const int8_t pin : {int8_t{34}, int8_t{35}, int8_t{36}, int8_t{37}, int8_t{38}}) {
        p.motor = pin;
        TEST_ASSERT_TRUE(validate_for_motor(p).ok());
    }
}

static void test_motor_must_not_collide_with_sio_or_microsd(void)
{
    BoardProfile p = signedFujinet();
    p.motor = 39;  // COMMAND
    expectError(ErrorCode::InvalidPinAssignment, p);
    p = signedFujinet();
    p.sio.rx = 36;
    expectError(ErrorCode::InvalidPinAssignment, p);
    p = signedFujinet();
    p.cardDetect = 36;
    expectError(ErrorCode::InvalidPinAssignment, p);
}

// The SIO checks see MOTOR too: a SIO line on the MOTOR GPIO is refused from either side.
static void test_sio_checks_refuse_a_line_on_the_motor_pin(void)
{
    BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_TRUE(rb::board::check_sio_pins(p, rb::board::SioMode::ReceiveOnly).ok());
    p.sio.rx = 36;
    TEST_ASSERT_FALSE(rb::board::check_sio_pins(p, rb::board::SioMode::ReceiveOnly).ok());
    p.motor = kNoPin;
    TEST_ASSERT_TRUE(rb::board::check_sio_pins(p, rb::board::SioMode::ReceiveOnly).ok());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fujinet_motor_is_gpio36_and_m3a_is_signed);
    RUN_TEST(test_fujinet_motor_with_m3a_unsigned_is_refused);
    RUN_TEST(test_unconfirmed_board_has_no_motor);
    RUN_TEST(test_signed_m3a_grants_gpio36);
    RUN_TEST(test_signed_m3a_on_an_unconfirmed_board_is_refused);
    RUN_TEST(test_motor_must_be_input_only);
    RUN_TEST(test_motor_must_not_collide_with_sio_or_microsd);
    RUN_TEST(test_sio_checks_refuse_a_line_on_the_motor_pin);
    return UNITY_END();
}
