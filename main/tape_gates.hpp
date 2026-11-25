#pragma once

// Whether the menu may offer a mounted tape for loading (spec puertas-aceptacion-riesgo, T3b). The board
// gates (H4 and M3a) are not enough on their own: until T9b the tape does not leave by the SIO
// (kTapeOutput in menu_sio.cpp), so with the gates signed and the output off the menu must still say
// «AUN NO PUEDE CARGARSE» instead of «ENCIENDE CON START». Header only, so the host tests check it alone.

#include "rb/board/board_profile.hpp"

namespace rb::menusio {

inline bool tapeGatesOpen(bool tapeOutput, const rb::board::BoardProfile& board) noexcept
{
    return tapeOutput && rb::board::validate_for_cassette_output(board).ok();
}

}  // namespace rb::menusio
