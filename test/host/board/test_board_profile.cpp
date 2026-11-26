// rb_board: board profiles and the GPIO validator (H3-11). The default profile
// is "unconfirmed" and blocks any storage hardware use.
//
// The pin numbers below are test fixtures that exercise the ESP32 rules; they
// are NOT the pinout of any real board, which stays unknown until H3c.

#include "rb/board/board_profile.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::kNoPin;
namespace PinWarning = rb::board::PinWarning;
using rb::board::SdInterface;
using rb::board::SdSpiPins;
using rb::board::SpiHost;
using rb::board::unconfirmed_profile;
using rb::board::validate_for_storage;
using rb::core::ErrorCode;

namespace {

constexpr rb::board::SioPins kNoSio{kNoPin, kNoPin, kNoPin};

BoardProfile spi_board(SdSpiPins pins, bool psram = false, int8_t card_detect = kNoPin, SpiHost host = SpiHost::Spi2)
{
    return BoardProfile{"test-fixture", true, psram, SdInterface::Spi, pins, host, card_detect, false, kNoSio, kNoPin, false};
}

BoardProfile sdmmc_board(SdInterface width)
{
    return BoardProfile{"test-fixture", true, false, width, SdSpiPins{kNoPin, kNoPin, kNoPin, kNoPin}, SpiHost::None,
                        kNoPin, false, kNoSio, kNoPin, false};
}

ErrorCode rejection(const BoardProfile& profile)
{
    const auto result = validate_for_storage(profile);
    TEST_ASSERT_FALSE_MESSAGE(result.ok(), "expected the profile to be rejected");
    return result.error();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_default_profile_is_unconfirmed_and_blocks_storage(void)
{
    const BoardProfile profile = unconfirmed_profile();
    TEST_ASSERT_FALSE(profile.hardwareConfirmed);
    TEST_ASSERT_TRUE(profile.sdInterface == SdInterface::None);
    TEST_ASSERT_TRUE(rejection(profile) == ErrorCode::BoardNotConfirmed);
}

static void test_unconfirmed_blocks_even_with_pins(void)
{
    BoardProfile profile = spi_board(SdSpiPins{18, 23, 19, 21});
    profile.hardwareConfirmed = false;
    TEST_ASSERT_TRUE(rejection(profile) == ErrorCode::BoardNotConfirmed);
}

static void test_valid_spi_profile(void)
{
    const auto result = validate_for_storage(spi_board(SdSpiPins{18, 23, 19, 21}));
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT8(0, result.value());  // no warnings
}

static void test_strapping_pins_are_warnings_not_errors(void)
{
    const auto result = validate_for_storage(spi_board(SdSpiPins{18, 23, 19, 5}));  // CS on GPIO5
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_TRUE(result.value() & PinWarning::kStrappingPin);
    TEST_ASSERT_FALSE(result.value() & PinWarning::kFlashVoltageStrap);
}

static void test_gpio12_warns_about_flash_voltage(void)
{
    const auto result = validate_for_storage(spi_board(SdSpiPins{18, 23, 12, 21}));  // MISO on GPIO12 (MTDI)
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_TRUE(result.value() & PinWarning::kFlashVoltageStrap);
}

static void test_flash_pins_are_rejected(void)
{
    for (int8_t pin = 6; pin <= 11; ++pin) {
        TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{pin, 23, 19, 21})) == ErrorCode::InvalidPinAssignment);
    }
}

static void test_psram_pins_are_rejected_only_with_psram(void)
{
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{16, 23, 19, 21}, true)) == ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, 17, 19, 21}, true)) == ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(validate_for_storage(spi_board(SdSpiPins{16, 17, 19, 21}, false)).ok());
}

static void test_input_only_pins_cannot_drive_outputs(void)
{
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{34, 23, 19, 21})) == ErrorCode::InvalidPinAssignment);  // SCK
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, 35, 19, 21})) == ErrorCode::InvalidPinAssignment);  // MOSI
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, 23, 19, 39})) == ErrorCode::InvalidPinAssignment);  // CS
    TEST_ASSERT_TRUE(validate_for_storage(spi_board(SdSpiPins{18, 23, 36, 21})).ok());                   // MISO: input
    TEST_ASSERT_TRUE(validate_for_storage(spi_board(SdSpiPins{18, 23, 19, 21}, false, 39)).ok());        // CD: input
}

static void test_missing_duplicate_and_nonexistent_pins_are_rejected(void)
{
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, kNoPin, 19, 21})) == ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, 23, 18, 21})) == ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, 23, 19, 21}, false, 21)) == ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{40, 23, 19, 21})) == ErrorCode::InvalidPinAssignment);
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, 23, 19, 20})) == ErrorCode::InvalidPinAssignment);  // no GPIO20
}

static void test_spi_requires_a_valid_host(void)
{
    TEST_ASSERT_TRUE(validate_for_storage(spi_board(SdSpiPins{18, 23, 19, 21}, false, kNoPin, SpiHost::Spi3)).ok());
    TEST_ASSERT_TRUE(rejection(spi_board(SdSpiPins{18, 23, 19, 21}, false, kNoPin, SpiHost::None)) ==
                     ErrorCode::InvalidPinAssignment);
}

static void test_confirmed_board_without_storage_interface(void)
{
    TEST_ASSERT_TRUE(rejection(sdmmc_board(SdInterface::None)) == ErrorCode::InvalidPinAssignment);
}

static void test_sdmmc_uses_the_fixed_slot1_pins_of_the_esp32(void)
{
    // ESP32 SDMMC slot 1: CLK 14, CMD 15, D0 2, D1 4, D2 12, D3 13 (chip fact).
    const auto one_bit = validate_for_storage(sdmmc_board(SdInterface::Sdmmc1Bit));
    TEST_ASSERT_TRUE(one_bit.ok());
    TEST_ASSERT_TRUE(one_bit.value() & PinWarning::kStrappingPin);        // GPIO2, GPIO15
    TEST_ASSERT_FALSE(one_bit.value() & PinWarning::kFlashVoltageStrap);

    const auto four_bit = validate_for_storage(sdmmc_board(SdInterface::Sdmmc4Bit));
    TEST_ASSERT_TRUE(four_bit.ok());
    TEST_ASSERT_TRUE(four_bit.value() & PinWarning::kFlashVoltageStrap);  // D2 on GPIO12 (MTDI)
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_profile_is_unconfirmed_and_blocks_storage);
    RUN_TEST(test_unconfirmed_blocks_even_with_pins);
    RUN_TEST(test_valid_spi_profile);
    RUN_TEST(test_strapping_pins_are_warnings_not_errors);
    RUN_TEST(test_gpio12_warns_about_flash_voltage);
    RUN_TEST(test_flash_pins_are_rejected);
    RUN_TEST(test_psram_pins_are_rejected_only_with_psram);
    RUN_TEST(test_input_only_pins_cannot_drive_outputs);
    RUN_TEST(test_missing_duplicate_and_nonexistent_pins_are_rejected);
    RUN_TEST(test_spi_requires_a_valid_host);
    RUN_TEST(test_confirmed_board_without_storage_interface);
    RUN_TEST(test_sdmmc_uses_the_fixed_slot1_pins_of_the_esp32);
    return UNITY_END();
}
