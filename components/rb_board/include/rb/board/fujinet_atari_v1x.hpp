#pragma once

#include "rb/board/board_profile.hpp"

namespace rb::board {

// microSD profile of the FujiNet Atari ESP32 board, revisions FN32ROV-1.3/1.5
// (H3c preparation, docs/hardware/fujinet-atari-v1x.md).
//
// Source: the public FujiNet hardware schematics FN32ROV-1.3-Q24 and
// FN32ROV-1.5-Q24 (fujinet-hardware repository, CERN OHL 2.0): microSD J4 in
// SPI mode, CLK GPIO18, CMD/MOSI GPIO23, DAT0/MISO GPIO19, CD/DAT3/CS GPIO5;
// no card-detect line before rev 1.6. Not taken from any FujiNet source code.
//
// SIO pins from the same 1.3 schematic (docs/hardware/h4-puerta-sio.md section 2):
// COMMAND GPIO39 (through U3, R11 10k pull-up), Atari DATA OUT GPIO33 (through
// U4), Atari DATA IN GPIO21 (to U4, open collector; output forbidden until H6).
// Its H4 gate is not signed, so validate_for_sio() never authorizes them. M3a is signed by the
// owner's risk acceptance of R1, not by a measurement (docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md):
// validate_for_motor() grants GPIO36 as an input;
// check_sio_pins() only checks the assignment. Configures nothing by itself. Selecting it
// (RB_BOARD_FUJINET_ATARI_V1X) asserts that the board's silkscreen revision and
// pinout were checked against this data; until then RB_BOARD_UNCONFIRMED stays
// the default. GPIO5 is a strapping pin (validator warning).
constexpr BoardProfile fujinet_atari_v1x_profile() noexcept
{
    return BoardProfile{"fujinet-atari-v1.3/1.5",
                        true,  // asserted by whoever selects the profile (Kconfig help)
                        true,  // ESP32-WROVER module: PSRAM on GPIO16/17
                        SdInterface::Spi,
                        SdSpiPins{18, 23, 19, 5},
                        SpiHost::Spi3,  // VSPI: the native controller for these pins
                        kNoPin,
                        false,  // H4 not approved (docs/hardware/h4-puerta-sio.md); R2, R6 and R7 not accepted
                        SioPins{39, 33, 21},
                        36,      // MOTOR: GPIO36 (SENSOR_VP), straight from SIO pin 8
                        true};   // M3a signed by risk acceptance of R1 (docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md)
}

}  // namespace rb::board
