#pragma once
// Bench C8a, phase F1 (docs/hardware/banco-c8a-aislado.md, controls C-1 and C-2): when the bench may take
// GPIO21. Pure logic, tested on the host; main/bench_c8_console.cpp samples the ADC and asks.
//
// C-1, the SIO +5 V interlock: the bench runs only when every reading of GPIO35 is valid and says no power from
// the Atari. A failed read, too few samples, a value outside the ADC's range or readings that disagree are not a
// reading: the answer is Invalid and the bench does not run (fail safe). Its limit: an Atari switched off but still
// connected reads as absent too, so the rule "SIO cable disconnected" still holds.
// C-2, the console order: the bench never runs at boot, only on `c8a run`, and at most once per boot.
#include <cstddef>
#include <cstdint>

namespace rb::bench {

// One ADC reading of GPIO35: the SIO +5 V (pin 10) through R4 2 k / R5 3.9 k. ok is false when the read failed.
struct Sio5vSample {
    bool ok = false;
    int pinMv = 0;
};

enum class Sio5v : uint8_t { Absent, Present, Invalid };

constexpr std::size_t kSio5vSamples = 8;
constexpr int kAbsentBelowPinMv = 500;  // as menu-sio: below this, no power from the Atari (about 0.76 V on pin 10)
constexpr int kMaxPinMv = 3300;         // above the ADC's range: not a reading
constexpr int kMaxSpreadMv = 200;       // samples further apart than this: not a steady line, not a reading

// Invalid unless exactly kSio5vSamples samples (or more) all read, all within 0..kMaxPinMv, at most kMaxSpreadMv
// apart. Then Absent when every sample is below kAbsentBelowPinMv, otherwise Present.
constexpr Sio5v classifySio5v(const Sio5vSample* samples, std::size_t count) noexcept
{
    if (samples == nullptr || count < kSio5vSamples) {
        return Sio5v::Invalid;
    }
    int low = kMaxPinMv;
    int high = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Sio5vSample& s = samples[i];
        if (!s.ok || s.pinMv < 0 || s.pinMv > kMaxPinMv) {
            return Sio5v::Invalid;
        }
        low = s.pinMv < low ? s.pinMv : low;
        high = s.pinMv > high ? s.pinMv : high;
    }
    if (high - low > kMaxSpreadMv) {
        return Sio5v::Invalid;
    }
    return high < kAbsentBelowPinMv ? Sio5v::Absent : Sio5v::Present;
}

// The console order, once per boot. A refusal leaves it waiting: the owner can disconnect the cable and ask again.
class C8Trigger {
public:
    enum class Answer : uint8_t { Run, RefusedSio5v, RefusedInvalid, AlreadyRan };

    Answer request(Sio5v sio5v) noexcept
    {
        if (ran_) {
            return Answer::AlreadyRan;
        }
        if (sio5v == Sio5v::Present) {
            return Answer::RefusedSio5v;
        }
        if (sio5v != Sio5v::Absent) {
            return Answer::RefusedInvalid;
        }
        ran_ = true;  // before the run: a second order never starts while the first one is running
        return Answer::Run;
    }
    bool ran() const noexcept { return ran_; }

private:
    bool ran_ = false;
};

}  // namespace rb::bench
