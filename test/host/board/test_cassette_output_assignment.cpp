// rb_board: CassetteOutputAssignment and validate_for_cassette_output() (cassette-cas C107).
// The cassette drives DATA IN (records by UART, FSK by RMT) and follows MOTOR, so it needs both
// gates: H4 signed for transmission and M3a signed for MOTOR. The real FujiNet profile has
// neither signed; every signed profile here is a test-only copy. Nothing here approves a gate.

#include <cstdint>
#include <type_traits>

#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::CassetteOutputAssignment;
using rb::board::fujinet_atari_v1x_profile;
using rb::board::kNoPin;
using rb::board::unconfirmed_profile;
using rb::board::validate_for_cassette_output;
using rb::core::ErrorCode;

static_assert(!std::is_default_constructible<CassetteOutputAssignment>::value, "no unauthorized default value");
static_assert(!std::is_constructible<CassetteOutputAssignment, int8_t, int8_t>::value,
              "no assignment from bare pins");

namespace {

BoardProfile bothSigned()
{
    BoardProfile p = fujinet_atari_v1x_profile();
    p.sioGateApproved = true;    // test-only copy
    p.motorGateApproved = true;  // test-only copy
    return p;
}

void expectError(ErrorCode expected, const BoardProfile& profile)
{
    const auto result = validate_for_cassette_output(profile);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(result.error()));
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_real_fujinet_profile_is_refused(void)
{
    expectError(ErrorCode::BoardNotConfirmed, fujinet_atari_v1x_profile());
    expectError(ErrorCode::BoardNotConfirmed, unconfirmed_profile());
}

static void test_both_gates_grant_data_in_and_motor(void)
{
    const auto result = validate_for_cassette_output(bothSigned());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(21, result.value().dataIn());
    TEST_ASSERT_EQUAL_INT8(36, result.value().motor());
}

static void test_h4_alone_is_not_enough(void)
{
    BoardProfile p = bothSigned();
    p.motorGateApproved = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
}

static void test_m3a_alone_is_not_enough(void)
{
    BoardProfile p = bothSigned();
    p.sioGateApproved = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
}

static void test_unconfirmed_hardware_is_refused(void)
{
    BoardProfile p = bothSigned();
    p.hardwareConfirmed = false;
    expectError(ErrorCode::BoardNotConfirmed, p);
}

static void test_bad_pins_are_refused(void)
{
    BoardProfile p = bothSigned();
    p.sio.tx = kNoPin;  // no DATA IN line
    expectError(ErrorCode::InvalidPinAssignment, p);
    p = bothSigned();
    p.sio.tx = 36;  // DATA IN on the MOTOR pin
    expectError(ErrorCode::InvalidPinAssignment, p);
    p = bothSigned();
    p.motor = kNoPin;
    expectError(ErrorCode::InvalidPinAssignment, p);
    p = bothSigned();
    p.sio.tx = 34;  // input-only GPIO cannot drive DATA IN
    expectError(ErrorCode::InvalidPinAssignment, p);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_real_fujinet_profile_is_refused);
    RUN_TEST(test_both_gates_grant_data_in_and_motor);
    RUN_TEST(test_h4_alone_is_not_enough);
    RUN_TEST(test_m3a_alone_is_not_enough);
    RUN_TEST(test_unconfirmed_hardware_is_refused);
    RUN_TEST(test_bad_pins_are_refused);
    return UNITY_END();
}
