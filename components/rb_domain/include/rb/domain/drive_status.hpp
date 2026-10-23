#pragma once

#include "rb/domain/density.hpp"

namespace rb::domain {

// Snapshot of what the computer can observe about a drive (STATUS command).
// Domain terms only: the 4-byte SIO encoding belongs to the SIO context (H2, D-5).
class DriveStatus {
public:
    constexpr DriveStatus(bool diskPresent, bool writeProtected, bool lastOperationFailed, Density density) noexcept
        : diskPresent_(diskPresent),
          writeProtected_(writeProtected),
          lastOperationFailed_(lastOperationFailed),
          density_(density)
    {
    }

    constexpr bool diskPresent() const noexcept { return diskPresent_; }
    constexpr bool writeProtected() const noexcept { return writeProtected_; }
    constexpr bool lastOperationFailed() const noexcept { return lastOperationFailed_; }
    constexpr Density density() const noexcept { return density_; }

    friend constexpr bool operator==(const DriveStatus& a, const DriveStatus& b) noexcept
    {
        return a.diskPresent_ == b.diskPresent_ && a.writeProtected_ == b.writeProtected_ &&
               a.lastOperationFailed_ == b.lastOperationFailed_ && a.density_ == b.density_;
    }
    friend constexpr bool operator!=(const DriveStatus& a, const DriveStatus& b) noexcept { return !(a == b); }

private:
    bool diskPresent_;
    bool writeProtected_;
    bool lastOperationFailed_;
    Density density_;
};

}  // namespace rb::domain
