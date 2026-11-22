#pragma once

// Watches the +5 V of the SIO (pin 10, read through the board's divider) to tell when the 65XE was
// switched off and on while the FujiNet stays powered by USB (menu-unidades-persistentes, C10).
// D1 goes back to the menu on the power-off, so it is ready before the Atari reads it again.
// A software restart of the Atari (the menu boots with COLDSV) keeps the +5 V and is not seen.
// Pure logic: the caller samples the ADC and passes millivolts and a monotonic time.

#include <cstdint>

namespace rb::host {

class SioPowerWatch {
public:
    enum class Event : uint8_t { None, PoweredOff, PoweredOn };

    static constexpr int kOffBelowMv = 1000;      // below this the Atari is off...
    static constexpr uint64_t kOffForUs = 100000;  // ...once it stays there this long
    static constexpr int kOnFromMv = 4000;        // at or above this, after a power-off, it is on

    // One sample. Each event is reported once: PoweredOff after kOffForUs below kOffBelowMv,
    // PoweredOn at the first sample at or above kOnFromMv after a PoweredOff. A sample at or above
    // kOffBelowMv ends a low run before it counts.
    Event sample(int mv, uint64_t nowUs) noexcept
    {
        if (mv < kOffBelowMv) {
            if (!low_) {
                low_ = true;
                lowSinceUs_ = nowUs;
            }
            if (!off_ && nowUs - lowSinceUs_ >= kOffForUs) {
                off_ = true;
                return Event::PoweredOff;
            }
            return Event::None;
        }
        low_ = false;
        if (off_ && mv >= kOnFromMv) {
            off_ = false;
            return Event::PoweredOn;
        }
        return Event::None;
    }

private:
    bool low_ = false;
    bool off_ = false;
    uint64_t lowSinceUs_ = 0;
};

}  // namespace rb::host
