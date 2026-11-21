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

// SPI controller for SdInterface::Spi. On the ESP32, SPI2 (HSPI) and SPI3
// (VSPI) are usable; GPIO 18/23/19/5 are the native VSPI pins. The storage
// driver uses this one value for the bus, the SDSPI host and the device, so
// they can never disagree (H3c).
enum class SpiHost : uint8_t {
    None,  // not SPI
    Spi2,
    Spi3,
};

// SIO bus lines as seen from the ESP32 (H4 code deliverable, see
// docs/diseno/h5-escucha-solo-recepcion.md section 5). Directions follow the
// Atari naming: the Atari's DATA OUT is our receive line and its DATA IN our
// transmit line.
struct SioPins {
    int8_t command;  // COMMAND, input (input-only GPIO 34-39 allowed)
    int8_t rx;       // Atari DATA OUT, input (UART RX)
    int8_t tx;       // Atari DATA IN, output (UART TX), or kNoPin if the board has none
};

// How the SIO lines will be used: H5 only listens, H6 onwards also transmits.
enum class SioMode : uint8_t {
    ReceiveOnly,
    ReceiveTransmit,
};

// Everything the firmware may know about a board (H3-11). The SIO pins are
// data only: nothing may use them until the hardware gate H4 is signed
// (sioGateApproved) and validate_for_sio() accepts them; check_sio_pins()
// checks the pin rules alone.
struct BoardProfile {
    const char* name;
    bool hardwareConfirmed;  // pinout and power verified on the real board (Annex A)
    bool hasPsram;           // PSRAM uses GPIO16/17 on ESP32 modules
    SdInterface sdInterface;
    SdSpiPins spi;           // only for SdInterface::Spi
    SpiHost spiHost;         // only for SdInterface::Spi
    int8_t cardDetect;       // input, or kNoPin
    bool sioGateApproved;    // H4 checklist signed (docs/hardware/h4-puerta-sio.md section 7)
    SioPins sio;             // all kNoPin until the SIO pinout is assigned
    // SIO pin 8 (MOTOR CONTROL), input only. On the FujiNet it reaches the GPIO with no buffer or
    // divider, so nothing may read it until M3a has measured its level on the Atari alone
    // (docs/hardware/procedimiento-medicion-pasiva-sio.md, M3a) and the gate is signed here.
    int8_t motor;            // input-only GPIO 34-39, or kNoPin
    bool motorGateApproved;  // M3a signed (cassette-cas C106)
};

// The only profile until the board is identified: blocks the microSD bus and
// every board-specific GPIO.
constexpr BoardProfile unconfirmed_profile() noexcept
{
    return BoardProfile{"unconfirmed",
                        false,
                        false,
                        SdInterface::None,
                        SdSpiPins{kNoPin, kNoPin, kNoPin, kNoPin},
                        SpiHost::None,
                        kNoPin,
                        false,
                        SioPins{kNoPin, kNoPin, kNoPin},
                        kNoPin,
                        false};
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
//                          flash pins 6-11, PSRAM pins 16-17, an output on an
//                          input-only GPIO 34-39, or SPI without a valid host
// On success returns PinWarning flags (warnings, not errors).
rb::core::Result<uint8_t> validate_for_storage(const BoardProfile& profile) noexcept;

// Checks only the SIO pin rules for the given mode; ignores hardwareConfirmed
// and sioGateApproved, so a candidate profile can be checked without approving
// it. Never authorizes physical use. A TX line the board has is checked in both
// modes (it is wired even while unused). Errors:
//   InvalidPinAssignment - missing/duplicate pin (also against the microSD
//                          pins), nonexistent GPIO, flash pins 6-11, PSRAM
//                          pins 16-17, TX on an input-only GPIO 34-39, or TX
//                          missing in ReceiveTransmit
// On success returns PinWarning flags for the SIO and microSD pins together.
rb::core::Result<uint8_t> check_sio_pins(const BoardProfile& profile, SioMode mode) noexcept;

// The SIO pins a driver may use, as granted by validate_for_sio().
struct SioAssignment {
    SioPins pins;      // tx is kNoPin in ReceiveOnly, whatever the board has
    uint8_t warnings;  // PinWarning flags, as check_sio_pins()
};

// Authorizes physical use of the SIO pins: the hardware must be confirmed and
// the H4 gate signed, then check_sio_pins() must pass. SIO drivers take only
// the returned assignment, so in ReceiveOnly they never get the TX line. Must
// pass before any SIO driver call. Errors:
//   BoardNotConfirmed    - hardware not confirmed, or the H4 gate not signed
//   InvalidPinAssignment - as check_sio_pins()
rb::core::Result<SioAssignment> validate_for_sio(const BoardProfile& profile, SioMode mode) noexcept;

class SioListenAssignment;

// Authorizes passive listening only (H5, design of rb_esp_sio, delivery E1):
// exactly validate_for_sio(profile, ReceiveOnly), keeping COMMAND and RX alone.
// Same errors:
//   BoardNotConfirmed    - hardware not confirmed, or the H4 gate not signed
//   InvalidPinAssignment - as check_sio_pins() in ReceiveOnly
rb::core::Result<SioListenAssignment> validate_for_sio_listen(const BoardProfile& profile) noexcept;

// The pins a passive listener may use: COMMAND and RX, never TX. There is no
// transmit line to read, and only validate_for_sio_listen() can create one, so
// a listener driver that takes this type cannot be handed DATA IN by
// construction. Copyable, so it can travel in a Result.
class SioListenAssignment {
public:
    int8_t command() const noexcept { return command_; }
    int8_t rx() const noexcept { return rx_; }
    uint8_t warnings() const noexcept { return warnings_; }  // PinWarning flags, as check_sio_pins()

private:
    constexpr SioListenAssignment(int8_t command, int8_t rx, uint8_t warnings) noexcept
        : command_(command), rx_(rx), warnings_(warnings)
    {
    }

    friend rb::core::Result<SioListenAssignment> validate_for_sio_listen(const BoardProfile& profile) noexcept;

    int8_t command_;
    int8_t rx_;
    uint8_t warnings_;
};

class MotorAssignment;

// Authorizes reading the MOTOR line (cassette-cas C106): the hardware must be confirmed and M3a
// signed. The pin must be an input-only GPIO 34-39, so it can never be driven by mistake, and must
// not collide with the microSD or SIO pins. Errors:
//   BoardNotConfirmed    - hardware not confirmed, or M3a not signed
//   InvalidPinAssignment - no pin, not input-only, flash/PSRAM pin, or a collision
rb::core::Result<MotorAssignment> validate_for_motor(const BoardProfile& profile) noexcept;

// The MOTOR pin a driver may read. Only validate_for_motor() creates one.
class MotorAssignment {
public:
    int8_t pin() const noexcept { return pin_; }

private:
    constexpr explicit MotorAssignment(int8_t pin) noexcept : pin_(pin) {}

    friend rb::core::Result<MotorAssignment> validate_for_motor(const BoardProfile& profile) noexcept;

    int8_t pin_;
};

}  // namespace rb::board
