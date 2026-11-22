#pragma once

// Short reason for an errno of the microSD (menu-unidades-persistentes): what the menu shows after
// "NO SE GUARDO:" or "MEMORIA:". Header-only so the controller does not depend on the slot store.

#include <cerrno>

namespace rb::host {

inline const char* storageReason(int error) noexcept
{
    switch (error) {
    case ENOSPC:
        return "TARJETA LLENA";
    case EROFS:
    case EACCES:
    case EPERM:
        return "SOLO LECTURA";
    case ENOENT:
    case ENODEV:
    case ENXIO:
        return "SIN TARJETA";
    case ENAMETOOLONG:
        return "RUTA LARGA";
    default:
        return "ERROR DE TARJETA";
    }
}

}  // namespace rb::host
