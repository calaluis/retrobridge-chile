#pragma once

// menu-sio test package (menu-sio-fisico T5): the RetroBridge menu served as D1
// over the physical SIO of the FujiNet 1.3. Only built with CONFIG_RB_MENU_SIO.
// H4 and M3a stay unsigned: this package transmits on DATA IN only under the
// owner's written risk acceptance, with the SIO adapter that has no pin 8
// (docs/hardware/mitigacion-c-motor-pin8.md).
//
// menu-cinta T6: the cassette service starts after D1 is served (it reads tapes into PSRAM), and
// the menu gets its tape deck before the drives are restored, so the tape remembered on C: comes
// back too. The cassette never drives DATA IN here: EspDataIn is not configured and its outputs are
// discarded. T7: the SIO task steps the DATA IN arbiter between frames (MOTOR read only with M3a,
// COMMAND on its input pin, power-off from the +5 V watch); it stays inert, the disk keeps DATA IN,
// until the cassette can take the line while it runs (T9, after M3a and H4).

#include "rb/board/board_profile.hpp"
#include "rb/esp/cassette_service.hpp"
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
// With the microSD mounted it also starts `cassette` (logging the free PSRAM before and
// after) and hands the menu its tape deck; a cassette that cannot start leaves a deck
// that answers "no memory" (plan §4, R3). `motor` is read by the arbiter (off without M3a).
// Does nothing if start() failed.
void finish(bool sdMounted, const rb::hwdiag::BootRecord& boot, rb::esp::CassetteService& cassette,
            rb::esp::EspMotorIn& motor);

}  // namespace rb::menusio
