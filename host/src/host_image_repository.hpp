#pragma once

// ImageRepository of retrobridge_host with --images (Issues #38 and #40, menu-tnfs):
// the reserved location kMenuLocation is the in-memory menu disk; "*tnfs*<n>" is
// slot n of the MemoryImageStore (images brought over TNFS, M41-2); every other
// location is a file of the images folder, opened by StdioImageRepository.
// kFileSlots: one image per drive D1..D8 and one more being validated or
// mounted while the drive still holds the image it displaces.
// Files open for writing when they allow it and no other drive writes the same
// one (menu-escritura-disco); the menu and TNFS images are never writable.

#include <cstddef>
#include <cstring>

#include "memory_image_store.hpp"
#include "menu_image.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/storage/stdio_image_repository.hpp"

namespace rb::host {

// '*' cannot appear in a Windows file name and the catalog only holds names
// ending in ".atr", so this location never names a file of the folder.
constexpr const char* kMenuLocation = "*menu*";

class HostImageRepository final : public rb::app::ImageRepository {
public:
    static constexpr std::size_t kFileSlots = 8 + 1;

    HostImageRepository(const char* folder, MenuImage& menu) : files_(folder, true), menu_(menu) {}

    std::size_t openFiles() const noexcept { return files_.open_count(); }
    bool menuOpen() const noexcept { return menuOpen_; }
    MemoryImageStore& memory() noexcept { return memory_; }

protected:
    rb::core::Result<rb::domain::ImageByteSource*> doOpen(const rb::app::ImageLocation& location) override
    {
        if (isTnfsLocation(location.c_str())) {
            std::size_t slot = 0;
            if (!parseTnfsLocation(location.c_str(), slot)) {
                return rb::core::Failure{rb::core::ErrorCode::NotFound};
            }
            return memory_.open(slot);
        }
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
        } else if (memory_.owns(source)) {
            memory_.close(source);
        } else {
            files_.close(&source);
        }
    }

private:
    rb::storage::StdioImageRepository<kFileSlots> files_;
    MenuImage& menu_;
    MemoryImageStore memory_;
    bool menuOpen_ = false;
};

}  // namespace rb::host
