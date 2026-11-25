// The FujiNet Atari v1.x profile: microSD (H3c) and SIO pinout (H4 stage 2).
//
// Data source: the public FujiNet hardware schematics FN32ROV-1.3-Q24 and
// FN32ROV-1.5-Q24 (fujinet-hardware, CERN OHL 2.0), docs/hardware/fujinet-atari-v1x.md
// and docs/hardware/h4-puerta-sio.md section 2. Our unit is an FN32ROV-1.3-Q24
// (silkscreen). The profile is NOT the default: RB_BOARD_UNCONFIRMED stays
// selected (H3-11), and its H4 gate is not signed.

#include <array>
#include <cstdint>

#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::check_sio_pins;
using rb::board::fujinet_atari_v1x_profile;
using rb::board::kNoPin;
using rb::board::SdInterface;
using rb::board::SioMode;
using rb::board::SpiHost;
using rb::board::validate_for_sio;
using rb::board::validate_for_storage;
using rb::core::ErrorCode;
namespace PinWarning = rb::board::PinWarning;

namespace {

// GPIOs wired to the SIO bus, buttons and LEDs on FN32ROV-1.3/1.5 (schematics):
// SIO CMD 39, DATAOUT 33, DATAIN 21, CKOUT 32, CKIN 27, PROC 22, INT 26,
// MOTOR 36, SIO_5V sense 35, AUDIO 25; buttons 0, 14, 34; LEDs 2, 4, 13.
constexpr std::array<int8_t, 16> kNotForStorage{39, 33, 21, 32, 27, 22, 26, 36, 35, 25, 0, 14, 34, 2, 4, 13};

bool reserved(int8_t pin)
{
    for (int8_t other : kNotForStorage) {
        if (pin == other) {
            return true;
        }
    }
    return false;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_microsd_is_spi_on_the_schematic_pins(void)
{
    const BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_TRUE(p.sdInterface == SdInterface::Spi);
    TEST_ASSERT_EQUAL_INT8(18, p.spi.sck);
    TEST_ASSERT_EQUAL_INT8(23, p.spi.mosi);
    TEST_ASSERT_EQUAL_INT8(19, p.spi.miso);
    TEST_ASSERT_EQUAL_INT8(5, p.spi.cs);
    TEST_ASSERT_EQUAL_INT8(kNoPin, p.cardDetect);  // card detect exists only from rev 1.6 (GPIO15)
    TEST_ASSERT_TRUE(p.hasPsram);                  // ESP32-WROVER: GPIO16/17 reserved
}

static void test_uses_the_vspi_host_of_those_pins(void)
{
    // GPIO 18/23/19/5 are the native VSPI pins, i.e. SPI3_HOST on the ESP32.
    TEST_ASSERT_TRUE(fujinet_atari_v1x_profile().spiHost == SpiHost::Spi3);
}

static void test_validates_with_only_the_gpio5_strapping_warning(void)
{
    const auto result = validate_for_storage(fujinet_atari_v1x_profile());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT8(PinWarning::kStrappingPin, result.value());  // GPIO5 (CS) is a strapping pin
}

static void test_does_not_touch_sio_buttons_or_leds(void)
{
    const BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_FALSE(reserved(p.spi.sck));
    TEST_ASSERT_FALSE(reserved(p.spi.mosi));
    TEST_ASSERT_FALSE(reserved(p.spi.miso));
    TEST_ASSERT_FALSE(reserved(p.spi.cs));
}

static void test_sio_pins_follow_the_1_3_schematic(void)
{
    const BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_EQUAL_INT8(39, p.sio.command);  // COMMAND through U3, R11 pull-up
    TEST_ASSERT_EQUAL_INT8(33, p.sio.rx);       // Atari DATA OUT through U4
    TEST_ASSERT_EQUAL_INT8(21, p.sio.tx);       // Atari DATA IN to U4 (output forbidden until H6)
}

static void test_h4_gate_is_signed_by_risk_acceptance(void)
{
    // docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md: the owner's risk acceptance, not a measurement.
    const BoardProfile p = fujinet_atari_v1x_profile();
    TEST_ASSERT_TRUE(p.hardwareConfirmed);
    TEST_ASSERT_TRUE(p.sioGateApproved);
    TEST_ASSERT_TRUE(p.motorGateApproved);
}

static void test_sio_pins_pass_the_rules_without_approving_h4(void)
{
    // No conflict with the microSD, flash or PSRAM pins; only the GPIO5 (microSD CS) warning.
    const auto receive_only = check_sio_pins(fujinet_atari_v1x_profile(), SioMode::ReceiveOnly);
    TEST_ASSERT_TRUE(receive_only.ok());
    TEST_ASSERT_EQUAL_UINT8(PinWarning::kStrappingPin, receive_only.value());

    const auto receive_transmit = check_sio_pins(fujinet_atari_v1x_profile(), SioMode::ReceiveTransmit);
    TEST_ASSERT_TRUE(receive_transmit.ok());
    TEST_ASSERT_EQUAL_UINT8(PinWarning::kStrappingPin, receive_transmit.value());

}

static void test_physical_sio_use_is_granted(void)
{
    const BoardProfile p = fujinet_atari_v1x_profile();
    const auto receive_only = validate_for_sio(p, SioMode::ReceiveOnly);
    TEST_ASSERT_TRUE(receive_only.ok());
    TEST_ASSERT_EQUAL_INT8(kNoPin, receive_only.value().pins.tx);

    const auto receive_transmit = validate_for_sio(p, SioMode::ReceiveTransmit);
    TEST_ASSERT_TRUE(receive_transmit.ok());
    TEST_ASSERT_EQUAL_INT8(21, receive_transmit.value().pins.tx);
}

static void test_physical_sio_use_is_refused_with_h4_unsigned(void)
{
    BoardProfile closed = fujinet_atari_v1x_profile();
    closed.sioGateApproved = false;
    const auto receive_only = validate_for_sio(closed, SioMode::ReceiveOnly);
    TEST_ASSERT_FALSE(receive_only.ok());
    TEST_ASSERT_TRUE(receive_only.error() == ErrorCode::BoardNotConfirmed);
    const auto receive_transmit = validate_for_sio(closed, SioMode::ReceiveTransmit);
    TEST_ASSERT_FALSE(receive_transmit.ok());
    TEST_ASSERT_TRUE(receive_transmit.error() == ErrorCode::BoardNotConfirmed);
}

static void test_receive_only_would_not_grant_gpio21(void)
{
    // A copy with the gate signed, as the real profile now is.
    BoardProfile signed_copy = fujinet_atari_v1x_profile();
    signed_copy.sioGateApproved = true;
    const auto result = validate_for_sio(signed_copy, SioMode::ReceiveOnly);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT8(39, result.value().pins.command);
    TEST_ASSERT_EQUAL_INT8(33, result.value().pins.rx);
    TEST_ASSERT_EQUAL_INT8(kNoPin, result.value().pins.tx);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_microsd_is_spi_on_the_schematic_pins);
    RUN_TEST(test_uses_the_vspi_host_of_those_pins);
    RUN_TEST(test_validates_with_only_the_gpio5_strapping_warning);
    RUN_TEST(test_does_not_touch_sio_buttons_or_leds);
    RUN_TEST(test_sio_pins_follow_the_1_3_schematic);
    RUN_TEST(test_h4_gate_is_signed_by_risk_acceptance);
    RUN_TEST(test_sio_pins_pass_the_rules_without_approving_h4);
    RUN_TEST(test_physical_sio_use_is_granted);
    RUN_TEST(test_physical_sio_use_is_refused_with_h4_unsigned);
    RUN_TEST(test_receive_only_would_not_grant_gpio21);
    return UNITY_END();
}
