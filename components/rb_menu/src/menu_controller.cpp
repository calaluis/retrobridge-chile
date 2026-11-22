#include "rb/menu/menu_controller.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>

#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/memory_image_store.hpp"
#include "rb/menu/storage_reason.hpp"
#include "rb/app/image_location.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/domain/drive_number.hpp"

namespace rb::host {

using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::DriveNumber;

static_assert(kListMaxEntries == TnfsFolder::kPageRows, "a page of the local list as long as a TNFS one");

namespace {

// Atari keyboard codes (CH, Shift and Control bits cleared).
constexpr uint8_t kDigitCodes[MenuController::kDrives] = {0x1F, 0x1E, 0x1A, 0x18, 0x1D, 0x1B, 0x33, 0x35};  // 1..8
constexpr uint8_t kLetterCodes[kListMaxEntries] = {
    0x3F, 0x15, 0x12, 0x3A, 0x2A, 0x38, 0x3D, 0x39, 0x0D, 0x01,  // A B C D E F G H I J
    0x05, 0x00, 0x25, 0x23, 0x08, 0x0A, 0x2F, 0x28, 0x3E, 0x2D,  // K L M N O P Q R S T
};
constexpr uint8_t kKeyA = 0x3F;
constexpr uint8_t kKeyE = 0x2A;
constexpr uint8_t kKeyU = 0x0B;
constexpr uint8_t kKeyN = 0x23;
constexpr uint8_t kKeyS = 0x3E;
constexpr uint8_t kKeyM = 0x25;
// The new name's field in the message row, after "NOMBRE:" (menu-formatear-disco). The
// 6502 keeps its cursor one column past the text: 7 + 32 ends on the row's last column.
constexpr unsigned kCreateLabel = 7;
constexpr const char* kCreateInvalid = " NOMBRE NO VALIDO: LETRAS, 0-9 Y -";
constexpr uint8_t kKeyEsc = 0x1C;
constexpr uint8_t kKeyReturn = 0x0C;
constexpr std::size_t kPageRows = TnfsFolder::kPageRows;

bool isNavigation(uint8_t code) noexcept { return code >= kMenuNavUp && code <= kMenuNavFire; }
// RETURN or fire on a row of the local cursor (N007): kMenuConfirmRow + row.
bool isConfirmRow(uint8_t code) noexcept
{
    return code >= kMenuConfirmRow && code < kMenuConfirmRow + kListMaxEntries;
}
static_assert(kMenuConfirmRow > kMenuNavFire && kMenuConfirmRow + kListMaxEntries <= kMenuPollKey,
              "confirm codes apart from keys, moves, poll and redraw");
static_assert(kMenuCursor >= kListMessage + kMenuLineWidth && kMenuCursor >= kMainMessage + kMenuLineWidth &&
                  kMenuCursor + 5 <= kMenuScreenSize,
              "the cursor block past every layout");
bool isConfirm(uint8_t key) noexcept { return key == kKeyReturn || key == kMenuNavFire; }

// Marks one 40-column line of the screen in inverse video, edge to edge.
void invert(uint8_t* screen, unsigned offset) noexcept
{
    for (unsigned i = 0; i < kMenuLineWidth; ++i) {
        screen[offset + i] = static_cast<uint8_t>(screen[offset + i] | 0x80);
    }
}

constexpr std::size_t kNameColumns = 30;   // list entries: " A  " + 30
constexpr std::size_t kLabelColumns = 32;  // sources and drives: "D1 " + 32 (the reference's 35 columns)
constexpr const char* kLockedMark = " SL";  // read-only drive, after its name (ADR-017 amendment 8)
constexpr std::size_t kLockedColumns = kLabelColumns - 3;  // the name, cut to leave room for the mark

int digitOf(uint8_t code) noexcept
{
    for (unsigned i = 0; i < MenuController::kDrives; ++i) {
        if (kDigitCodes[i] == code) {
            return static_cast<int>(i + 1);
        }
    }
    return 0;
}

int letterOf(uint8_t code) noexcept
{
    for (unsigned i = 0; i < kListMaxEntries; ++i) {
        if (kLetterCodes[i] == code) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const char* reason(ErrorCode error) noexcept
{
    switch (error) {
    case ErrorCode::InvalidAtrHeader:
    case ErrorCode::UnsupportedSectorSize:
    case ErrorCode::UnsupportedAtrLayout:
    case ErrorCode::ImageTooSmall:
    case ErrorCode::ImageTooLarge:
    case ErrorCode::InvalidGeometry:
    case ErrorCode::InvalidDiskImage:
        return "ATR INVALIDA";
    case ErrorCode::NotFound:
        return "NO ENCONTRADA";
    case ErrorCode::IoError:
        return "ERROR DE LECTURA";
    case ErrorCode::MailboxBusy:
        return "UNIDAD OCUPADA, REPITE";
    default:
        return "NO SE PUDO MONTAR";
    }
}

DriveNumber drive(unsigned number) { return DriveNumber::from(static_cast<uint8_t>(number)).value(); }

const char* loaderText(LoaderError error) noexcept
{
    switch (error) {
    case LoaderError::NoEncontrado:
        return " ERROR: SERVIDOR NO ENCONTRADO";
    case LoaderError::NoSePudoAbrir:
        return " ERROR: NO SE PUDO ABRIR";
    case LoaderError::XexNoValido:
        return " XEX NO VALIDO";
    case LoaderError::XexNoCompatible:
        return " XEX NO COMPATIBLE";
    case LoaderError::NoResponde:
        break;
    }
    return " ERROR: SERVIDOR NO RESPONDE";
}

const char* loaderName(LoaderError error) noexcept
{
    switch (error) {
    case LoaderError::NoEncontrado:
        return "not found";
    case LoaderError::NoSePudoAbrir:
        return "could not open";
    case LoaderError::XexNoValido:
        return "invalid";
    case LoaderError::XexNoCompatible:
        return "incompatible";
    case LoaderError::NoResponde:
        break;
    }
    return "no answer";
}

// "*tnfs*<n>", the location of a slot of the MemoryImageStore.
rb::app::ImageLocation tnfsLocation(std::size_t slot)
{
    char text[16];
    std::snprintf(text, sizeof text, "%s%u", kTnfsLocationPrefix, static_cast<unsigned>(slot));
    return rb::app::ImageLocation::from(text).value();
}

void put(uint8_t* screen, unsigned offset, unsigned width, const char* text, bool upper) noexcept
{
    const Span<uint8_t> line{screen + offset, width};
    if (upper) {
        screenLine(text, line);
    } else {
        screenText(text, line);
    }
}

}  // namespace

void MenuController::event(const char* name, const char* detail)
{
    if (events_ != nullptr) {
        events_->menuEvent(name, detail);
    }
}

void MenuController::say(const char* format, ...) noexcept
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(message_.data(), message_.size(), format, args);
    va_end(args);
}

const char* MenuController::driveName(unsigned number) const noexcept
{
    if (number < 1 || number > kDrives) {
        return nullptr;
    }
    const Shown& shown = shown_[number - 1];
    switch (shown.kind) {
    case Shown::Kind::Local:
        return catalog_.name(shown.entry);
    case Shown::Kind::Tnfs:
        return shown.name;
    case Shown::Kind::None:
        break;
    }
    return nullptr;
}

void MenuController::onColdReset() noexcept
{
    coldResetPending_ = true;
    cancelJob();
    view_ = View::Main;
    chosen_ = kNoEntry;
    message_[0] = '\0';
}

void MenuController::recycle(const Shown& shown) noexcept
{
    if (shown.kind == Shown::Kind::Tnfs && loader_ != nullptr) {
        loader_->recycle(shown.slot);
    }
}

void MenuController::collect()
{
    const auto outcome = collect_.execute();
    if (!outcome) {
        return;
    }
    const unsigned number = outcome->drive.value();
    char detail[64];
    const bool restoring = restoreDrive_ == number && inFlight_ == InFlight::Mount;
    restoreDrive_ = 0;
    if (outcome->error) {
        std::snprintf(detail, sizeof detail, "D%u not applied: error %u", number,
                      static_cast<unsigned>(*outcome->error));
        if (restoring) {
            restoreProblem(number, "INVALIDA", reason(*outcome->error));
        }
        if (inFlight_ == InFlight::Mount || inFlight_ == InFlight::Unmount) {
            shown_[number - 1] = previous_;
            recycle(incoming_);  // its source was closed with the reply (ADR-014)
            say(" ERROR: D%u NO CAMBIO", number);
        } else if (inFlight_ == InFlight::Boot) {
            say(" ERROR: D1 NO SE PUDO MONTAR");
        }
    } else {
        if (inFlight_ == InFlight::Menu || inFlight_ == InFlight::Boot) {
            menuOnD1_ = inFlight_ == InFlight::Menu;
        } else {
            recycle(previous_);  // the displaced source was closed with the reply
            if (restoring) {
                ++restored_;
            } else {
                changed();  // a mount or an unmount the menu asked for: remember it
            }
        }
        const char* what = inFlight_ == InFlight::Menu    ? "menu"
                           : inFlight_ == InFlight::Boot  ? "image"
                           : inFlight_ == InFlight::Mount ? "mounted"
                                                          : "empty";
        std::snprintf(detail, sizeof detail, "D%u=%s", number, what);
    }
    const bool remount = inFlight_ == InFlight::Unmount && !outcome->error && remountDrive_ == number;
    const EntryIndex remountEntry = remountEntry_;
    inFlight_ = InFlight::None;
    previous_ = Shown{};
    incoming_ = Shown{};
    remountDrive_ = 0;
    remountEntry_ = kNoEntry;
    event("menu_mount", detail);
    if (remount) {
        mountOn(number, remountEntry);  // the old source is closed now
    }
}

void MenuController::service()
{
    collect();
    if (coldResetPending_ && mailbox_.idle()) {
        if (menuOnD1_) {
            coldResetPending_ = false;  // a cold reset with the menu already on D1 changes nothing
        } else {
            requestMenu();
        }
    }
    if (restoring_) {
        restoreNext();  // after the menu: D1 comes first
    }
}

void MenuController::requestMenu()
{
    const auto menu = rb::app::ImageLocation::from(kMenuLocation);
    const auto mounted = mount_.execute(drive(1), menu.value());
    if (mounted.ok()) {
        inFlight_ = InFlight::Menu;
        coldResetPending_ = false;
        event("menu_return", "cold reset: menu requested for D1");
    } else {
        char detail[48];
        std::snprintf(detail, sizeof detail, "menu mount failed: error %u", static_cast<unsigned>(mounted.error()));
        event("menu_return", detail);
    }
}

MenuReply MenuController::key(uint8_t code, uint8_t row)
{
    if (restoring_) {
        return render(kMenuActionWait);  // every key, A too, waits for the drives
    }
    if (job_ != Job::None) {
        return onLoading(code);
    }
    // $FF (redraw) and $FE (poll) are not keys: compared before the mask.
    if (code == kMenuNoKey || code == kMenuPollKey) {
        return render(kMenuActionShow);
    }
    // Editing a server (menu-editar-servidor): kMenuEditSave and Esc, compared before
    // the row and the mask (kMenuEditSave is not a key of the keyboard matrix).
    if (view_ == View::Edit) {
        onEdit(code);
        return render(kMenuActionShow);
    }
    if (editing()) {  // the name of a new ATR, the same way
        onCreateName(code);
        return render(kMenuActionShow);
    }
    // The bar may have moved on the Atari since the host drew it: start from the row it
    // shows. A row not on the screen changes nothing.
    if (row != kMenuNoRow) {
        (void)selectRow(row);
    }
    // RETURN or fire on a row the 6502 marked by itself (N007): the cursor goes there
    // first, then it is RETURN on the marked row. A row not on the screen does nothing.
    if (isConfirmRow(code)) {
        if (!selectRow(static_cast<unsigned>(code - kMenuConfirmRow))) {
            return render(kMenuActionShow);
        }
        code = kKeyReturn;
    }
    // $40-$44 (arrows, joystick, fire) are compared before the mask too: they are not keys
    // of the keyboard matrix, and the mask would turn them into $00-$04 (plan §8, point 2).
    const uint8_t key = isNavigation(code) ? code : code & kMenuKeyMask;  // Shift and Control do not change it
    const int digit = digitOf(key);
    switch (view_) {
    case View::Main:
        if (key == kKeyA && digit == 0) {
            if (boot()) {
                return render(kMenuActionBoot);
            }
        } else if (!moveLine(sourceLine_, key)) {
            onMain(isConfirm(key) ? sourceLine_ + 1 : digit, key);  // RETURN and fire: the marked source
        }
        break;
    case View::List:
        onList(key);
        break;
    case View::Pick:
        if (key == kKeyEsc) {
            view_ = View::List;
            chosen_ = kNoEntry;
            message_[0] = '\0';
        } else if (!moveLine(driveLine_, key) && driveDigit(key, digit) != 0) {
            onPick(driveDigit(key, digit));
        }
        break;
    case View::Unmount:
        if (key == kKeyEsc) {
            view_ = View::Main;
            message_[0] = '\0';
        } else if (!moveLine(driveLine_, key) && driveDigit(key, digit) != 0) {
            onUnmount(driveDigit(key, digit));
        }
        break;
    case View::Folder:
        onFolder(key);
        break;
    case View::FolderPick:
        if (!moveLine(driveLine_, key)) {
            onFolderPick(key, driveDigit(key, digit));
        }
        break;
    case View::Edit:
        break;  // answered above
    case View::Create:
        onCreate(key, digit);  // density or drive; the name was answered above
        break;
    }
    if (job_ != Job::None) {
        return advance();  // the key started a TNFS job
    }
    return render(kMenuActionShow);
}

bool MenuController::moveLine(uint8_t& line, uint8_t key) noexcept
{
    switch (key) {
    case kMenuNavUp:
        if (line > 0) {
            --line;
        }
        return true;
    case kMenuNavDown:
        if (line + 1u < kDrives) {
            ++line;
        }
        return true;
    case kMenuNavLeft:
    case kMenuNavRight:
        return true;  // no page on the main screen
    default:
        return false;
    }
}

int MenuController::driveDigit(uint8_t key, int digit) const noexcept
{
    return isConfirm(key) ? driveLine_ + 1 : digit;
}

void MenuController::onMain(int digit, uint8_t key)
{
    char detail[96];
    if (digit >= 1) {
        sourceLine_ = static_cast<uint8_t>(digit - 1);  // the cursor follows the digit
    }
    if (digit == 1) {
        view_ = View::List;
        listCursor_ = Cursor{};
        message_[0] = '\0';
        std::snprintf(detail, sizeof detail, "source=1 local entries=%u", static_cast<unsigned>(catalog_.count()));
        event("menu_open", detail);
    } else if (digit >= 2) {
        const char* name = sources_.name(static_cast<unsigned>(digit));
        if (name != nullptr && loader_ != nullptr) {
            message_[0] = '\0';
            startJob(Job::Open, static_cast<unsigned>(digit), "/", 1);
            return;
        }
        if (name != nullptr) {
            say(" PENDIENTE: FALTA EL CLIENTE TNFS");
            std::snprintf(detail, sizeof detail, "source=%d name=%s: needs a TNFS client", digit, name);
        } else {
            say(" FUENTE %d SIN CONFIGURAR", digit);
            std::snprintf(detail, sizeof detail, "source=%d not configured", digit);
        }
        event("menu_pending", detail);
    } else if (key == kKeyE) {
        startEdit();
    } else if (key == kKeyU) {
        view_ = View::Unmount;
        driveLine_ = 0;
        say(" DESMONTAR: PULSA 1-8, ESC CANCELA");
    } else if (key == kKeyN) {
        startCreate();
    }
}

void MenuController::onList(uint8_t key)
{
    if (key == kKeyEsc) {
        view_ = View::Main;
        message_[0] = '\0';
        return;
    }
    const EntryIndex entry = listKey(listCursor_, catalog_.count(), key);
    if (entry == kNoEntry) {
        return;  // a move, or not an entry of the list
    }
    chosen_ = entry;
    view_ = View::Pick;
    driveLine_ = 0;
    say(" UNIDAD PARA: %s", catalog_.name(entry));
}

MenuController::EntryIndex MenuController::listKey(Cursor& cursor, std::size_t count, uint8_t key) noexcept
{
    if (count == 0) {
        return kNoEntry;  // nothing marked: moves, RETURN and fire do nothing
    }
    const std::size_t pages = (count + kPageRows - 1) / kPageRows;
    const std::size_t rows = pageRows(cursor, count);
    switch (key) {
    case kMenuNavUp:
        // The 6502 moves inside the page by itself and sends up only from the top row, and
        // down only from the bottom one (N007): the host's row may be older than the bar.
        cursor.row = 0;
        if (cursor.page > 0) {
            --cursor.page;
            cursor.row = static_cast<uint8_t>(kPageRows - 1);
        }
        return kNoEntry;
    case kMenuNavDown:
        cursor.row = static_cast<uint8_t>(rows - 1);
        if (cursor.page + 1u < pages) {
            ++cursor.page;
            cursor.row = 0;
        }
        return kNoEntry;
    case kMenuNavLeft:
        if (cursor.page > 0) {
            --cursor.page;
            cursor.row = 0;
        }
        return kNoEntry;
    case kMenuNavRight:
        if (cursor.page + 1u < pages) {
            ++cursor.page;
            cursor.row = 0;
        }
        return kNoEntry;
    case kMenuNavFire:
    case kKeyReturn:
        return static_cast<EntryIndex>(cursor.page * kPageRows + cursor.row);
    default:
        break;
    }
    const int letter = letterOf(key);
    if (letter < 0 || static_cast<std::size_t>(letter) >= rows) {
        return kNoEntry;  // not a letter of the visible page
    }
    cursor.row = static_cast<uint8_t>(letter);
    return static_cast<EntryIndex>(cursor.page * kPageRows + cursor.row);
}

std::size_t MenuController::pageRows(const Cursor& cursor, std::size_t count) noexcept
{
    return count == 0 ? 0 : std::min(kPageRows, count - cursor.page * kPageRows);
}

uint8_t MenuController::pages(const Cursor& cursor, std::size_t count) noexcept
{
    const std::size_t total = (count + kPageRows - 1) / kPageRows;
    uint8_t ways = 0;
    if (cursor.page > 0) {
        ways = static_cast<uint8_t>(ways | kMenuPageBefore);
    }
    if (cursor.page + 1u < total) {
        ways = static_cast<uint8_t>(ways | kMenuPageAfter);
    }
    return ways;
}

void MenuController::cursorBlock(uint8_t* screen, unsigned base, std::size_t rows, unsigned row, uint8_t pages) noexcept
{
    uint8_t* block = screen + kMenuCursor;
    block[0] = static_cast<uint8_t>(base & 0xFF);
    block[1] = static_cast<uint8_t>(base >> 8);
    block[2] = static_cast<uint8_t>(rows);
    block[3] = static_cast<uint8_t>(rows == 0 ? 0 : row);
    block[4] = rows != 0 ? pages : 0;
}

bool MenuController::selectRow(unsigned row) noexcept
{
    switch (view_) {
    case View::Main:
        if (row >= kDrives) {
            return false;
        }
        sourceLine_ = static_cast<uint8_t>(row);
        return true;
    case View::Pick:
    case View::Unmount:
    case View::FolderPick:
        if (row >= kDrives) {
            return false;
        }
        driveLine_ = static_cast<uint8_t>(row);
        return true;
    case View::List:
        if (row >= pageRows(listCursor_, catalog_.count())) {
            return false;
        }
        listCursor_.row = static_cast<uint8_t>(row);
        return true;
    case View::Folder:
        if (row >= pageRows(folderCursor_, folder_.count())) {
            return false;
        }
        folderCursor_.row = static_cast<uint8_t>(row);
        return true;
    case View::Edit:
        return false;  // the 6502 has no bar while it edits
    case View::Create:
        if (createStep_ != CreateStep::Drive || row >= kDrives) {
            return false;  // no bar while the name or the density is asked
        }
        driveLine_ = static_cast<uint8_t>(row);
        return true;
    }
    return false;
}

void MenuController::markRow(uint8_t* screen, const Cursor& cursor, std::size_t count) noexcept
{
    if (count == 0) {
        return;
    }
    invert(screen, static_cast<unsigned>(kListEntries + cursor.row * kMenuLineWidth));
}

void MenuController::pushPosition() noexcept
{
    if (depth_ < kStackDepth) {
        stack_[depth_] = folderCursor_;
    }
    ++depth_;
}

MenuController::Cursor MenuController::popPosition() noexcept
{
    if (depth_ == 0) {
        return Cursor{};
    }
    --depth_;
    return depth_ < kStackDepth ? stack_[depth_] : Cursor{};
}

void MenuController::onPick(int digit)
{
    const EntryIndex entry = chosen_;
    chosen_ = kNoEntry;
    view_ = View::Main;
    const char* name = catalog_.name(entry);
    if (hasXexExtension(name)) {
        chooseXex(digit, 1, name, std::strlen(name), name);
        return;
    }
    if (digit == 1) {
        assignD1(entry);
    } else {
        mountOn(static_cast<unsigned>(digit), entry);
    }
}

// menu-xex: a .xex goes to D1 only (AC4). It is read (source 1: the --images folder) or
// brought over TNFS, checked and wrapped with the 6502 loader on the loader thread, which
// allocates; key() never does (ADR-011). The job ends in fetched() or failed().
void MenuController::chooseXex(int digit, unsigned source, const char* path, std::size_t length, const char* name)
{
    if (digit != 1) {
        say(" UN XEX SOLO VA EN D1");
        char detail[TnfsFolder::kDisplayWidth + 48];
        std::snprintf(detail, sizeof detail, "D%d name=%s refused: xex only on D1", digit, name);
        event("menu_select", detail);
        return;
    }
    std::snprintf(jobName_, sizeof jobName_, "%s", name);
    jobDrive_ = 1;
    message_[0] = '\0';
    startJob(Job::Xex, source, path, length);
}

void MenuController::onUnmount(int digit)
{
    view_ = View::Main;
    emptyDrive(static_cast<unsigned>(digit));
}

std::optional<ErrorCode> MenuController::assignD1(EntryIndex entry)
{
    const char* name = catalog_.name(entry);
    // Every catalog name was accepted by ImageLocation::from when it was listed.
    const auto location = rb::app::ImageLocation::from(name);
    // D1 keeps serving the menu: validate now (open the file, read its header),
    // mount when A is pressed.
    std::optional<ErrorCode> error;
    bool locked = true;
    const auto opened = repository_.open(location.value());
    if (!opened.ok()) {
        error = opened.error();
    } else {
        locked = !opened.value()->writable();  // confirmed when A mounts it
        {
            const auto image = rb::domain::DiskImage::open(*opened.value());  // detaches when it goes
            if (!image.ok()) {
                error = image.error();
            }
        }
        repository_.close(opened.value());
    }
    char detail[MenuCatalog::kMaxName + 48];
    if (error) {
        say(" ERROR: %s", reason(*error));
        std::snprintf(detail, sizeof detail, "D1 name=%s refused: error %u", name, static_cast<unsigned>(*error));
    } else {
        Shown local;
        local.kind = Shown::Kind::Local;
        local.entry = entry;
        local.locked = locked;
        recycle(shown_[0]);  // D1 is assigned, not mounted: nothing holds its slot
        shown_[0] = local;
        say(" D1: %s (A ARRANCA)", name);
        changed();
        std::snprintf(detail, sizeof detail, "D1 name=%s assigned", name);
    }
    event("menu_select", detail);
    return error;
}

std::optional<ErrorCode> MenuController::mountOn(unsigned number, EntryIndex entry)
{
    const Shown& current = shown_[number - 1];
    if (current.kind == Shown::Kind::Local && current.entry == entry && remountDrive_ == 0) {
        // The same file again: a new source opened beside the old writer would be read-only.
        remountDrive_ = number;
        remountEntry_ = entry;
        emptyDrive(number);
        if (inFlight_ != InFlight::Unmount) {  // refused: nothing to wait for
            remountDrive_ = 0;
            remountEntry_ = kNoEntry;
        }
        return std::nullopt;
    }
    const char* name = catalog_.name(entry);
    const auto location = rb::app::ImageLocation::from(name);
    const rb::core::Result<rb::app::MountReport> mounted =
        mailbox_.idle() ? mount_.execute(drive(number), location.value())
                        : rb::core::Result<rb::app::MountReport>{rb::core::Failure{ErrorCode::MailboxBusy}};
    char detail[MenuCatalog::kMaxName + 64];
    std::optional<ErrorCode> error;
    if (!mounted.ok()) {
        error = mounted.error();
        say(" ERROR: %s", reason(mounted.error()));
        std::snprintf(detail, sizeof detail, "D%u name=%s refused: error %u", number, name,
                      static_cast<unsigned>(mounted.error()));
    } else {
        previous_ = shown_[number - 1];
        incoming_ = Shown{};
        incoming_.kind = Shown::Kind::Local;
        incoming_.entry = entry;
        incoming_.locked = mounted.value().writeProtected;
        shown_[number - 1] = incoming_;
        inFlight_ = InFlight::Mount;
        say(" D%u: %s", number, name);
        std::snprintf(detail, sizeof detail, "D%u name=%s accepted: %u sectors of %u bytes", number, name,
                      static_cast<unsigned>(mounted.value().geometry.sectorCount()),
                      static_cast<unsigned>(mounted.value().geometry.sectorSize()));
    }
    event("menu_select", detail);
    return error;
}

void MenuController::emptyDrive(unsigned number)
{
    char detail[48];
    if (shown_[number - 1].kind == Shown::Kind::None) {
        say(" D%u YA ESTA VACIA", number);
        return;
    }
    if (number == 1) {
        recycle(shown_[0]);
        shown_[0] = Shown{};  // D1 serves the menu: there is nothing mounted to remove
        say(" D1 DESMONTADA");
        changed();
        event("menu_unmount", "D1 unassigned");
        return;
    }
    const auto removed =
        mailbox_.idle() ? unmount_.execute(drive(number)) : rb::core::Result<void>{rb::core::Failure{ErrorCode::MailboxBusy}};
    if (!removed.ok()) {
        say(" ERROR: %s", reason(removed.error()));
        std::snprintf(detail, sizeof detail, "D%u refused: error %u", number, static_cast<unsigned>(removed.error()));
    } else {
        previous_ = shown_[number - 1];
        incoming_ = Shown{};
        shown_[number - 1] = Shown{};
        inFlight_ = InFlight::Unmount;
        say(" D%u DESMONTADA", number);
        std::snprintf(detail, sizeof detail, "D%u requested", number);
    }
    event("menu_unmount", detail);
}

bool MenuController::boot()
{
    const Shown& d1 = shown_[0];
    if (d1.kind == Shown::Kind::None) {
        say(" D1 SIN DISCO: ABRE LA FUENTE 1");
        return false;
    }
    const char* name = driveName(1);
    // Every catalog name was accepted by ImageLocation::from when it was listed.
    const rb::app::ImageLocation location =
        d1.kind == Shown::Kind::Tnfs ? tnfsLocation(d1.slot) : rb::app::ImageLocation::from(name).value();
    const rb::core::Result<rb::app::MountReport> mounted =
        mailbox_.idle() ? mount_.execute(drive(1), location)
                        : rb::core::Result<rb::app::MountReport>{rb::core::Failure{ErrorCode::MailboxBusy}};
    char detail[MenuCatalog::kMaxName + 48];
    if (!mounted.ok()) {
        say(" ERROR: %s", reason(mounted.error()));
        std::snprintf(detail, sizeof detail, "D1 name=%s refused: error %u", name, static_cast<unsigned>(mounted.error()));
        event("menu_boot", detail);
        return false;
    }
    inFlight_ = InFlight::Boot;
    shown_[0].locked = mounted.value().writeProtected;
    message_[0] = '\0';
    std::snprintf(detail, sizeof detail, "D1 name=%s", name);
    event("menu_boot", detail);
    return true;
}

MenuReply MenuController::render(uint8_t action) const noexcept
{
    if (action == kMenuActionShow && view_ == View::Main && (restoring_ || saving())) {
        action = kMenuActionWait;  // the Atari polls: the restore or the save result shows up by itself
    }
    if (editing() && action == kMenuActionShow) {
        action = kMenuActionEdit;  // the 6502 edits the row (menu-editar-servidor, menu-formatear-disco)
    }
    MenuReply reply;
    reply.bytes[0] = action;
    uint8_t* screen = reply.bytes.data() + 2;
    char text[kMenuLineWidth + 64];
    if (view_ == View::Folder) {  // FolderPick chooses the drive on the main screen (M42-4)
        reply.bytes[1] = kMenuLayoutList;
        std::memcpy(screen, kMenuListTemplate, kMenuScreenSize);
        renderFolder(screen, text, sizeof text);
        if (action == kMenuActionShow && job_ == Job::None) {
            cursorBlock(screen, kListEntries, pageRows(folderCursor_, folder_.count()), folderCursor_.row,
                        pages(folderCursor_, folder_.count()));
        }
        return reply;
    }
    if (view_ == View::List) {
        reply.bytes[1] = kMenuLayoutList;
        std::memcpy(screen, kMenuListTemplate, kMenuScreenSize);
        put(screen, kListPath, kMenuLineWidth, " FUENTE 1: SD", true);  // the title stays fixed (N008)
        const std::size_t first = listCursor_.page * kPageRows;  // only the visible rows are drawn
        for (std::size_t i = 0; i < kPageRows && first + i < catalog_.count(); ++i) {
            std::snprintf(text, sizeof text, " %c  %.*s", static_cast<char>('A' + i), static_cast<int>(kNameColumns),
                          catalog_.name(first + i));
            put(screen, static_cast<unsigned>(kListEntries + i * kMenuLineWidth), kMenuLineWidth, text, true);
        }
        markRow(screen, listCursor_, catalog_.count());
        if (action == kMenuActionShow) {
            cursorBlock(screen, kListEntries, pageRows(listCursor_, catalog_.count()), listCursor_.row,
                        pages(listCursor_, catalog_.count()));
        }
        // The total: the names kept, or every .atr seen once the catalog is full.
        const std::size_t total = catalog_.count() < MenuCatalog::kMaxEntries ? catalog_.count() : catalog_.atrSeen();
        char page[TnfsFolder::kNoticeSize];
        pageLine(catalog_.count(), total, listCursor_.page, page);
        if (message_[0] != '\0') {
            std::snprintf(text, sizeof text, "%s", message_.data());
        } else {
            std::snprintf(text, sizeof text, " %s", page[0] != '\0' ? page : "PULSA LA LETRA DE LA IMAGEN");
        }
        put(screen, kListMessage, kMenuLineWidth, text, true);
        return reply;
    }
    reply.bytes[1] = kMenuLayoutMain;
    std::memcpy(screen, kMenuMainTemplate, kMenuScreenSize);
    put(screen, kMainSources, kMenuLineWidth, "1  SD", false);
    char edited[kMenuEditWidth + 1];
    const std::size_t editedLength = editing() ? editedText(edited) : 0;
    for (unsigned n = 2; n <= kDrives; ++n) {
        const char* name = view_ == View::Edit && n == editSource_ ? edited : sources_.name(n);
        std::snprintf(text, sizeof text, "%u  %.*s", n, static_cast<int>(kLabelColumns), name != nullptr ? name : "");
        put(screen, kMainSources + (n - 1) * kMenuLineWidth, kMenuLineWidth, text, false);
    }
    for (unsigned n = 1; n <= kDrives; ++n) {
        const char* name = driveName(n);
        if (name != nullptr && shown_[n - 1].locked) {
            std::snprintf(text, sizeof text, "D%u %-*.*s%s", n, static_cast<int>(kLockedColumns),
                          static_cast<int>(kLockedColumns), name, kLockedMark);
        } else {
            std::snprintf(text, sizeof text, "D%u %.*s", n, static_cast<int>(kLabelColumns),
                          name != nullptr ? name : "SIN DISCO");
        }
        put(screen, kMainDrives + (n - 1) * kMenuLineWidth, kMenuLineWidth, text, true);
    }
    put(screen, kMainTape, kMenuLineWidth, "C: SIN CINTA", true);
    const bool local = action == kMenuActionShow && job_ == Job::None;  // the 6502 moves the bar (N007)
    if (view_ == View::Edit) {
        // No bar: the 6502 shows its own cursor in the field (after "n  ").
        cursorBlock(screen, kMainSources, 0, 0, 0);
        const unsigned field = kMainSources + (editSource_ - 1) * kMenuLineWidth + 3;
        screen[kMenuEdit] = static_cast<uint8_t>(field & 0xFF);
        screen[kMenuEdit + 1] = static_cast<uint8_t>(field >> 8);
        screen[kMenuEdit + 2] = static_cast<uint8_t>(editedLength);
    } else if (editing()) {
        // The new name: the 6502 edits kMenuEditWidth columns after "NOMBRE:".
        cursorBlock(screen, kMainSources, 0, 0, 0);
        const unsigned field = kMainMessage + kCreateLabel;
        screen[kMenuEdit] = static_cast<uint8_t>(field & 0xFF);
        screen[kMenuEdit + 1] = static_cast<uint8_t>(field >> 8);
        screen[kMenuEdit + 2] = static_cast<uint8_t>(editedLength);
    } else if (view_ == View::Create && createStep_ == CreateStep::Density) {
        cursorBlock(screen, kMainSources, 0, 0, 0);  // no bar: S or M
    } else if (view_ == View::Main) {
        invert(screen, kMainSources + sourceLine_ * kMenuLineWidth);
        cursorBlock(screen, kMainSources, local ? kDrives : 0, sourceLine_, 0);
    } else {  // Pick, FolderPick, Unmount: the drive under the cursor
        invert(screen, kMainDrives + driveLine_ * kMenuLineWidth);
        cursorBlock(screen, kMainDrives, local ? kDrives : 0, driveLine_, 0);
    }
    if (job_ != Job::None) {
        std::snprintf(text, sizeof text, " CARGANDO %u%%", static_cast<unsigned>(percent_));
        put(screen, kMainMessage, kMenuLineWidth, text, true);
    } else if (editing() && view_ == View::Create) {
        static_assert(kCreateLabel + kMenuEditWidth < kMenuLineWidth, "the field and its cursor fit the row");
        std::snprintf(text, sizeof text, "NOMBRE:%s", edited);
        put(screen, kMainMessage, kMenuLineWidth, text, false);  // as typed: the 6502 shows it so
    } else {
        put(screen, kMainMessage, kMenuLineWidth, restoring_ ? " RESTAURANDO UNIDADES" : message_.data(), true);
    }
    return reply;
}

// ---------------------------------------------------------------------------
// Editing a server (menu-editar-servidor, M43).

void MenuController::startEdit()
{
    const unsigned source = sourceLine_ + 1u;
    if (source == 1) {
        say(" LA FUENTE 1 ES LOCAL");
        event("menu_pending", "edit server: source 1 is the local folder");
        return;
    }
    if (sourcesFile_ == nullptr) {
        say(" SIN ARCHIVO DE FUENTES");
        event("menu_pending", "edit server: no --sources file");
        return;
    }
    view_ = View::Edit;
    editSource_ = source;
    editText_.fill(' ');
    const char* name = sources_.name(source);
    for (std::size_t i = 0; name != nullptr && name[i] != '\0' && i < editText_.size(); ++i) {
        editText_[i] = name[i];
    }
    say(" RETURN GUARDA, VACIO BORRA, ESC CANCELA");
}

void MenuController::text(uint8_t position, uint8_t ascii) noexcept
{
    if (editing() && position < editText_.size()) {
        editText_[position] = static_cast<char>(ascii);
    }
}

std::size_t MenuController::editedText(char* out) const noexcept
{
    std::size_t length = editText_.size();
    while (length > 0 && editText_[length - 1] == ' ') {
        --length;
    }
    std::memcpy(out, editText_.data(), length);
    out[length] = '\0';
    return length;
}

void MenuController::onEdit(uint8_t code)
{
    if (code == kMenuEditSave) {
        saveEdit();
    } else if ((code & kMenuKeyMask) == kKeyEsc) {
        view_ = View::Main;
        message_[0] = '\0';
    }
    // Anything else: the 6502 sends only those two while it edits; the field is drawn again.
}

void MenuController::saveEdit()
{
    char name[kMenuEditWidth + 1];
    char detail[160];
    const std::size_t length = editedText(name);
    if (length == 0) {
        deleteSource();  // M44: an empty text deletes a configured source; on an empty row, as Esc
        return;
    }
    if (!MenuSources::validServer(name)) {
        say(" NOMBRE O IPV4 NO VALIDO");  // still editing, with the text typed
        std::snprintf(detail, sizeof detail, "source=%u reason=not a name or IPv4: %s", editSource_, name);
        event("menu_source_rejected", detail);
        return;
    }
    // An empty row past the last source adds one: it takes the first free number.
    const std::size_t index = editSource_ - 2u < sources_.count() ? editSource_ - 2u : sources_.count();
    char error[96];
    view_ = View::Main;
    if (!writeSourcesFile(sourcesFile_, index, name, error, sizeof error)) {
        say(" NO SE PUDO GUARDAR EL ARCHIVO");
        std::snprintf(detail, sizeof detail, "source=%u reason=%s", editSource_, error);
        event("menu_source_rejected", detail);
        return;
    }
    const unsigned number = sources_.set(static_cast<unsigned>(index + 2), name);
    if (loader_ != nullptr) {
        loader_->changeServer(number, name);
    }
    sourceLine_ = static_cast<uint8_t>(number - 1);
    if (number == editSource_) {
        say(" FUENTE %u GUARDADA", number);
    } else {
        say(" GUARDADA COMO FUENTE %u", number);
    }
    std::snprintf(detail, sizeof detail, "source=%u name=%s", number, name);
    event("menu_source_saved", detail);
}

// M44 (menu-borrar-fuente): the source's line leaves --sources and the next
// sources move up one number. Their links take the names now under their numbers.
void MenuController::deleteSource()
{
    view_ = View::Main;
    message_[0] = '\0';
    const char* old = sources_.name(editSource_);
    if (old == nullptr) {
        return;  // an empty row: nothing to delete, as Esc
    }
    char name[MenuSources::kMaxName + 1];
    std::snprintf(name, sizeof name, "%s", old);
    char error[96];
    char detail[160];
    if (!writeSourcesFile(sourcesFile_, editSource_ - 2u, nullptr, error, sizeof error)) {
        say(" NO SE PUDO GUARDAR EL ARCHIVO");
        std::snprintf(detail, sizeof detail, "source=%u reason=%s", editSource_, error);
        event("menu_source_rejected", detail);
        return;
    }
    const unsigned last = static_cast<unsigned>(sources_.count()) + 1u;  // the old last number
    sources_.remove(editSource_);
    if (loader_ != nullptr) {
        for (unsigned n = editSource_; n <= last; ++n) {
            const char* now = sources_.name(n);
            loader_->changeServer(n, now != nullptr ? now : "");
        }
    }
    say(" FUENTE %u BORRADA", editSource_);
    std::snprintf(detail, sizeof detail, "source=%u name=%s", editSource_, name);
    event("menu_source_deleted", detail);
}

// ---------------------------------------------------------------------------
// Creating a blank ATR (menu-formatear-disco, M46-2, M46-3, M46-5).

void MenuController::createEvent(const char* result, unsigned drive)
{
    const bool chosen = createStep_ != CreateStep::Name;
    char detail[96];
    std::snprintf(detail, sizeof detail, "name=%s density=%s drive=%u result=%s",
                  createName_[0] != '\0' ? createName_ : "-",
                  !chosen ? "-" : createSingle_ ? "single" : "medium", drive, result);
    event("menu_create", detail);
}

void MenuController::startCreate()
{
    createName_[0] = '\0';
    createStep_ = CreateStep::Name;
    if (imagesFolder_ == nullptr || writableCatalog_ == nullptr) {
        say(" SIN CARPETA LOCAL");
        createEvent("no_folder", 0);
        return;
    }
    if (writableCatalog_->full()) {
        say(" LISTA LLENA: %u IMAGENES", static_cast<unsigned>(MenuCatalog::kMaxEntries));
        createEvent("full", 0);
        return;
    }
    view_ = View::Create;
    editText_.fill(' ');
    message_[0] = '\0';
}

void MenuController::onCreateName(uint8_t code)
{
    if ((code & kMenuKeyMask) == kKeyEsc && code != kMenuEditSave) {
        view_ = View::Main;
        message_[0] = '\0';
        return;
    }
    if (code != kMenuEditSave) {
        return;  // the 6502 sends only those two while it edits
    }
    char name[kMenuEditWidth + 1];
    editedText(name);
    view_ = View::Main;
    // Without Shift the 6502 sends lower case: the file name is the same in capitals.
    for (char* c = name; *c != '\0'; ++c) {
        if (*c >= 'a' && *c <= 'z') {
            *c = static_cast<char>(*c - 'a' + 'A');
        }
    }
    if (!isNewAtrName(name)) {
        say("%s", kCreateInvalid);
        createEvent("invalid_name", 0);
        return;
    }
    // isNewAtrName allows at most kMaxNewAtrName characters; the precision says so to the compiler.
    std::snprintf(createName_, sizeof createName_, "%.*s.ATR", static_cast<int>(kMaxNewAtrName), name);
    for (std::size_t i = 0; i < catalog_.count(); ++i) {
        if (compareNames(catalog_.name(i), createName_) == 0) {
            say(" YA EXISTE %s", createName_);
            createEvent("exists", 0);
            return;
        }
    }
    view_ = View::Create;
    createStep_ = CreateStep::Density;
    say(" S SIMPLE  M MEDIA  ESC CANCELA");
}

void MenuController::onCreate(uint8_t key, int digit)
{
    if (key == kKeyEsc) {
        view_ = View::Main;
        message_[0] = '\0';
        return;
    }
    if (createStep_ == CreateStep::Density) {
        if (key == kKeyS || key == kKeyM) {
            createSingle_ = key == kKeyS;
            createStep_ = CreateStep::Drive;
            driveLine_ = 1;  // D2: D1 serves the menu
            say(" UNIDAD 2-8, ESC CANCELA");
        }
        return;
    }
    if (moveLine(driveLine_, key)) {
        return;
    }
    const int drive = driveDigit(key, digit);
    if (drive >= 2) {
        createOn(static_cast<unsigned>(drive));
    }
    // 1 (or RETURN on D1): the menu's drive is not offered; the step stays.
}

void MenuController::createOn(unsigned drive)
{
    view_ = View::Main;
    char name[kMaxNewAtrName + 1];
    std::snprintf(name, sizeof name, "%.*s", static_cast<int>(std::strlen(createName_) - 4), createName_);
    const CreateAtrResult result = createAtr(imagesFolder_, name,
                                             createSingle_ ? rb::domain::Density::Single : rb::domain::Density::Enhanced);
    if (result == CreateAtrResult::AlreadyExists) {
        say(" YA EXISTE %s", createName_);
        createEvent("exists", drive);
        return;
    }
    if (result != CreateAtrResult::Created) {
        say(" ERROR: NO SE PUDO CREAR");
        createEvent("io", drive);
        return;
    }
    const std::size_t position = writableCatalog_->insert(createName_);
    if (position == MenuCatalog::kNotInserted) {
        say(" ERROR: NO CABE EN LA LISTA");  // created, but the menu cannot show it
        createEvent("not_listed", drive);
        return;
    }
    shiftEntries(static_cast<EntryIndex>(position));
    createEvent("created", drive);
    mountOn(drive, static_cast<EntryIndex>(position));
}

void MenuController::shiftEntries(EntryIndex from) noexcept
{
    const auto move = [from](EntryIndex& entry) {
        if (entry != kNoEntry && entry >= from) {
            ++entry;
        }
    };
    for (Shown& shown : shown_) {
        if (shown.kind == Shown::Kind::Local) {
            move(shown.entry);
        }
    }
    if (previous_.kind == Shown::Kind::Local) {
        move(previous_.entry);
    }
    if (incoming_.kind == Shown::Kind::Local) {
        move(incoming_.entry);
    }
    move(remountEntry_);
    // The list's marked row follows the entry it was on.
    EntryIndex marked = static_cast<EntryIndex>(listCursor_.page * kPageRows + listCursor_.row);
    if (catalog_.count() > 1 && marked >= from) {
        ++marked;
        listCursor_.page = static_cast<uint16_t>(marked / kPageRows);
        listCursor_.row = static_cast<uint8_t>(marked % kPageRows);
    }
}

// ---------------------------------------------------------------------------
// TNFS sources (menu-tnfs T012).

void MenuController::renderFolder(uint8_t* screen, char* text, std::size_t size) const noexcept
{
    // The path line under the fixed title (N008): " FUENTE n: " and the folder, cut on the
    // left so the current one shows.
    const int prefix = std::snprintf(text, size, " FUENTE %u: ", source_);
    const std::size_t room = kMenuLineWidth > static_cast<unsigned>(prefix) ? kMenuLineWidth - prefix : 0;
    tnfsTitle(path_, pathLength_, room, text + prefix);
    put(screen, kListPath, kMenuLineWidth, text, true);
    const std::size_t first = folderCursor_.page * kPageRows;  // only the visible rows are drawn
    for (std::size_t i = 0; i < kPageRows && first + i < folder_.count(); ++i) {
        std::snprintf(text, size, "%c %s", static_cast<char>('A' + i), folder_.entry(first + i).display);
        put(screen, static_cast<unsigned>(kListEntries + i * kMenuLineWidth), kMenuLineWidth, text, true);
    }
    markRow(screen, folderCursor_, folder_.count());
    if (job_ != Job::None) {
        std::snprintf(text, size, " CARGANDO %u%%", static_cast<unsigned>(percent_));
    } else if (message_[0] != '\0') {
        std::snprintf(text, size, "%s", message_.data());
    } else {
        char notice[TnfsFolder::kNoticeSize];
        folder_.notice(folderCursor_.page, notice);
        std::snprintf(text, size, " %s", notice[0] != '\0' ? notice : "PULSA LA LETRA DE LA IMAGEN");
    }
    put(screen, kListMessage, kMenuLineWidth, text, true);
}

void MenuController::onFolder(uint8_t key)
{
    if (key == kKeyEsc) {
        message_[0] = '\0';
        if (pathLength_ <= 1) {
            view_ = View::Main;  // the root of the source: back to the main screen
            return;
        }
        std::size_t parent = pathLength_ - 1;
        while (parent > 0 && path_[parent] != '/') {
            --parent;
        }
        startJob(Job::Up, source_, path_, parent == 0 ? 1 : parent);
        return;
    }
    const EntryIndex entry = listKey(folderCursor_, folder_.count(), key);
    if (entry == kNoEntry) {
        return;  // a move, or not an entry of the list
    }
    const TnfsFolder::Entry& chosen = folder_.entry(entry);
    if (chosen.isDir) {
        message_[0] = '\0';
        char path[TnfsFolder::kMaxPath + 1];
        const std::size_t length = folder_.path(entry, path);
        startJob(Job::Enter, source_, path, length);  // listed() keeps this position for Esc
        return;
    }
    chosen_ = entry;
    view_ = View::FolderPick;
    driveLine_ = 0;
    say(" UNIDAD PARA: %s", chosen.display);
}

void MenuController::onFolderPick(uint8_t key, int digit)
{
    if (key == kKeyEsc) {
        view_ = View::Folder;
        chosen_ = kNoEntry;
        message_[0] = '\0';
        return;
    }
    if (digit == 0) {
        return;
    }
    const TnfsFolder::Entry& chosen = folder_.entry(chosen_);
    char path[TnfsFolder::kMaxPath + 1];
    const std::size_t length = folder_.path(chosen_, path);
    if (hasXexExtension(path)) {
        char name[TnfsFolder::kDisplayWidth + 1];
        std::snprintf(name, sizeof name, "%s", chosen.display);
        chosen_ = kNoEntry;
        view_ = View::Folder;  // refused or loading, the view stays on the folder
        message_[0] = '\0';
        chooseXex(digit, source_, path, length, name);
        return;
    }
    std::snprintf(jobName_, sizeof jobName_, "%s", chosen.display);
    jobDrive_ = static_cast<unsigned>(digit);
    chosen_ = kNoEntry;
    view_ = View::Folder;  // the view stays on the folder while the image comes
    message_[0] = '\0';
    startJob(Job::Fetch, source_, path, length);
}

void MenuController::startJob(Job job, unsigned source, const char* path, std::size_t length)
{
    if (length > kTnfsMaxPath) {
        length = kTnfsMaxPath + 1;  // the loader refuses it: NoSePudoAbrir
    } else {
        std::memcpy(jobPath_, path, length);
    }
    jobPath_[length > kTnfsMaxPath ? 0 : length] = '\0';
    jobPathLength_ = length > kTnfsMaxPath ? 0 : length;
    jobSource_ = source;
    job_ = job;
    percent_ = 0;
    jobNumber_ = job == Job::Fetch ? loader_->startFetch(source, path, length)
                 : job == Job::Xex ? loader_->startXex(source, path, length)
                                   : loader_->startList(source, path, length);
}

void MenuController::cancelJob() noexcept
{
    if (job_ != Job::None) {
        loader_->cancel(jobNumber_);  // a late result is dropped and its slot freed by the loader
        job_ = Job::None;
        jobNumber_ = 0;
    }
}

MenuReply MenuController::onLoading(uint8_t code)
{
    if (code != kMenuNoKey && code != kMenuPollKey && (code & kMenuKeyMask) == kKeyEsc) {
        cancelJob();
        message_[0] = '\0';
        event("menu_tnfs", "load cancelled with Esc");
        return render(kMenuActionShow);
    }
    return advance();  // a poll, a redraw or any other key: only the progress
}

MenuReply MenuController::advance()
{
    loader_->await(jobNumber_, kLoadWaitMicros);
    const LoaderStatus status = loader_->poll(jobNumber_, folder_);
    switch (status.kind) {
    case LoaderStatus::Kind::InProgress:
        percent_ = status.percent;
        return render(kMenuActionWait);
    case LoaderStatus::Kind::Folder:
        listed();
        break;
    case LoaderStatus::Kind::Image:
        fetched(status);
        break;
    case LoaderStatus::Kind::Error:
        failed(status);
        break;
    case LoaderStatus::Kind::Unknown:
        job_ = Job::None;  // never happens: the job is ours until it ends
        break;
    }
    return render(kMenuActionShow);
}

void MenuController::listed()
{
    const Job done = job_;
    job_ = Job::None;
    // The cursor: row 1 of page 1 in a folder opened or entered; Esc restores where
    // the parent was left, within its new total if it changed on the server.
    if (done == Job::Enter) {
        pushPosition();
        folderCursor_ = Cursor{};
    } else if (done == Job::Up) {
        folderCursor_ = popPosition();
        const std::size_t count = folder_.count();
        if (count == 0) {
            folderCursor_ = Cursor{};
        } else if (folderCursor_.page * kPageRows + folderCursor_.row >= count) {
            folderCursor_.page = static_cast<uint16_t>((count - 1) / kPageRows);
            folderCursor_.row = static_cast<uint8_t>((count - 1) % kPageRows);
        }
    } else {
        depth_ = 0;
        folderCursor_ = Cursor{};
    }
    source_ = jobSource_;
    std::memcpy(path_, jobPath_, jobPathLength_ + 1);
    pathLength_ = jobPathLength_;
    view_ = View::Folder;
    message_[0] = '\0';
    char detail[kTnfsMaxPath + 64];
    std::snprintf(detail, sizeof detail, "source=%u tnfs path=%s entries=%u shown=%u", source_, path_,
                  static_cast<unsigned>(folder_.total()),
                  static_cast<unsigned>(std::min(folder_.count(), TnfsFolder::kPageRows)));
    event("menu_open", detail);
}

void MenuController::failed(const LoaderStatus& status)
{
    job_ = Job::None;
    say("%s", loaderText(status.error));  // the view stays where it was; the cause goes to the log only
    if (status.error == LoaderError::XexNoValido || status.error == LoaderError::XexNoCompatible) {
        char refused[TnfsFolder::kDisplayWidth + 96];
        std::snprintf(refused, sizeof refused, "D%u name=%s refused: result=%s xex=1 segments=%u bytes=%lu",
                      jobDrive_, jobName_, loaderName(status.error), static_cast<unsigned>(status.segments),
                      static_cast<unsigned long>(status.bytes));
        event("menu_select", refused);
    }
    char detail[kTnfsMaxPath + LoaderStatus::kCauseSize + 48];
    std::snprintf(detail, sizeof detail, "source=%u path=%s %s: %s", jobSource_, jobPath_, loaderName(status.error),
                  status.cause);
    event("menu_tnfs", detail);
}

void MenuController::fetched(const LoaderStatus& status)
{
    job_ = Job::None;
    char sha[2 * 32 + 1];
    for (std::size_t i = 0; i < status.sha256.size(); ++i) {
        std::snprintf(sha + 2 * i, 3, "%02x", status.sha256[i]);
    }
    char detail[kTnfsMaxPath + 160];
    std::snprintf(detail, sizeof detail, "source=%u path=%s bytes=%lu sha256=%s slot=%u", jobSource_, jobPath_,
                  static_cast<unsigned long>(status.bytes), sha, static_cast<unsigned>(status.slot));
    event("menu_fetch", detail);

    Shown image;
    image.kind = Shown::Kind::Tnfs;
    image.slot = status.slot;
    std::snprintf(image.name, sizeof image.name, "%s", jobName_);
    place(jobDrive_, image, status.xex ? &status : nullptr);
}

// ---------------------------------------------------------------------------
// Persistent drives (menu-unidades-persistentes T3).

void MenuController::beginRestore() noexcept
{
    persistent_ = true;
    restoring_ = true;
    view_ = View::Main;
    message_[0] = '\0';
    event("menu_restore", "begin");
}

MenuController::EntryIndex MenuController::findEntry(const char* name) const noexcept
{
    for (std::size_t i = 0; i < catalog_.count(); ++i) {
        if (std::strcmp(catalog_.name(i), name) == 0) {
            return static_cast<EntryIndex>(i);
        }
    }
    return kNoEntry;
}

void MenuController::restore(const DriveMemory& memory)
{
    if (!restoring_) {
        return;
    }
    restored_ = 0;
    problems_ = 0;
    problem_[0] = '\0';
    pendingRestore_.fill(kNoEntry);
    for (unsigned number = 1; number <= kDrives; ++number) {
        const char* name = memory.name(number);
        if (name[0] == '\0') {
            continue;
        }
        const EntryIndex entry = findEntry(name);
        if (entry == kNoEntry) {
            restoreProblem(number, "FALTA", name);
        } else if (number > 1) {
            pendingRestore_[number - 1] = entry;  // mounted one at a time by service()
        } else if (const auto error = assignD1(entry)) {
            restoreProblem(1, "INVALIDA", reason(*error));
        } else {
            ++restored_;
        }
    }
    restoreNext();
}

void MenuController::restoreNext()
{
    // A pending cold reset took the mailbox first in service(); if its menu mount was refused,
    // the restore goes on anyway rather than leave every key waiting.
    if (!restoring_ || inFlight_ != InFlight::None || !mailbox_.idle()) {
        return;
    }
    for (unsigned number = 2; number <= kDrives; ++number) {
        const EntryIndex entry = pendingRestore_[number - 1];
        if (entry == kNoEntry) {
            continue;
        }
        pendingRestore_[number - 1] = kNoEntry;
        if (const auto error = mountOn(number, entry)) {
            restoreProblem(number, "INVALIDA", reason(*error));
            continue;
        }
        restoreDrive_ = number;
        return;  // the next one when this one is applied
    }
    finishRestore();
}

void MenuController::restoreProblem(unsigned number, const char* what, const char* detail) noexcept
{
    if (++problems_ == 1) {
        std::snprintf(problem_.data(), problem_.size(), " D%u: %s %s", number, what, detail);
    }
}

void MenuController::finishRestore() noexcept
{
    restoring_ = false;
    emitted_ = version_;
    confirmed_ = version_;  // what was restored is what is saved
    saveFailed_ = false;
    if (problems_ > 1) {
        say("%s +%u", problem_.data(), problems_ - 1);
    } else if (problems_ == 1) {
        say("%s", problem_.data());
    } else if (restored_ > 0) {
        say(" UNIDADES RESTAURADAS");
    } else {
        message_[0] = '\0';
    }
    char detail[48];
    std::snprintf(detail, sizeof detail, "restored=%u problems=%u", restored_, problems_);
    event("menu_restore", detail);
}

void MenuController::restoreFailed(const char* message) noexcept
{
    if (!restoring_) {
        return;
    }
    pendingRestore_.fill(kNoEntry);
    restoring_ = false;
    emitted_ = version_;
    confirmed_ = version_;
    say("%s", message != nullptr ? message : "");
    event("menu_restore", "failed");
}

uint32_t MenuController::snapshot(DriveMemory& out) noexcept
{
    out = DriveMemory{};
    for (unsigned number = 1; number <= kDrives; ++number) {
        const Shown& shown = shown_[number - 1];
        if (shown.kind == Shown::Kind::Local) {
            (void)out.set(number, catalog_.name(shown.entry));  // a name it refuses is remembered empty
        }
    }
    emitted_ = version_;
    return version_;
}

void MenuController::changed() noexcept
{
    if (!persistent_ || restoring_) {
        return;
    }
    ++version_;
    saveFailed_ = false;
    status(" GUARDANDO");
}

bool MenuController::statusShown() const noexcept
{
    const std::size_t length = std::strlen(status_.data());
    return length > 0 && std::strlen(message_.data()) == statusAt_ + length &&
           std::memcmp(message_.data() + statusAt_, status_.data(), length) == 0;
}

void MenuController::status(const char* text) noexcept
{
    if (statusShown()) {
        message_[statusAt_] = '\0';  // the previous status goes
    }
    const std::size_t length = std::strlen(message_.data());
    const std::size_t add = std::strlen(text);
    statusAt_ = length + add <= kMenuLineWidth ? length : 0;
    std::snprintf(message_.data() + statusAt_, message_.size() - statusAt_, "%s", text);
    std::snprintf(status_.data(), status_.size(), "%s", text);
}

void MenuController::saveResult(uint32_t version, bool ok, int error) noexcept
{
    if (!persistent_ || version != version_) {
        return;  // an older version: a newer one is being saved
    }
    char detail[48];
    if (ok) {
        confirmed_ = version;
        if (statusShown()) {
            status(" GUARDADO");
        }
        std::snprintf(detail, sizeof detail, "version=%lu saved", static_cast<unsigned long>(version));
    } else {
        saveFailed_ = true;
        char text[kMenuLineWidth + 1];
        std::snprintf(text, sizeof text, " NO SE GUARDO: %s", storageReason(error));
        status(text);
        std::snprintf(detail, sizeof detail, "version=%lu error %d", static_cast<unsigned long>(version), error);
    }
    event("menu_save", detail);
}

// An image that came over TNFS: validated like a local one, then assigned to
// D1 or mounted on D2..D8. Refused, its slot goes back to the loader.
void MenuController::place(unsigned number, const Shown& image, const LoaderStatus* xex)
{
    const rb::app::ImageLocation location = tnfsLocation(image.slot);
    std::optional<ErrorCode> error;
    const char* verb = "assigned";
    if (number == 1) {
        const auto opened = repository_.open(location);
        if (!opened.ok()) {
            error = opened.error();
        } else {
            {
                const auto disk = rb::domain::DiskImage::open(*opened.value());  // detaches when it goes
                if (!disk.ok()) {
                    error = disk.error();
                }
            }
            repository_.close(opened.value());
        }
        if (!error) {
            recycle(shown_[0]);
            shown_[0] = image;
            view_ = View::Main;
            say(" D1: %s (A ARRANCA)", image.name);
            changed();  // remembered empty: TNFS images are not restored
        }
    } else {
        const rb::core::Result<rb::app::MountReport> mounted =
            mailbox_.idle() ? mount_.execute(drive(number), location)
                            : rb::core::Result<rb::app::MountReport>{rb::core::Failure{ErrorCode::MailboxBusy}};
        if (!mounted.ok()) {
            error = mounted.error();
        } else {
            previous_ = shown_[number - 1];
            incoming_ = image;
            shown_[number - 1] = image;
            inFlight_ = InFlight::Mount;
            view_ = View::Main;
            say(" D%u: %s", number, image.name);
            verb = "accepted";
        }
    }
    char detail[TnfsFolder::kDisplayWidth + 96];
    if (error) {
        recycle(image);
        say(" ERROR: %s", reason(*error));  // the view stays on the folder
        std::snprintf(detail, sizeof detail, "D%u name=%s refused: error %u", number, image.name,
                      static_cast<unsigned>(*error));
    } else if (xex != nullptr) {
        std::snprintf(detail, sizeof detail, "D%u name=%s %s xex=1 segments=%u bytes=%lu", number, image.name, verb,
                      static_cast<unsigned>(xex->segments), static_cast<unsigned long>(xex->bytes));
    } else {
        std::snprintf(detail, sizeof detail, "D%u name=%s %s", number, image.name, verb);
    }
    event("menu_select", detail);
}

}  // namespace rb::host
