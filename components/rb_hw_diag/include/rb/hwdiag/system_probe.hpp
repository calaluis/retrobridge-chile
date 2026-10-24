#pragma once

#include "rb/hwdiag/diag_types.hpp"

namespace rb::hwdiag {

// Outbound port: the platform's view of itself (uptime, reset history, heap,
// watchdog configuration, firmware identity). EspSystemProbe on the ESP32;
// fixed values in the host tests. Reads internal state only: it does not
// measure anything electrical on the board.
class SystemProbe {
public:
    SystemProbe(const SystemProbe&) = delete;
    SystemProbe& operator=(const SystemProbe&) = delete;

    virtual SystemSnapshot capture() const = 0;

protected:
    SystemProbe() = default;
    ~SystemProbe() = default;  // never deleted through the interface
};

}  // namespace rb::hwdiag
