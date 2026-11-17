#include "menu_controller.hpp"

#include <cstdio>

#include "host_image_repository.hpp"
#include "menu_image.hpp"
#include "rb/app/image_location.hpp"
#include "rb/domain/drive_number.hpp"

namespace rb::host {

using rb::core::ErrorCode;

namespace {

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

MenuReply reply(uint8_t status, const char* text) noexcept
{
    MenuReply out;
    out.bytes[0] = status;
    screenLine(text, rb::core::Span<uint8_t>{out.bytes.data() + 1, MenuReply::kSize - 1});
    return out;
}

const rb::domain::DriveNumber kD1 = rb::domain::DriveNumber::from(1).value();

}  // namespace

void MenuController::event(const char* name, const char* detail)
{
    if (events_ != nullptr) {
        events_->menuEvent(name, detail);
    }
}

void MenuController::collect()
{
    const auto outcome = collect_.execute();
    if (!outcome) {
        return;
    }
    char detail[64];
    if (outcome->error) {
        std::snprintf(detail, sizeof detail, "%s not applied: error %u", inFlight_ == InFlight::Menu ? "menu" : "image",
                      static_cast<unsigned>(*outcome->error));
    } else {
        menuOnD1_ = inFlight_ == InFlight::Menu;
        std::snprintf(detail, sizeof detail, "D1=%s", menuOnD1_ ? "menu" : "image");
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
    const auto mounted = mount_.execute(kD1, menu.value());
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

MenuReply MenuController::select(uint8_t index)
{
    char text[kMenuLineWidth + 1];
    char detail[MenuCatalog::kMaxName + 64];
    if (index >= catalog_.count()) {
        std::snprintf(detail, sizeof detail, "index=%u refused: outside the list of %u", index,
                      static_cast<unsigned>(catalog_.count()));
        event("menu_select", detail);
        return reply(1, " ERROR: OPCION FUERA DE LA LISTA");
    }
    const char* name = catalog_.name(index);
    // Every catalog name was accepted by ImageLocation::from when it was listed.
    const auto location = rb::app::ImageLocation::from(name);
    const rb::core::Result<rb::app::MountReport> mounted =
        mailbox_.idle() ? mount_.execute(kD1, location.value())
                        : rb::core::Result<rb::app::MountReport>{rb::core::Failure{ErrorCode::MailboxBusy}};
    if (!mounted.ok()) {
        std::snprintf(detail, sizeof detail, "index=%u name=%s refused: error %u", index, name,
                      static_cast<unsigned>(mounted.error()));
        event("menu_select", detail);
        std::snprintf(text, sizeof text, " ERROR: %s", reason(mounted.error()));
        return reply(1, text);
    }
    inFlight_ = InFlight::Image;
    std::snprintf(detail, sizeof detail, "index=%u name=%s accepted: %u sectors of %u bytes", index, name,
                  static_cast<unsigned>(mounted.value().geometry.sectorCount()),
                  static_cast<unsigned>(mounted.value().geometry.sectorSize()));
    event("menu_select", detail);
    // The line is kMenuLineWidth columns; the name gets what the prefix leaves.
    constexpr char kBooting[] = " ARRANCANDO ";
    std::snprintf(text, sizeof text, "%s%.*s", kBooting, static_cast<int>(kMenuLineWidth - (sizeof kBooting - 1)),
                  name);
    return reply(0, text);
}

}  // namespace rb::host
