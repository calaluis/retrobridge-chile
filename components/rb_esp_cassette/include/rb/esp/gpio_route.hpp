#pragma once

// Who drives a pin, read from the ESP32's GPIO matrix: the output signal routed to it and whether its
// output is enabled. First written for the bench test C8 (main/bench_c8.cpp); shared since menu-cinta
// T9a.2, where the cassette checks DATA IN is back on UART2 TX before it gives the line to the disk.

#include <cstdint>

namespace rb::esp {

struct OutputRoute {
    uint32_t signal = 0;   // GPIO_FUNCn_OUT_SEL (U2TXD_OUT_IDX, RMT_SIG_OUTn_IDX, 256 = plain GPIO...)
    bool enabled = false;  // the pin's output enable
};

// A pin outside 0..31 reads as {0, false}.
OutputRoute readOutputRoute(int pin) noexcept;
uint32_t uart2TxSignal() noexcept;  // U2TXD_OUT_IDX

// DATA IN is driven by UART2 TX: its output is enabled and the matrix routes U2TXD to it.
inline bool drivenByUart2Tx(int pin) noexcept
{
    const OutputRoute route = readOutputRoute(pin);
    return route.enabled && route.signal == uart2TxSignal();
}

}  // namespace rb::esp
