#pragma once

// Storage-context side of the menu (Issues #38 and #40, ADR-017). Runs in the
// same thread as SioSession, strictly between or inside its steps:
//   * onColdReset(): called by the NetSIO observer when a COLD_RESET (FF)
//     arrives; D1 must go back to the menu before the next command is served,
//     and the menu starts again on its main screen;
//   * service(): called after every step; collects what the last request did
//     and, if a cold reset is pending and the mailbox is free, mounts the menu;
//   * key(): answers the menu's 'K' command. The Atari only draws: this class
//     decides what each key means and renders the whole screen.
//
// Screens and keys (keyboard codes, Shift and Control ignored):
//   main      1 opens the local source (--images); 2..8 and E show what is
//             missing (no TNFS client, no text input yet); A boots the image
//             assigned to D1; U asks which drive to unmount.
//   list      A..T choose an image of the local source; Esc goes back.
//   pick      after a choice, 1..8 is the drive; Esc cancels.
//   unmount   1..8 is the drive to empty; Esc cancels.
// D1 serves the menu until A: an image chosen for D1 is validated and assigned,
// and mounted when A is pressed. D2..D8 are mounted and unmounted at once
// through the mailbox (applied at the start of the next SioSession step).

#include <array>
#include <cstddef>
#include <cstdint>

#include "menu_catalog.hpp"
#include "menu_image.hpp"
#include "menu_sources.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/app/usecases/collect_drive_reply.hpp"
#include "rb/app/usecases/mount_disk_image.hpp"
#include "rb/app/usecases/unmount_disk_image.hpp"

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

// Reply to 'K': action, layout and the screen in screen codes.
struct MenuReply {
    static constexpr std::size_t kSize = kMenuReplySize;
    std::array<uint8_t, kSize> bytes{};

    uint8_t action() const noexcept { return bytes[0]; }
    uint8_t layout() const noexcept { return bytes[1]; }
    const uint8_t* screen() const noexcept { return bytes.data() + 2; }
};

class MenuController {
public:
    static constexpr unsigned kDrives = 8;

    MenuController(const MenuCatalog& catalog, const MenuSources& sources, rb::app::ImageRepository& repository,
                   rb::app::InlineDriveMailbox& mailbox, MenuEvents* events) noexcept
        : catalog_(catalog),
          sources_(sources),
          repository_(repository),
          mailbox_(mailbox),
          mount_(repository, mailbox),
          unmount_(mailbox),
          collect_(repository, mailbox),
          events_(events)
    {
        shown_.fill(kNone);
    }

    void onColdReset() noexcept;
    bool coldResetPending() const noexcept { return coldResetPending_; }
    bool menuOnD1() const noexcept { return menuOnD1_; }

    void service();
    MenuReply key(uint8_t code);

    // Catalog name shown for drive 1..8 (for D1, the assigned image), or nullptr.
    const char* driveName(unsigned drive) const noexcept;

private:
    static constexpr int8_t kNone = -1;
    enum class View : uint8_t { Main, List, Pick, Unmount };
    enum class InFlight : uint8_t { None, Menu, Boot, Mount, Unmount };

    void collect();
    void event(const char* name, const char* detail);
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    void say(const char* format, ...) noexcept;
    MenuReply render(uint8_t action) const noexcept;

    void onMain(int digit, uint8_t code);
    void onList(uint8_t code);
    void onPick(int digit);
    void onUnmount(int digit);
    bool boot();
    void assignD1(int8_t entry);
    void mountOn(unsigned drive, int8_t entry);
    void emptyDrive(unsigned drive);

    const MenuCatalog& catalog_;
    const MenuSources& sources_;
    rb::app::ImageRepository& repository_;
    rb::app::InlineDriveMailbox& mailbox_;
    rb::app::MountDiskImage mount_;
    rb::app::UnmountDiskImage unmount_;
    rb::app::CollectDriveReply collect_;
    MenuEvents* events_;

    View view_ = View::Main;
    int8_t chosen_ = kNone;                // catalog entry waiting for a drive
    std::array<int8_t, kDrives> shown_{};  // catalog entry per drive (D1: assigned)
    InFlight inFlight_ = InFlight::None;

    int8_t previous_ = kNone;              // what the drive in flight showed before
    std::array<char, kMenuLineWidth + 1> message_{};
    bool coldResetPending_ = false;
    bool menuOnD1_ = false;
};

}  // namespace rb::host
