#include "menu_controller.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>

#include "host_image_repository.hpp"
#include "memory_image_store.hpp"
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
constexpr uint8_t kKeyEsc = 0x1C;
constexpr uint8_t kKeyReturn = 0x0C;
constexpr std::size_t kPageRows = TnfsFolder::kPageRows;

bool isNavigation(uint8_t code) noexcept { return code >= kMenuNavUp && code <= kMenuNavFire; }

constexpr std::size_t kNameColumns = 30;   // list entries: " A  " + 30
constexpr std::size_t kLabelColumns = 32;  // sources and drives: "D1 " + 32 (the reference's 35 columns)

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
    if (outcome->error) {
        std::snprintf(detail, sizeof detail, "D%u not applied: error %u", number,
                      static_cast<unsigned>(*outcome->error));
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
        }
        const char* what = inFlight_ == InFlight::Menu    ? "menu"
                           : inFlight_ == InFlight::Boot  ? "image"
                           : inFlight_ == InFlight::Mount ? "mounted"
                                                          : "empty";
        std::snprintf(detail, sizeof detail, "D%u=%s", number, what);
    }
    inFlight_ = InFlight::None;
    previous_ = Shown{};
    incoming_ = Shown{};
    event("menu_mount", detail);
}

void MenuController::service()
{
    collect();
    if (!coldResetPending_ || !mailbox_.idle()) {
        return;
    }
    if (menuOnD1_) {
        coldResetPending_ = false;  // a cold reset with the menu already on D1 changes nothing
        return;
    }
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

MenuReply MenuController::key(uint8_t code)
{
    if (job_ != Job::None) {
        return onLoading(code);
    }
    // $FF (redraw) and $FE (poll) are not keys: compared before the mask.
    if (code == kMenuNoKey || code == kMenuPollKey) {
        return render(kMenuActionShow);
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
        } else {
            onMain(digit, key);
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
        } else if (digit != 0) {
            onPick(digit);
        }
        break;
    case View::Unmount:
        if (key == kKeyEsc) {
            view_ = View::Main;
            message_[0] = '\0';
        } else if (digit != 0) {
            onUnmount(digit);
        }
        break;
    case View::Folder:
        onFolder(key);
        break;
    case View::FolderPick:
        onFolderPick(key, digit);
        break;
    }
    if (job_ != Job::None) {
        return advance();  // the key started a TNFS job
    }
    return render(kMenuActionShow);
}

void MenuController::onMain(int digit, uint8_t key)
{
    char detail[96];
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
        say(" PENDIENTE: E NECESITA ENTRADA DE TEXTO");
        event("menu_pending", "edit server: needs text input on the Atari and a writable source list");
    } else if (key == kKeyU) {
        view_ = View::Unmount;
        say(" DESMONTAR: PULSA 1-8, ESC CANCELA");
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
    say(" UNIDAD 1-8 (ESC VUELVE): %s", catalog_.name(entry));
}

MenuController::EntryIndex MenuController::listKey(Cursor& cursor, std::size_t count, uint8_t key) noexcept
{
    if (count == 0) {
        return kNoEntry;  // nothing marked: moves, RETURN and fire do nothing
    }
    const std::size_t pages = (count + kPageRows - 1) / kPageRows;
    const std::size_t rows = std::min(kPageRows, count - cursor.page * kPageRows);  // on this page
    switch (key) {
    case kMenuNavUp:
        if (cursor.row > 0) {
            --cursor.row;
        } else if (cursor.page > 0) {
            --cursor.page;
            cursor.row = static_cast<uint8_t>(kPageRows - 1);
        }
        return kNoEntry;
    case kMenuNavDown:
        if (cursor.row + 1u < rows) {
            ++cursor.row;
        } else if (cursor.page + 1u < pages) {
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

void MenuController::markRow(uint8_t* screen, const Cursor& cursor, std::size_t count) noexcept
{
    if (count == 0) {
        return;
    }
    uint8_t* line = screen + kListEntries + cursor.row * kMenuLineWidth;
    for (unsigned i = 0; i < kMenuLineWidth; ++i) {
        line[i] = static_cast<uint8_t>(line[i] | 0x80);  // inverse video, edge to edge
    }
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
    if (digit == 1) {
        assignD1(entry);
    } else {
        mountOn(static_cast<unsigned>(digit), entry);
    }
}

void MenuController::onUnmount(int digit)
{
    view_ = View::Main;
    emptyDrive(static_cast<unsigned>(digit));
}

void MenuController::assignD1(EntryIndex entry)
{
    const char* name = catalog_.name(entry);
    // Every catalog name was accepted by ImageLocation::from when it was listed.
    const auto location = rb::app::ImageLocation::from(name);
    // D1 keeps serving the menu: validate now (open the file, read its header),
    // mount when A is pressed.
    std::optional<ErrorCode> error;
    const auto opened = repository_.open(location.value());
    if (!opened.ok()) {
        error = opened.error();
    } else {
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
        recycle(shown_[0]);  // D1 is assigned, not mounted: nothing holds its slot
        shown_[0] = local;
        say(" D1: %s (A ARRANCA)", name);
        std::snprintf(detail, sizeof detail, "D1 name=%s assigned", name);
    }
    event("menu_select", detail);
}

void MenuController::mountOn(unsigned number, EntryIndex entry)
{
    const char* name = catalog_.name(entry);
    const auto location = rb::app::ImageLocation::from(name);
    const rb::core::Result<rb::app::MountReport> mounted =
        mailbox_.idle() ? mount_.execute(drive(number), location.value())
                        : rb::core::Result<rb::app::MountReport>{rb::core::Failure{ErrorCode::MailboxBusy}};
    char detail[MenuCatalog::kMaxName + 64];
    if (!mounted.ok()) {
        say(" ERROR: %s", reason(mounted.error()));
        std::snprintf(detail, sizeof detail, "D%u name=%s refused: error %u", number, name,
                      static_cast<unsigned>(mounted.error()));
    } else {
        previous_ = shown_[number - 1];
        incoming_ = Shown{};
        incoming_.kind = Shown::Kind::Local;
        incoming_.entry = entry;
        shown_[number - 1] = incoming_;
        inFlight_ = InFlight::Mount;
        say(" D%u: %s", number, name);
        std::snprintf(detail, sizeof detail, "D%u name=%s accepted: %u sectors of %u bytes", number, name,
                      static_cast<unsigned>(mounted.value().geometry.sectorCount()),
                      static_cast<unsigned>(mounted.value().geometry.sectorSize()));
    }
    event("menu_select", detail);
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
    message_[0] = '\0';
    std::snprintf(detail, sizeof detail, "D1 name=%s", name);
    event("menu_boot", detail);
    return true;
}

MenuReply MenuController::render(uint8_t action) const noexcept
{
    MenuReply reply;
    reply.bytes[0] = action;
    uint8_t* screen = reply.bytes.data() + 2;
    char text[kMenuLineWidth + 64];
    if (view_ == View::Folder || view_ == View::FolderPick) {
        reply.bytes[1] = kMenuLayoutList;
        std::memcpy(screen, kMenuListTemplate, kMenuScreenSize);
        renderFolder(screen, text, sizeof text);
        return reply;
    }
    if (view_ == View::List) {
        reply.bytes[1] = kMenuLayoutList;
        std::memcpy(screen, kMenuListTemplate, kMenuScreenSize);
        put(screen, kListTitle, kMenuTitleWidth, " FUENTE 1: SD", true);
        const std::size_t first = listCursor_.page * kPageRows;  // only the visible rows are drawn
        for (std::size_t i = 0; i < kPageRows && first + i < catalog_.count(); ++i) {
            std::snprintf(text, sizeof text, " %c  %.*s", static_cast<char>('A' + i), static_cast<int>(kNameColumns),
                          catalog_.name(first + i));
            put(screen, static_cast<unsigned>(kListEntries + i * kMenuLineWidth), kMenuLineWidth, text, true);
        }
        markRow(screen, listCursor_, catalog_.count());
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
    for (unsigned n = 2; n <= kDrives; ++n) {
        const char* name = sources_.name(n);
        std::snprintf(text, sizeof text, "%u  %.*s", n, static_cast<int>(kLabelColumns), name != nullptr ? name : "");
        put(screen, kMainSources + (n - 1) * kMenuLineWidth, kMenuLineWidth, text, false);
    }
    for (unsigned n = 1; n <= kDrives; ++n) {
        const char* name = driveName(n);
        std::snprintf(text, sizeof text, "D%u %.*s", n, static_cast<int>(kLabelColumns),
                      name != nullptr ? name : "SIN DISCO");
        put(screen, kMainDrives + (n - 1) * kMenuLineWidth, kMenuLineWidth, text, true);
    }
    put(screen, kMainTape, kMenuLineWidth, "C: SIN CINTA", true);
    if (job_ != Job::None) {
        std::snprintf(text, sizeof text, " CARGANDO %u%%", static_cast<unsigned>(percent_));
        put(screen, kMainMessage, kMenuLineWidth, text, true);
    } else {
        put(screen, kMainMessage, kMenuLineWidth, message_.data(), true);
    }
    return reply;
}

// ---------------------------------------------------------------------------
// TNFS sources (menu-tnfs T012).

void MenuController::renderFolder(uint8_t* screen, char* text, std::size_t size) const noexcept
{
    // " FUENTE n: " and the folder, cut on the left so the current one shows.
    const int prefix = std::snprintf(text, size, " FUENTE %u: ", source_);
    const std::size_t room = kMenuTitleWidth > static_cast<unsigned>(prefix) ? kMenuTitleWidth - prefix : 0;
    tnfsTitle(path_, pathLength_, room, text + prefix);
    put(screen, kListTitle, kMenuTitleWidth, text, true);
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
    say(" UNIDAD 1-8 (ESC VUELVE): %s", chosen.display);
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
    jobNumber_ = job == Job::Fetch ? loader_->startFetch(source, path, length) : loader_->startList(source, path, length);
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
    place(jobDrive_, image);
}

// An image that came over TNFS: validated like a local one, then assigned to
// D1 or mounted on D2..D8. Refused, its slot goes back to the loader.
void MenuController::place(unsigned number, const Shown& image)
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
    char detail[TnfsFolder::kDisplayWidth + 64];
    if (error) {
        recycle(image);
        say(" ERROR: %s", reason(*error));  // the view stays on the folder
        std::snprintf(detail, sizeof detail, "D%u name=%s refused: error %u", number, image.name,
                      static_cast<unsigned>(*error));
    } else {
        std::snprintf(detail, sizeof detail, "D%u name=%s %s", number, image.name, verb);
    }
    event("menu_select", detail);
}

}  // namespace rb::host
