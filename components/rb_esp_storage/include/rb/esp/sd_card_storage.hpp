#pragma once

#include <cstdint>

#include "rb/board/board_profile.hpp"
#include "rb/core/result.hpp"

struct rb_sd_card;

namespace rb::esp {

// Mounts the microSD as FAT at kMountPoint in the ESP-IDF VFS (H3b). Used only
// from the storage context (ADR-014).
//
// Safety (H3-11): mount() validates the board profile before anything else.
// With an unconfirmed board it returns BoardNotConfirmed and initialises no
// bus, no GPIO and no driver. Cards are never formatted (H3-5). The caller must
// unmount only when no image file is open (repository open_count() == 0).
class SdCardStorage {
public:
    static constexpr const char* kMountPoint = "/sd";
    static constexpr int kMaxOpenFiles = 4;  // H3-4: 3 slots + 1 spare

    SdCardStorage() = default;
    SdCardStorage(const SdCardStorage&) = delete;
    SdCardStorage& operator=(const SdCardStorage&) = delete;

    // BoardNotConfirmed or InvalidPinAssignment (nothing touched), IoError
    // (driver or mount failure; see lastEspError()). Mounting twice is a no-op.
    rb::core::Result<void> mount(const rb::board::BoardProfile& profile);

    // StorageNotMounted if nothing is mounted.
    rb::core::Result<void> unmount();

    bool mounted() const noexcept { return card_ != nullptr; }
    int lastEspError() const noexcept { return lastEspError_; }
    uint8_t pinWarnings() const noexcept { return pinWarnings_; }

private:
    rb_sd_card* card_ = nullptr;
    int lastEspError_ = 0;
    uint8_t pinWarnings_ = 0;
};

}  // namespace rb::esp
