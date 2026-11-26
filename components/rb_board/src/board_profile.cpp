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
    std::array<int8_t, 13> used_{};  // up to 7 microSD pins, 3 SIO pins and MOTOR
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
    // A wired TX line is checked even in ReceiveOnly; it is required to transmit.
    if (profile.sio.tx != kNoPin || mode == SioMode::ReceiveTransmit) {
        ok = ok && checker.add({profile.sio.tx, true});
    }
    // MOTOR is SIO pin 8: no other SIO line may share its GPIO (cassette-cas C106).
    if (profile.motor != kNoPin) {
        ok = ok && checker.add({profile.motor, false});
    }
    if (!ok) {
        return Failure{ErrorCode::InvalidPinAssignment};
    }
    return checker.warnings();
}

Result<SioAssignment> validate_for_sio(const BoardProfile& profile, SioMode mode) noexcept
{
    if (!profile.hardwareConfirmed || !profile.sioGateApproved) {
        return Failure{ErrorCode::BoardNotConfirmed};
    }
    const auto checked = check_sio_pins(profile, mode);
    if (!checked.ok()) {
        return Failure{checked.error()};
    }
    SioPins granted = profile.sio;
    if (mode == SioMode::ReceiveOnly) {
        granted.tx = kNoPin;  // DATA IN is never driven before H6
    }
    return SioAssignment{granted, checked.value()};
}

Result<SioListenAssignment> validate_for_sio_listen(const BoardProfile& profile) noexcept
{
    // The same gate and rules as any receive-only use; only the TX line is left out.
    const auto granted = validate_for_sio(profile, SioMode::ReceiveOnly);
    if (!granted.ok()) {
        return Failure{granted.error()};
    }
    const SioAssignment& assignment = granted.value();
    return SioListenAssignment{assignment.pins.command, assignment.pins.rx, assignment.warnings};
}

Result<MotorAssignment> validate_for_motor(const BoardProfile& profile) noexcept
{
    if (!profile.hardwareConfirmed || !profile.motorGateApproved) {
        return Failure{ErrorCode::BoardNotConfirmed};
    }
    if (!is_input_only(profile.motor)) {
        return Failure{ErrorCode::InvalidPinAssignment};  // also kNoPin
    }
    PinChecker checker{profile.hasPsram};
    bool ok = profile.sdInterface == SdInterface::None || add_storage_pins(checker, profile);
    for (const int8_t pin : {profile.sio.command, profile.sio.rx, profile.sio.tx}) {
        if (pin != kNoPin) {
            ok = ok && checker.add({pin, false});  // registered only to catch a collision
        }
    }
    ok = ok && checker.add({profile.motor, false});
    if (!ok) {
        return Failure{ErrorCode::InvalidPinAssignment};
    }
    return MotorAssignment{profile.motor};
}

Result<CassetteOutputAssignment> validate_for_cassette_output(const BoardProfile& profile) noexcept
{
    const auto sio = validate_for_sio(profile, SioMode::ReceiveTransmit);
    if (!sio.ok()) {
        return Failure{sio.error()};
    }
    const auto motor = validate_for_motor(profile);
    if (!motor.ok()) {
        return Failure{motor.error()};
    }
    return CassetteOutputAssignment{sio.value().pins.tx, motor.value().pin()};
}

Result<CassetteOutputAssignment> validate_for_menu_cassette_output(const BoardProfile& profile, bool riskAccepted) noexcept
{
    if (profile.sioGateApproved) {
        return validate_for_cassette_output(profile);  // H4 signed: the same grant as the cassette
    }
    if (!profile.hardwareConfirmed || !riskAccepted) {
        return Failure{ErrorCode::BoardNotConfirmed};
    }
    const auto checked = check_sio_pins(profile, SioMode::ReceiveTransmit);
    if (!checked.ok()) {
        return Failure{checked.error()};
    }
    const auto motor = validate_for_motor(profile);
    if (!motor.ok()) {
        return Failure{motor.error()};
    }
    return CassetteOutputAssignment{profile.sio.tx, motor.value().pin()};
}

Result<CassetteOutputAssignment> validate_for_bench_data_in(const BoardProfile& profile) noexcept
{
    if (!profile.hardwareConfirmed) {
        return Failure{ErrorCode::BoardNotConfirmed};
    }
    // The same pin rules as transmitting on the SIO: a wired, output-capable TX that collides with nothing.
    const auto checked = check_sio_pins(profile, SioMode::ReceiveTransmit);
    if (!checked.ok()) {
        return Failure{checked.error()};
    }
    return CassetteOutputAssignment{profile.sio.tx, kNoPin};
}

}  // namespace rb::board
