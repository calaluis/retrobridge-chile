#pragma once

// Diagnostics of one take of DATA IN by the tape (fix-cinta-data-in-65xe, review PR #124 H1). Pure
// logic, tested on the host. The SIO task takes the hardwareFree() counters when a take is asked and
// again when it ends (confirmed or expired); only the difference says what the cassette did for THIS
// take. A reading left over from an earlier take is never this take's answer.

#include <cstdint>

namespace rb::menusio {

// EspDataIn's hardwareFree() counters, as read from another task. All counters wrap at 2^32.
struct HardwareProbe {
    uint32_t calls = 0;     // hardwareFree() calls, true or false
    uint32_t refusals = 0;  // calls that returned false
    uint32_t lastBusy = 0;  // the busy bits of the last call (0: free)
};

enum class TakeVerdict {
    NotChecked,        // the cassette did not call hardwareFree() during the take
    Busy,              // it did, and the last call found something busy (lastBusy)
    FreeNotConfirmed,  // it did, the last call found it free, and the take was still not confirmed
};

class TakeDiag {
public:
    struct Result {
        bool hasRequest = false;  // false: no take was seen asked (e.g. confirmed in the same step)
        int64_t elapsedUs = 0;    // since the request; only with hasRequest
        uint32_t calls = 0;       // hardwareFree() calls during the take
        uint32_t refusals = 0;    // of them, false
        uint32_t lastBusy = 0;    // the last call's bits (meaningful only when calls > 0)
        TakeVerdict verdict = TakeVerdict::NotChecked;
    };

    void requested(int64_t nowUs, const HardwareProbe& probe) noexcept
    {
        pending_ = true;
        askedUs_ = nowUs;
        start_ = probe;
    }

    // The take ended (confirmed or expired). Consumes the request.
    Result ended(int64_t nowUs, const HardwareProbe& probe) noexcept
    {
        Result r;
        r.hasRequest = pending_;
        if (pending_) {
            r.elapsedUs = nowUs - askedUs_;
            r.calls = probe.calls - start_.calls;           // modulo 2^32
            r.refusals = probe.refusals - start_.refusals;  // modulo 2^32
        }
        r.lastBusy = probe.lastBusy;
        if (r.calls == 0) {
            r.verdict = TakeVerdict::NotChecked;
        } else {
            r.verdict = probe.lastBusy != 0 ? TakeVerdict::Busy : TakeVerdict::FreeNotConfirmed;
        }
        pending_ = false;
        return r;
    }

private:
    bool pending_ = false;
    int64_t askedUs_ = 0;
    HardwareProbe start_{};
};

inline const char* takeVerdictName(TakeVerdict v) noexcept
{
    switch (v) {
    case TakeVerdict::NotChecked:
        return "la casetera no miro el hardware";
    case TakeVerdict::Busy:
        return "hardware ocupado";
    case TakeVerdict::FreeNotConfirmed:
        return "hardware libre sin confirmar";
    }
    return "?";
}

}  // namespace rb::menusio
