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
    std::array<int8_t, 8> used_{};
    std::size_t count_ = 0;
    uint8_t warnings_ = 0;
};

}  // namespace

Result<uint8_t> validate_for_storage(const BoardProfile& profile) noexcept
{
    if (!profile.hardwareConfirmed) {
        return Failure{ErrorCode::BoardNotConfirmed};
    }

    PinChecker checker{profile.hasPsram};
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
        return Failure{ErrorCode::InvalidPinAssignment};
    }
    if (ok && profile.cardDetect != kNoPin) {
        ok = checker.add({profile.cardDetect, false});
    }
    if (!ok) {
        return Failure{ErrorCode::InvalidPinAssignment};
    }
    return checker.warnings();
}

}  // namespace rb::board
