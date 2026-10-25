#include "rb/board/board_profile.hpp"

#include <array>
#include <cstddef>

namespace rb::board {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;

namespace {

// ESP32 GPIO facts (ESP-IDF GPIO reference): pads 0-19, 21-23, 25-27, 32-39;
// 6-11 belong to the SPI flash; 34-39 are input-only; 0, 2, 5, 12, 15 strap boot.
bool exists(int8_t pin) noexcept
{
    return (pin >= 0 && pin <= 19) || (pin >= 21 && pin <= 23) || (pin >= 25 && pin <= 27) ||
           (pin >= 32 && pin <= 39);
}
bool is_flash(int8_t pin) noexcept { return pin >= 6 && pin <= 11; }
bool is_psram(int8_t pin) noexcept { return pin == 16 || pin == 17; }
bool is_input_only(int8_t pin) noexcept { return pin >= 34 && pin <= 39; }
bool is_strapping(int8_t pin) noexcept { return pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15; }

struct Pin {
    int8_t number;
    bool output;
};

class PinChecker {
public:
    explicit PinChecker(bool psram) noexcept : psram_(psram) {}

    bool add(Pin pin) noexcept
    {
        if (!exists(pin.number) || is_flash(pin.number) || (psram_ && is_psram(pin.number)) ||
            (pin.output && is_input_only(pin.number))) {
            return false;
        }
        for (std::size_t i = 0; i < count_; ++i) {
            if (used_[i] == pin.number) {
                return false;  // duplicate
            }
        }
        used_[count_++] = pin.number;
        if (is_strapping(pin.number)) {
            warnings_ |= PinWarning::kStrappingPin;
        }
        if (pin.number == 12) {
            warnings_ |= PinWarning::kFlashVoltageStrap;
        }
        return true;
    }

    uint8_t warnings() const noexcept { return warnings_; }

private:
    bool psram_;
    std::array<int8_t, 12> used_{};  // up to 7 microSD pins plus 3 SIO pins
    std::size_t count_ = 0;
    uint8_t warnings_ = 0;
};

// Registers the microSD pins of a profile whose interface is not None.
bool add_storage_pins(PinChecker& checker, const BoardProfile& profile) noexcept
{
    bool ok = true;
    switch (profile.sdInterface) {
    case SdInterface::Spi:
        ok = (profile.spiHost == SpiHost::Spi2 || profile.spiHost == SpiHost::Spi3) &&
             checker.add({profile.spi.sck, true}) && checker.add({profile.spi.mosi, true}) &&
             checker.add({profile.spi.miso, false}) && checker.add({profile.spi.cs, true});
        break;
    case SdInterface::Sdmmc4Bit:
        ok = checker.add({4, true}) && checker.add({12, true}) && checker.add({13, true});
        [[fallthrough]];
    case SdInterface::Sdmmc1Bit:
        ok = ok && checker.add({14, true}) && checker.add({15, true}) && checker.add({2, true});
        break;
    case SdInterface::None:
    default:
        return false;
    }
    if (ok && profile.cardDetect != kNoPin) {
        ok = checker.add({profile.cardDetect, false});
    }
    return ok;
}

}  // namespace

Result<uint8_t> validate_for_storage(const BoardProfile& profile) noexcept
{
    if (!profile.hardwareConfirmed) {
        return Failure{ErrorCode::BoardNotConfirmed};
    }

    PinChecker checker{profile.hasPsram};
    if (!add_storage_pins(checker, profile)) {
        return Failure{ErrorCode::InvalidPinAssignment};
    }
    return checker.warnings();
}

Result<uint8_t> check_sio_pins(const BoardProfile& profile, SioMode mode) noexcept
{
    PinChecker checker{profile.hasPsram};
    // A board without microSD has no storage pins to collide with.
    bool ok = profile.sdInterface == SdInterface::None || add_storage_pins(checker, profile);
    ok = ok && checker.add({profile.sio.command, false}) && checker.add({profile.sio.rx, false});
    if (mode == SioMode::ReceiveOnly) {
        ok = ok && profile.sio.tx == kNoPin;  // DATA IN is never driven before H6
    } else {
        ok = ok && checker.add({profile.sio.tx, true});
    }
    if (!ok) {
        return Failure{ErrorCode::InvalidPinAssignment};
    }
    return checker.warnings();
}

Result<uint8_t> validate_for_sio(const BoardProfile& profile, SioMode mode) noexcept
{
    if (!profile.hardwareConfirmed || !profile.sioGateApproved) {
        return Failure{ErrorCode::BoardNotConfirmed};
    }
    return check_sio_pins(profile, mode);
}

}  // namespace rb::board
