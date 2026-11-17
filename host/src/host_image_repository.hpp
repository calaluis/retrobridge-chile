#pragma once

// ImageRepository of retrobridge_host with --images (Issue #38): the reserved
// location kMenuLocation is the in-memory menu disk; every other location is a
// file of the images folder, opened by StdioImageRepository. Two file slots: the
// image mounted as D1 and the one being validated after a choice.

#include <cstring>

#include "menu_image.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/storage/stdio_image_repository.hpp"

namespace rb::host {

// '*' cannot appear in a Windows file name and the catalog only holds names
// ending in ".atr", so this location never names a file of the folder.
constexpr const char* kMenuLocation = "*menu*";

class HostImageRepository final : public rb::app::ImageRepository {
public:
    HostImageRepository(const char* folder, MenuImage& menu) : files_(folder), menu_(menu) {}

    std::size_t openFiles() const noexcept { return files_.open_count(); }
    bool menuOpen() const noexcept { return menuOpen_; }

protected:
    rb::core::Result<rb::domain::ImageByteSource*> doOpen(const rb::app::ImageLocation& location) override
    {
        if (std::strcmp(location.c_str(), kMenuLocation) != 0) {
            return files_.open(location);
        }
        if (menuOpen_) {
            return rb::core::Failure{rb::core::ErrorCode::NoFreeSlot};
        }
        menuOpen_ = true;
        return static_cast<rb::domain::ImageByteSource*>(&menu_);
    }

    void doClose(rb::domain::ImageByteSource& source) override
    {
        if (&source == &menu_) {
            menuOpen_ = false;
        } else {
            files_.close(&source);
        }
    }

private:
    rb::storage::StdioImageRepository<2> files_;
    MenuImage& menu_;
    bool menuOpen_ = false;
};

}  // namespace rb::host
