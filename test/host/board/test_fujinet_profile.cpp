// The FujiNet Atari v1.x microSD profile (H3c preparation).
//
// Data source: the public FujiNet hardware schematics FN32ROV-1.3-Q24 and
// FN32ROV-1.5-Q24 (fujinet-hardware, CERN OHL 2.0), docs/hardware/fujinet-atari-v1x.md.
// The exact revision of our unit is still pending (silkscreen), so this profile
// is NOT the default: RB_BOARD_UNCONFIRMED stays selected (H3-11).

#include <array>
#include <cstdint>

#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "unity.h"

using rb::board::BoardProfile;
using rb::board::fujinet_atari_v1x_profile;
using rb::board::kNoPin;
using rb::board::SdInterface;
using rb::board::SpiHost;
using rb::board::validate_for_storage;
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_microsd_is_spi_on_the_schematic_pins);
    RUN_TEST(test_uses_the_vspi_host_of_those_pins);
    RUN_TEST(test_validates_with_only_the_gpio5_strapping_warning);
    RUN_TEST(test_does_not_touch_sio_buttons_or_leds);
    return UNITY_END();
}
