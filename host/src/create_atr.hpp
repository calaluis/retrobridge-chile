#pragma once

// Creates a blank ATR in the --images folder (menu-formatear-disco, F003):
// "<NAME>.ATR" with a 16-byte header and every sector at $00, unformatted; the
// Atari's DOS formats it afterwards. Single density is 720 x 128, enhanced
// density 1040 x 128 (M46-2). Host only: the menu creates files, the SIO path
// never does.

#include <cstddef>
#include <cstdint>

#include "rb/domain/density.hpp"

namespace rb::host {

constexpr std::size_t kMaxNewAtrName = 25;  // fits the drive line with ".ATR" (M46-3)

enum class CreateAtrResult : uint8_t {
    Created,
    InvalidName,     // not 1-25 characters of A-Z, 0-9, '-' and '_'
    InvalidDensity,  // neither Single nor Enhanced
    AlreadyExists,   // a file with that name is there: it is not touched
    IoError,         // the folder or the disk refused; nothing is left behind
};

// The name typed in the menu, without ".ATR": 1 to 25 characters of A-Z, 0-9,
// '-' and '_' (M46-3). Nothing else, so it cannot leave the folder. Windows
// device names (CON, PRN, AUX, NUL, COM0-9, LPT0-9) are refused too.
bool isNewAtrName(const char* name) noexcept;

// Writes "<folder>/<name>.ATR". The file is opened for exclusive creation, so an
// existing file is never overwritten; a failed write removes what was created.
CreateAtrResult createAtr(const char* folder, const char* name, rb::domain::Density density) noexcept;

}  // namespace rb::host
