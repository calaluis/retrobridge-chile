// rb_board: SIO pins, check_sio_pins() and validate_for_sio() (H4 code
// deliverable, docs/diseno/h5-escucha-solo-recepcion.md section 5).
//
// The pin numbers below are test fixtures that exercise the ESP32 rules; the
// real FujiNet pinout is tested in test_fujinet_profile.cpp. The rule tests use
// profiles whose H4 gate is NOT signed: checking pins never approves them.

#include "rb/board/board_profile.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::check_sio_pins;
using rb::board::kNoPin;
namespace PinWarning = rb::board::PinWarning;
using rb::board::SdInterface;
using rb::board::SdSpiPins;
using rb::board::SioMode;
using rb::board::SioPins;
using rb::board::SpiHost;
using rb::board::unconfirmed_profile;
using rb::board::validate_for_sio;
using rb::core::ErrorCode;

namespace {

// microSD on 18/23/19/4 (no strapping pin) plus the given SIO pins; H4 gate not signed.
BoardProfile sio_board(SioPins sio, bool psram = false)
{
    return BoardProfile{"test-fixture", true, psram, SdInterface::Spi, SdSpiPins{18, 23, 19, 4}, SpiHost::Spi3,
                        kNoPin, false, sio, kNoPin, false};
}

BoardProfile approved(BoardProfile profile)
{
    profile.sioGateApproved = true;
    return profile;
}

ErrorCode rejection(const BoardProfile& profile, SioMode mode)
{
    const auto result = check_sio_pins(profile, mode);
    TEST_ASSERT_FALSE_MESSAGE(result.ok(), "expected the SIO pins to be rejected");
    return result.error();
}

ErrorCode refusal(const BoardProfile& profile, SioMode mode)
{
    const auto result = validate_for_sio(profile, mode);
    TEST_ASSERT_FALSE_MESSAGE(result.ok(), "expected physical SIO use to be refused");
    return result.error();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Authorization: validate_for_sio() ---

static void test_default_profile_blocks_sio(void)
{
    const BoardProfile profile = unconfirmed_profile();
    TEST_ASSERT_FALSE(profile.sioGateApproved);
    TEST_ASSERT_TRUE(refusal(profile, SioMode::ReceiveOnly) == ErrorCode::BoardNotConfirmed);
}

static void test_valid_pins_are_not_authorized_without_the_h4_gate(void)
{
    const BoardProfile candidate = sio_board(SioPins{39, 33, kNoPin});
    TEST_ASSERT_TRUE(check_sio_pins(candidate, SioMode::ReceiveOnly).ok());  // the pins are fine...
    TEST_ASSERT_TRUE(refusal(candidate, SioMode::ReceiveOnly) == ErrorCode::BoardNotConfirmed);  // ...but not signed
    TEST_ASSERT_FALSE(candidate.sioGateApproved);  // checking did not approve it
}

static void test_unconfirmed_hardware_blocks_even_with_the_gate_signed(void)
{
    BoardProfile profile = approved(sio_board(SioPins{39, 33, kNoPin}));
    profile.hardwareConfirmed = false;
    TEST_ASSERT_TRUE(refusal(profile, SioMode::ReceiveOnly) == ErrorCode::BoardNotConfirmed);
}

static void test_signed_gate_and_valid_pins_are_authorized(void)
{
    const auto result = validate_for_sio(approved(sio_board(SioPins{39, 33, 21})), SioMode::ReceiveTransmit);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(39, result.value().pins.command);
    TEST_ASSERT_EQUAL_INT8(33, result.value().pins.rx);
    TEST_ASSERT_EQUAL_INT8(21, result.value().pins.tx);
    TEST_ASSERT_EQUAL_UINT8(0, result.value().warnings);
}

static void test_receive_only_never_grants_the_wired_tx(void)
{
    const auto result = validate_for_sio(approved(sio_board(SioPins{39, 33, 21})), SioMode::ReceiveOnly);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(39, result.value().pins.command);
    TEST_ASSERT_EQUAL_INT8(33, result.value().pins.rx);
    TEST_ASSERT_EQUAL_INT8(kNoPin, result.value().pins.tx);  // DATA IN is never handed to a driver
}

static void test_signed_gate_still_needs_valid_pins(void)
{
    TEST_ASSERT_TRUE(refusal(approved(sio_board(SioPins{39, 33, 35})), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);  // wired TX on an input-only GPIO
}

// --- Pin rules: check_sio_pins() ---

static void test_valid_receive_only_pins(void)
{
    // COMMAND and RX may sit on input-only GPIOs.
    const auto result = check_sio_pins(sio_board(SioPins{39, 36, kNoPin}), SioMode::ReceiveOnly);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT8(0, result.value());  // no warnings
}

static void test_receive_only_accepts_and_checks_a_wired_tx(void)
{
    TEST_ASSERT_TRUE(check_sio_pins(sio_board(SioPins{39, 33, 21}), SioMode::ReceiveOnly).ok());
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 33, 35}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);  // input-only
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 33, 19}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);  // MISO
}

static void test_receive_transmit_requires_an_output_capable_tx(void)
{
    TEST_ASSERT_TRUE(check_sio_pins(sio_board(SioPins{39, 33, 21}), SioMode::ReceiveTransmit).ok());
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 33, kNoPin}), SioMode::ReceiveTransmit) ==
                     ErrorCode::InvalidPinAssignment);
    for (int8_t pin = 34; pin <= 39; ++pin) {
        if (pin == 36 || pin == 39) {
            continue;  // used by COMMAND/RX below
        }
        TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 36, pin}), SioMode::ReceiveTransmit) ==
                         ErrorCode::InvalidPinAssignment);
    }
}

static void test_missing_command_or_rx_is_rejected(void)
{
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{kNoPin, 33, kNoPin}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, kNoPin, kNoPin}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);
}

static void test_flash_psram_and_nonexistent_pins_are_rejected(void)
{
    for (int8_t pin = 6; pin <= 11; ++pin) {
        TEST_ASSERT_TRUE(rejection(sio_board(SioPins{pin, 33, kNoPin}), SioMode::ReceiveOnly) ==
                         ErrorCode::InvalidPinAssignment);
    }
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 16, kNoPin}, true), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 33, 17}, true), SioMode::ReceiveTransmit) ==
                     ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(check_sio_pins(sio_board(SioPins{39, 16, 17}, false), SioMode::ReceiveTransmit).ok());
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{20, 33, kNoPin}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);  // no GPIO20
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 40, kNoPin}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);
}

static void test_duplicates_and_microsd_collisions_are_rejected(void)
{
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 39, kNoPin}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 33, 33}), SioMode::ReceiveTransmit) ==
                     ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 19, kNoPin}), SioMode::ReceiveOnly) ==
                     ErrorCode::InvalidPinAssignment);  // MISO
    TEST_ASSERT_TRUE(rejection(sio_board(SioPins{39, 33, 4}), SioMode::ReceiveTransmit) ==
                     ErrorCode::InvalidPinAssignment);  // CS
}

static void test_invalid_microsd_pins_also_fail_the_sio_check(void)
{
    BoardProfile profile = sio_board(SioPins{39, 33, kNoPin});
    profile.spiHost = SpiHost::None;
    TEST_ASSERT_TRUE(rejection(profile, SioMode::ReceiveOnly) == ErrorCode::InvalidPinAssignment);
}

static void test_board_without_microsd_checks_only_sio(void)
{
    BoardProfile profile = sio_board(SioPins{39, 19, kNoPin});  // 19 is free without microSD
    profile.sdInterface = SdInterface::None;
    TEST_ASSERT_TRUE(check_sio_pins(profile, SioMode::ReceiveOnly).ok());
}

static void test_strapping_pins_are_warnings(void)
{
    const auto result = check_sio_pins(sio_board(SioPins{39, 12, kNoPin}), SioMode::ReceiveOnly);  // RX on MTDI
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_TRUE(result.value() & PinWarning::kStrappingPin);
    TEST_ASSERT_TRUE(result.value() & PinWarning::kFlashVoltageStrap);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_profile_blocks_sio);
    RUN_TEST(test_valid_pins_are_not_authorized_without_the_h4_gate);
    RUN_TEST(test_unconfirmed_hardware_blocks_even_with_the_gate_signed);
    RUN_TEST(test_signed_gate_and_valid_pins_are_authorized);
    RUN_TEST(test_receive_only_never_grants_the_wired_tx);
    RUN_TEST(test_signed_gate_still_needs_valid_pins);
    RUN_TEST(test_valid_receive_only_pins);
    RUN_TEST(test_receive_only_accepts_and_checks_a_wired_tx);
    RUN_TEST(test_receive_transmit_requires_an_output_capable_tx);
    RUN_TEST(test_missing_command_or_rx_is_rejected);
    RUN_TEST(test_flash_psram_and_nonexistent_pins_are_rejected);
    RUN_TEST(test_duplicates_and_microsd_collisions_are_rejected);
    RUN_TEST(test_invalid_microsd_pins_also_fail_the_sio_check);
    RUN_TEST(test_board_without_microsd_checks_only_sio);
    RUN_TEST(test_strapping_pins_are_warnings);
    return UNITY_END();
}
