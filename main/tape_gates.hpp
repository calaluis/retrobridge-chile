#pragma once

// Whether the menu may offer a mounted tape for loading (spec puertas-aceptacion-riesgo, T3b). The board
// gates (H4 and M3a, both needed) are not enough on their own: until T9b the tape does not leave by the SIO
// (kTapeOutput in menu_sio.cpp), so even with both gates signed and the output off the menu must still say
// «AUN NO PUEDE CARGARSE» instead of «ENCIENDE CON START». Header only, so the host tests check it alone.

#include "rb/board/board_profile.hpp"

namespace rb::menusio {

// tapeOutput: the build carries the owner's acceptance for DATA IN (CONFIG_RB_MENU_SIO_TAPE_OUTPUT). The grant
// is a permission, not a capability: motorReady and dataInDriven say what start-up really configured
// (EspMotorIn::configure, CassetteService::shareDataIn). Without either the menu says «AUN NO PUEDE CARGARSE».
inline bool tapeGatesOpen(bool tapeOutput, const rb::board::BoardProfile& board, bool motorReady,
                          bool dataInDriven) noexcept
{
    return tapeOutput && motorReady && dataInDriven &&
           rb::board::validate_for_menu_cassette_output(board, tapeOutput).ok();
}

}  // namespace rb::menusio
