#include "menu_controller.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>

#include "host_image_repository.hpp"
#include "rb/app/image_location.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/domain/drive_number.hpp"

namespace rb::host {

using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::DriveNumber;

static_assert(kListMaxEntries == MenuCatalog::kMaxEntries, "the list shows the whole catalog");

namespace {

// Atari keyboard codes (CH, Shift and Control bits cleared).
constexpr uint8_t kDigitCodes[MenuController::kDrives] = {0x1F, 0x1E, 0x1A, 0x18, 0x1D, 0x1B, 0x33, 0x35};  // 1..8
constexpr uint8_t kLetterCodes[MenuCatalog::kMaxEntries] = {
    0x3F, 0x15, 0x12, 0x3A, 0x2A, 0x38, 0x3D, 0x39, 0x0D, 0x01,  // A B C D E F G H I J
    0x05, 0x00, 0x25, 0x23, 0x08, 0x0A, 0x2F, 0x28, 0x3E, 0x2D,  // K L M N O P Q R S T
};
constexpr uint8_t kKeyA = 0x3F;
constexpr uint8_t kKeyE = 0x2A;
constexpr uint8_t kKeyU = 0x0B;
constexpr uint8_t kKeyEsc = 0x1C;

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
    for (unsigned i = 0; i < MenuCatalog::kMaxEntries; ++i) {
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
    if (number < 1 || number > kDrives || shown_[number - 1] == kNone) {
        return nullptr;
    }
    return catalog_.name(static_cast<std::size_t>(shown_[number - 1]));
}

void MenuController::onColdReset() noexcept
{
    coldResetPending_ = true;
    view_ = View::Main;
    chosen_ = kNone;
    message_[0] = '\0';
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
            say(" ERROR: D%u NO CAMBIO", number);
        } else if (inFlight_ == InFlight::Boot) {
            say(" ERROR: D1 NO SE PUDO MONTAR");
        }
    } else {
        if (inFlight_ == InFlight::Menu || inFlight_ == InFlight::Boot) {
            menuOnD1_ = inFlight_ == InFlight::Menu;
        }
        const char* what = inFlight_ == InFlight::Menu    ? "menu"
                           : inFlight_ == InFlight::Boot  ? "image"
                           : inFlight_ == InFlight::Mount ? "mounted"
                                                          : "empty";
        std::snprintf(detail, sizeof detail, "D%u=%s", number, what);
    }
    inFlight_ = InFlight::None;
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
    if (code == kMenuNoKey) {
        return render(kMenuActionShow);
    }
    const uint8_t key = code & 0x3F;  // Shift and Control do not change the meaning
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
            chosen_ = kNone;
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
    }
    return render(kMenuActionShow);
}

void MenuController::onMain(int digit, uint8_t key)
{
    char detail[96];
    if (digit == 1) {
        view_ = View::List;
        message_[0] = '\0';
        std::snprintf(detail, sizeof detail, "source=1 local entries=%u", static_cast<unsigned>(catalog_.count()));
        event("menu_open", detail);
    } else if (digit >= 2) {
        const char* name = sources_.name(static_cast<unsigned>(digit));
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
    const int entry = letterOf(key);
    if (entry < 0 || static_cast<std::size_t>(entry) >= catalog_.count()) {
        return;  // not a letter of the list
    }
    chosen_ = static_cast<int8_t>(entry);
    view_ = View::Pick;
    say(" UNIDAD 1-8 (ESC VUELVE): %s", catalog_.name(static_cast<std::size_t>(entry)));
}

void MenuController::onPick(int digit)
{
    const int8_t entry = chosen_;
    chosen_ = kNone;
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

void MenuController::assignD1(int8_t entry)
{
    const char* name = catalog_.name(static_cast<std::size_t>(entry));
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
        shown_[0] = entry;
        say(" D1: %s (A ARRANCA)", name);
        std::snprintf(detail, sizeof detail, "D1 name=%s assigned", name);
    }
    event("menu_select", detail);
}

void MenuController::mountOn(unsigned number, int8_t entry)
{
    const char* name = catalog_.name(static_cast<std::size_t>(entry));
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
        shown_[number - 1] = entry;
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
    if (shown_[number - 1] == kNone) {
        say(" D%u YA ESTA VACIA", number);
        return;
    }
    if (number == 1) {
        shown_[0] = kNone;  // D1 serves the menu: there is nothing mounted to remove
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
        shown_[number - 1] = kNone;
        inFlight_ = InFlight::Unmount;
        say(" D%u DESMONTADA", number);
        std::snprintf(detail, sizeof detail, "D%u requested", number);
    }
    event("menu_unmount", detail);
}

bool MenuController::boot()
{
    if (shown_[0] == kNone) {
        say(" D1 SIN DISCO: ABRE LA FUENTE 1");
        return false;
    }
    const char* name = catalog_.name(static_cast<std::size_t>(shown_[0]));
    const auto location = rb::app::ImageLocation::from(name);
    const rb::core::Result<rb::app::MountReport> mounted =
        mailbox_.idle() ? mount_.execute(drive(1), location.value())
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
    if (view_ == View::List) {
        reply.bytes[1] = kMenuLayoutList;
        std::memcpy(screen, kMenuListTemplate, kMenuScreenSize);
        put(screen, kListTitle, kMenuTitleWidth, " FUENTE 1: SD", true);
        for (std::size_t i = 0; i < catalog_.count(); ++i) {
            std::snprintf(text, sizeof text, " %c  %.*s", static_cast<char>('A' + i), static_cast<int>(kNameColumns),
                          catalog_.name(i));
            put(screen, static_cast<unsigned>(kListEntries + i * kMenuLineWidth), kMenuLineWidth, text, true);
        }
        put(screen, kListMessage, kMenuLineWidth, message_[0] != '\0' ? message_.data() : " PULSA LA LETRA DE LA IMAGEN",
            true);
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
    put(screen, kMainMessage, kMenuLineWidth, message_.data(), true);
    return reply;
}

}  // namespace rb::host
