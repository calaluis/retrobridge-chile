#include "rb/esp/sd_card_storage.hpp"

#include "esp_err.h"
#include "sd_card_driver.h"

namespace rb::esp {

using rb::board::BoardProfile;
using rb::board::SdInterface;
using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;

Result<void> SdCardStorage::mount(const BoardProfile& profile)
{
    // First and unconditionally: an unconfirmed or invalid board never reaches a driver.
    const Result<uint8_t> valid = rb::board::validate_for_storage(profile);
    if (!valid.ok()) {
        return Failure{valid.error()};
    }
    pinWarnings_ = valid.value();
    if (card_ != nullptr) {
        return {};
    }

    esp_err_t err = ESP_FAIL;
    switch (profile.sdInterface) {
    case SdInterface::Spi:
        err = rb_sd_mount_spi(kMountPoint, profile.spi.sck, profile.spi.mosi, profile.spi.miso, profile.spi.cs,
                              profile.cardDetect, kMaxOpenFiles, &card_);
        break;
    case SdInterface::Sdmmc1Bit:
        err = rb_sd_mount_sdmmc(kMountPoint, 1, kMaxOpenFiles, &card_);
        break;
    case SdInterface::Sdmmc4Bit:
        err = rb_sd_mount_sdmmc(kMountPoint, 4, kMaxOpenFiles, &card_);
        break;
    case SdInterface::None:
    default:
        return Failure{ErrorCode::InvalidPinAssignment};  // unreachable: rejected by the validator
    }
    lastEspError_ = err;
    if (err != ESP_OK) {
        card_ = nullptr;
        return Failure{ErrorCode::IoError};
    }
    return {};
}

Result<void> SdCardStorage::unmount()
{
    if (card_ == nullptr) {
        return Failure{ErrorCode::StorageNotMounted};
    }
    const esp_err_t err = rb_sd_unmount(kMountPoint, card_);
    card_ = nullptr;  // the handle is released even if the VFS reports an error
    lastEspError_ = err;
    return err == ESP_OK ? Result<void>{} : Result<void>{Failure{ErrorCode::IoError}};
}

}  // namespace rb::esp
