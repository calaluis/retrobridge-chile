#pragma once
// Bench test C8 (H4-C, docs/hardware/h4-c-casetera.md): DATA IN on the FujiNet alone, USB only, the
// Atari disconnected. Built only with CONFIG_RB_BENCH_C8; H4 and M3a stay unsigned.
#include "rb/board/board_profile.hpp"
#include "rb/esp/esp_data_in.hpp"

namespace rb::bench {

// Runs once: GPIO21 to UART2, a run that plays to its end, a run cancelled with stop(), then
// release(). Logs every ESP-IDF call of the driver with its result and the GPIO matrix route of
// the pin after each step. Leaves `dataIn` released (enabled() false). True when every step passed.
bool run_c8(const rb::board::BoardProfile& profile, rb::esp::EspDataIn& dataIn) noexcept;

}  // namespace rb::bench
