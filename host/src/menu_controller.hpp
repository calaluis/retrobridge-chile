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
//   main      1 opens the local source (--images); 2..8 open a TNFS source
//             (menu-tnfs; without a loader they say the client is missing);
//             E edits the server of the source under the bar, or adds one
//             on an empty row (menu-editar-servidor); A boots the image
//             assigned to D1; U asks which drive to unmount.
//   edit      action 3: the 6502 edits the row by itself; its text comes with
//             text() and 'K' kMenuEditSave saves it in --sources; Esc cancels.
//   list      A..T choose an image of the local source; Esc goes back.
//   pick      after a choice, 1..8 is the drive; Esc cancels.
//   unmount   1..8 is the drive to empty; Esc cancels.
//   folder    a TNFS folder: A..T enter a folder or choose an image; Esc goes
//             up one level, and from the root back to main.
//   tnfs pick after an image, 1..8 is the drive; Esc goes back to the folder.
// D1 serves the menu until A: an image chosen for D1 is validated and assigned,
// and mounted when A is pressed. D2..D8 are mounted and unmounted at once
// through the mailbox (applied at the start of the next SioSession step).
//
// TNFS (menu-tnfs T012): listing a folder and bringing an image run on the
// loader thread (MenuLoader). key() starts a job and waits for it at most
// kLoadWaitMicros; if it has not ended, the reply is action 2 ("wait") with
// "CARGANDO nn%" under the key footer, the view stays where it was and the
// Atari polls with $FE. While a job runs, Esc cancels it and every other key is
// ignored. A TNFS image lives in a slot of the MemoryImageStore ("*tnfs*<n>");
// a slot the menu stops using goes back with MenuLoader::recycle (M41-20).
// $FF (redraw) and $FE (poll) are compared before the Shift/Control mask, so
// they never read as a key. key() does not allocate (ADR-011).

#include <array>
#include <cstddef>
#include <cstdint>

#include "menu_catalog.hpp"
#include "menu_image.hpp"
#include "menu_loader.hpp"
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

    MenuController(const MenuCatalog& catalog, MenuSources& sources, rb::app::ImageRepository& repository,
                   rb::app::InlineDriveMailbox& mailbox, MenuEvents* events, MenuLoader* loader = nullptr) noexcept
        : catalog_(catalog),
          sources_(sources),
          repository_(repository),
          mailbox_(mailbox),
          mount_(repository, mailbox),
          unmount_(mailbox),
          collect_(repository, mailbox),
          events_(events),
          loader_(loader)
    {
    }

    void onColdReset() noexcept;
    // The --sources file E rewrites (menu-editar-servidor); nullptr: E says there is none.
    void setSourcesFile(const char* path) noexcept { sourcesFile_ = path; }
    // Command 'T': character `ascii` at `position` of the text being edited. Writing
    // a position again gives the same text, so a retry of SIOV changes nothing.
    void text(uint8_t position, uint8_t ascii) noexcept;
    bool coldResetPending() const noexcept { return coldResetPending_; }
    bool menuOnD1() const noexcept { return menuOnD1_; }

    void service();
    // false if the listing block could not be reserved (main does not start then).
    bool ready() const noexcept { return folder_.ready(); }
    // `row`: the row the Atari's local bar shows (AUX2 bits 4-0), or kMenuNoRow. A key
    // first puts the cursor there, so a key the host does not use keeps the bar in place.
    MenuReply key(uint8_t code, uint8_t row = static_cast<uint8_t>(kMenuNoRow));

    // Name shown for drive 1..8 (for D1, the assigned image), or nullptr: the
    // catalog name of a local image, the display name of a TNFS one.
    const char* driveName(unsigned drive) const noexcept;

    // How long key() waits for a TNFS job before answering "wait" (plan §0:
    // every 'K' is answered within 250 ms).
    static constexpr uint32_t kLoadWaitMicros = 200000;

private:
    // A global entry of a list (local catalog or TNFS folder): 0..998 (M42-3).
    using EntryIndex = uint16_t;
    static constexpr EntryIndex kNoEntry = 0xFFFF;
    // The marked row of a list and its page (menu-navegacion); no allocation.
    struct Cursor {
        uint8_t row = 0;
        uint16_t page = 0;
    };
    static constexpr std::size_t kStackDepth = 32;  // folder levels remembered for Esc (M42-3)
    enum class View : uint8_t { Main, List, Pick, Unmount, Folder, FolderPick, Edit };
    enum class InFlight : uint8_t { None, Menu, Boot, Mount, Unmount };
    enum class Job : uint8_t { None, Open, Enter, Up, Fetch };

    // What a drive shows: nothing, an entry of the local catalog, or a TNFS
    // image in a slot of the MemoryImageStore.
    struct Shown {
        enum class Kind : uint8_t { None, Local, Tnfs };
        Kind kind = Kind::None;
        EntryIndex entry = kNoEntry;                   // Local
        std::size_t slot = 0;                          // Tnfs
        char name[TnfsFolder::kDisplayWidth + 1] = {};  // Tnfs: display name
        bool locked = true;                            // read-only: " SL" on the main screen
    };

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
    void startEdit();
    void onEdit(uint8_t code);
    void saveEdit();
    void deleteSource();
    // The edited text without the spaces after it, in `out` (kMenuEditWidth + 1 bytes).
    std::size_t editedText(char* out) const noexcept;
    bool boot();
    void assignD1(EntryIndex entry);
    void mountOn(unsigned drive, EntryIndex entry);
    void emptyDrive(unsigned drive);

    void onFolder(uint8_t code);
    void onFolderPick(uint8_t code, int digit);
    void startJob(Job job, unsigned source, const char* path, std::size_t length);
    MenuReply onLoading(uint8_t code);
    MenuReply advance();
    void cancelJob() noexcept;
    void listed();
    void fetched(const LoaderStatus& status);
    void failed(const LoaderStatus& status);
    void place(unsigned drive, const Shown& image);
    void recycle(const Shown& shown) noexcept;
    void renderFolder(uint8_t* screen, char* text, std::size_t size) const noexcept;
    // The entry a list key asks for: a letter of the visible page, or RETURN / fire on
    // the marked row; kNoEntry for anything else or past the end. A move ($40-$43)
    // changes the cursor and returns kNoEntry.
    static EntryIndex listKey(Cursor& cursor, std::size_t count, uint8_t key) noexcept;
    static void markRow(uint8_t* screen, const Cursor& cursor, std::size_t count) noexcept;
    // Rows of the page the cursor is on (0 for an empty list), and which pages there are
    // around it (kMenuPageBefore, kMenuPageAfter): what the 6502 needs to move the bar by
    // itself and to stay put, without SIO, when a move has no page to go to (N007).
    static std::size_t pageRows(const Cursor& cursor, std::size_t count) noexcept;
    static uint8_t pages(const Cursor& cursor, std::size_t count) noexcept;
    // The local cursor block at kMenuCursor: base of row 0, rows, marked row, pages.
    // rows 0: the 6502 sends every move to the host, as before N007.
    static void cursorBlock(uint8_t* screen, unsigned base, std::size_t rows, unsigned row, uint8_t pages) noexcept;
    // RETURN or fire on row `row` of the local cursor (kMenuConfirmRow + row): the
    // cursor of the shown screen goes there. false if the row is not on the screen.
    bool selectRow(unsigned row) noexcept;
    // Up and down move `line` (0..7) on the main screen; left and right are taken and do
    // nothing there. false for any other key.
    static bool moveLine(uint8_t& line, uint8_t key) noexcept;
    // The digit a key means while a drive is chosen: its own, or the drive under the
    // cursor for RETURN and fire; 0 for none.
    int driveDigit(uint8_t key, int digit) const noexcept;
    void pushPosition() noexcept;
    Cursor popPosition() noexcept;

    const MenuCatalog& catalog_;
    MenuSources& sources_;
    rb::app::ImageRepository& repository_;
    rb::app::InlineDriveMailbox& mailbox_;
    rb::app::MountDiskImage mount_;
    rb::app::UnmountDiskImage unmount_;
    rb::app::CollectDriveReply collect_;
    MenuEvents* events_;
    MenuLoader* loader_;

    View view_ = View::Main;
    EntryIndex chosen_ = kNoEntry;         // catalog entry (or folder entry) waiting for a drive
    uint8_t sourceLine_ = 0;               // main screen: the source marked (0: source 1)
    uint8_t driveLine_ = 0;                // drive choice (Pick, FolderPick, Unmount): 0 is D1 (M42-4)
    Cursor listCursor_;                    // the local list
    Cursor folderCursor_;                  // the TNFS folder on screen
    Cursor stack_[kStackDepth];            // folderCursor_ of each folder entered (Esc restores it)
    std::size_t depth_ = 0;                // levels entered; past kStackDepth, Esc restores row 1
    std::array<Shown, kDrives> shown_{};   // per drive (D1: assigned)
    InFlight inFlight_ = InFlight::None;

    Shown previous_;                       // what the drive in flight showed before
    Shown incoming_;                       // what it shows if the request is applied

    // The TNFS folder on screen: its source, its path and its listing.
    unsigned source_ = 0;
    char path_[kTnfsMaxPath + 1] = {};
    std::size_t pathLength_ = 0;
    TnfsFolder folder_;

    // The TNFS job in progress (job_ None: none).
    Job job_ = Job::None;
    uint32_t jobNumber_ = 0;
    unsigned jobSource_ = 0;
    char jobPath_[kTnfsMaxPath + 1] = {};
    std::size_t jobPathLength_ = 0;
    unsigned jobDrive_ = 0;
    char jobName_[TnfsFolder::kDisplayWidth + 1] = {};
    uint8_t percent_ = 0;

    // E (menu-editar-servidor): the source edited (2..8) and the field the 6502 sends.
    const char* sourcesFile_ = nullptr;
    unsigned editSource_ = 0;
    std::array<char, kMenuEditWidth> editText_{};

    std::array<char, kMenuLineWidth + 1> message_{};
    bool coldResetPending_ = false;
    bool menuOnD1_ = false;
};

}  // namespace rb::host
