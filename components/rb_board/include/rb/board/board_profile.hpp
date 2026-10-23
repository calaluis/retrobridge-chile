#pragma once

#include <cstdint>

#include "rb/core/result.hpp"

namespace rb::board {

constexpr int8_t kNoPin = -1;

enum class SdInterface : uint8_t {
    None,       // no microSD configured
    Spi,        // SDSPI on the pins of SdSpiPins (any GPIO through the matrix)
    Sdmmc1Bit,  // ESP32 SDMMC slot 1, fixed pins: CLK 14, CMD 15, D0 2
    Sdmmc4Bit,  // ESP32 SDMMC slot 1, fixed pins: plus D1 4, D2 12, D3 13
};

struct SdSpiPins {
    int8_t sck;
    int8_t mosi;
    int8_t miso;
    int8_t cs;
};

// Everything the firmware may know about a board (H3-11). Deliberately has no
// SIO pins: they are added only after the hardware gate H4.
struct BoardProfile {
    const char* name;
    bool hardwareConfirmed;  // pinout and power verified on the real board (Annex A)
    bool hasPsram;           // PSRAM uses GPIO16/17 on ESP32 modules
    SdInterface sdInterface;
    SdSpiPins spi;           // only for SdInterface::Spi
    int8_t cardDetect;       // input, or kNoPin
};

// The only profile until the board is identified: blocks the microSD bus and
// every board-specific GPIO.
constexpr BoardProfile unconfirmed_profile() noexcept
{
    return BoardProfile{"unconfirmed", false, false, SdInterface::None, SdSpiPins{kNoPin, kNoPin, kNoPin, kNoPin},
                        kNoPin};
}

// Warning flags returned by validate_for_storage().
namespace PinWarning {
constexpr uint8_t kStrappingPin = 0x01;      // uses GPIO 0, 2, 5, 12 or 15: check boot levels
constexpr uint8_t kFlashVoltageStrap = 0x02; // uses GPIO12 (MTDI): a pull-up can select 1.8 V flash
}  // namespace PinWarning

// Checks that the profile may be used to start the microSD. Must pass before
// any storage driver call. Errors:
//   BoardNotConfirmed    - hardware not confirmed (the default): touch nothing
//   InvalidPinAssignment - no interface, missing/duplicate pin, nonexistent GPIO,
//                          flash pins 6-11, PSRAM pins 16-17, or an output on an
//                          input-only GPIO 34-39
// On success returns PinWarning flags (warnings, not errors).
rb::core::Result<uint8_t> validate_for_storage(const BoardProfile& profile) noexcept;

}  // namespace rb::board
