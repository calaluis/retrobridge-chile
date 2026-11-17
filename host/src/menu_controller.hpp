#pragma once

// Storage-context side of the boot menu (Issue #38, ADR-017). Runs in the same
// thread as SioSession, strictly between or inside its steps:
//   * onColdReset(): called by the NetSIO observer when a COLD_RESET (FF)
//     arrives; D1 must go back to the menu before the next command is served;
//   * service(): called after every step; closes what the last mount displaced
//     and, if a cold reset is pending and the mailbox is free, mounts the menu;
//   * select(): answers the menu's 'M' command; validates the chosen image with
//     MountDiskImage (DiskImage::open) and, if valid, hands it to D1. The mount
//     is applied at the start of the next SioSession step, before any frame.

#include <array>
#include <cstddef>
#include <cstdint>

#include "menu_catalog.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/app/usecases/collect_drive_reply.hpp"
#include "rb/app/usecases/mount_disk_image.hpp"

namespace rb::host {

class MenuEvents {
public:
    MenuEvents(const MenuEvents&) = delete;
    MenuEvents& operator=(const MenuEvents&) = delete;
    virtual void menuEvent(const char* name, const char* detail) = 0;

protected:
    MenuEvents() = default;
    ~MenuEvents() = default;
};

// Reply to 'M': status byte (0 mounted, 1 refused) and one 40-column screen line.
struct MenuReply {
    static constexpr std::size_t kSize = 41;
    std::array<uint8_t, kSize> bytes{};
};

class MenuController {
public:
    MenuController(const MenuCatalog& catalog, rb::app::ImageRepository& repository,
                   rb::app::InlineDriveMailbox& mailbox, MenuEvents* events) noexcept
        : catalog_(catalog), mailbox_(mailbox), mount_(repository, mailbox), collect_(repository, mailbox),
          events_(events)
    {
    }

    void onColdReset() noexcept { coldResetPending_ = true; }
    bool coldResetPending() const noexcept { return coldResetPending_; }
    bool menuOnD1() const noexcept { return menuOnD1_; }

    void service();
    MenuReply select(uint8_t index);

private:
    enum class InFlight : uint8_t { None, Menu, Image };

    void collect();
    void event(const char* name, const char* detail);

    const MenuCatalog& catalog_;
    rb::app::InlineDriveMailbox& mailbox_;
    rb::app::MountDiskImage mount_;
    rb::app::CollectDriveReply collect_;
    MenuEvents* events_;
    InFlight inFlight_ = InFlight::None;
    bool coldResetPending_ = false;
    bool menuOnD1_ = false;
};

}  // namespace rb::host
