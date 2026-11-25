#pragma once
// Bench test C8 (H4-C, docs/hardware/h4-c-casetera.md): DATA IN on the FujiNet alone, USB only, the
// Atari disconnected. Built only with CONFIG_RB_BENCH_C8; no CI package builds it since 2026-11-25
// (recovery: docs/hardware/procedimiento-prueba-banco-c8.md). The grant ignores the gates.
#include "rb/board/board_profile.hpp"
#include "rb/esp/esp_data_in.hpp"

namespace rb::bench {

// Runs once: GPIO21 to UART2, a run that plays to its end, a run cancelled with stop(), then
// release(). Logs every ESP-IDF call of the driver with its result and the GPIO matrix route of
// the pin after each step. Leaves `dataIn` released (enabled() false). True when every step passed.
bool run_c8(const rb::board::BoardProfile& profile, rb::esp::EspDataIn& dataIn) noexcept;

}  // namespace rb::bench
