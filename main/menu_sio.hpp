#pragma once

// menu-sio test package (menu-sio-fisico T5): the RetroBridge menu served as D1
// over the physical SIO of the FujiNet 1.3. Only built with CONFIG_RB_MENU_SIO.
// H4 and M3a stay unsigned: this package transmits on DATA IN only under the
// owner's written risk acceptance, with the SIO adapter that has no pin 8
// (docs/hardware/mitigacion-c-motor-pin8.md). The cassette is not started.

#include "rb/board/board_profile.hpp"

namespace rb::menusio {

// Reads the catalog of the microSD root (if mounted), starts the SIO line and
// the SIO task, and registers the console commands `menu` and `sio`. Call after
// the diagnostic console has started. Returns false if the line could not start.
bool start(const rb::board::BoardProfile& board, bool sdMounted);

}  // namespace rb::menusio
