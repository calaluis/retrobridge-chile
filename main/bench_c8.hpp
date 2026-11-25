#pragma once
// Bench test C8 (H4-C, docs/hardware/h4-c-casetera.md): DATA IN on the FujiNet alone, USB only, the
// Atari disconnected. Built only with CONFIG_RB_BENCH_C8; the CI job firmware-package-bench-c8a builds and
// verifies it, not distributed (C8a F3; docs/hardware/banco-c8a-aislado.md). The grant ignores the gates.
#include "rb/board/board_profile.hpp"
#include "rb/esp/esp_data_in.hpp"

namespace rb::bench {

// Runs once: GPIO21 to UART2, a run that plays to its end, a run cancelled with stop(), then
// release(). Logs every ESP-IDF call of the driver with its result and the GPIO matrix route of
// the pin after each step. Leaves `dataIn` released (enabled() false). True when every step passed.
bool run_c8(const rb::board::BoardProfile& profile, rb::esp::EspDataIn& dataIn) noexcept;

// Bench C8a, phase F1 (main/bench_c8_console.cpp): registers the console order `c8a run`, which reads the
// SIO +5 V on GPIO35, asks the gate (bench_c8_gate.hpp) and only then calls run_c8. Never runs the bench itself.
bool start_c8_command(const rb::board::BoardProfile& profile, rb::esp::EspDataIn& dataIn) noexcept;

}  // namespace rb::bench
