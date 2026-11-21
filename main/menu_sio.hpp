#pragma once

// menu-sio test package (menu-sio-fisico T5): the RetroBridge menu served as D1
// over the physical SIO of the FujiNet 1.3. Only built with CONFIG_RB_MENU_SIO.
// H4 and M3a stay unsigned: this package transmits on DATA IN only under the
// owner's written risk acceptance, with the SIO adapter that has no pin 8
// (docs/hardware/mitigacion-c-motor-pin8.md). The cassette is not started.

#include "rb/board/board_profile.hpp"
#include "rb/hwdiag/diag_types.hpp"

namespace rb::menusio {

// Starts the SIO line, the menu as D1 and the SIO task, before the microSD is
// mounted: powered only from the SIO, the board starts with the 65XE and D1 must
// answer its first boot attempt. The catalog starts empty. Returns false if the
// line could not start.
bool start(const rb::board::BoardProfile& board);

// After the microSD mount and the diagnostic console: reads the catalog of the
// microSD root (the SIO task takes it between two frames), registers the console
// commands `menu` and `sio`, and appends a boot record to the microSD ~10 s later.
// Does nothing if start() failed.
void finish(bool sdMounted, const rb::hwdiag::BootRecord& boot);

}  // namespace rb::menusio
