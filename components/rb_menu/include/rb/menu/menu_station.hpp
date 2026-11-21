#pragma once

// The boot menu served as D1 (menu-sio-fisico): the composition retrobridge_host
// used to build in main(), as one class, so the firmware serves the same menu
// over the physical SIO without copying the wiring. The caller keeps what
// depends on its platform: the transport under the menu (NetSIO on the PC, the
// SIO line on the FujiNet), the clock, the image repository, the catalog and
// sources, the events and the TNFS loader.
//
//   start()        D1 gets the menu, as after a cold reset: the next boot of
//                  the Atari loads the menu.
//   step()         one SioSession step, then the controller collects what the
//                  step did (and mounts the menu again if it is pending).
//   returnToMenu() what a COLD_RESET does on NetSIO: D1 goes back to the menu
//                  before the next command, and the menu starts on its main
//                  screen. On the physical SIO there is no such message: the
//                  FujiNet button and the console command `menu` call it.
//
// No allocation after construction (ADR-011).

#include "rb/app/drive_bay.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/app/ports/sio_transport.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "rb/menu/menu_controller.hpp"
#include "rb/menu/menu_sio_transport.hpp"
#include "rb/menu/menu_sources.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"
#include "rb/sio/sio_session.hpp"

namespace rb::host {

struct MenuStationParts {
    const MenuCatalog& catalog;
    MenuSources& sources;
    rb::app::ImageRepository& repository;
    MenuEvents* events = nullptr;
    MenuLoader* loader = nullptr;
    SioWriteObserver* writes = nullptr;
    bool allowWrites = false;  // SIO writes reach the images (menu-escritura-disco)
};

class MenuStation {
public:
    MenuStation(rb::app::SioTransport& inner, rb::app::Clock& clock, const MenuStationParts& parts) noexcept
        : bay_(MenuController::kDrives),
          controller_(parts.catalog, parts.sources, parts.repository, mailbox_, parts.events, parts.loader),
          transport_(inner, clock, controller_, parts.writes),
          dispatcher_(bay_, parts.allowWrites),
          apply_(bay_, mailbox_),
          session_(transport_, clock, dispatcher_, apply_)
    {
    }

    MenuStation(const MenuStation&) = delete;
    MenuStation& operator=(const MenuStation&) = delete;

    void start()
    {
        returning_ = true;
        controller_.onColdReset();
        controller_.service();
    }

    rb::sio::StepOutcome step()
    {
        const rb::sio::StepOutcome outcome = session_.step();
        controller_.service();
        if (controller_.menuOnD1()) {
            returning_ = false;
        }
        return outcome;
    }

    void returnToMenu() noexcept
    {
        returning_ = true;
        controller_.onColdReset();
    }

    // True while D1 serves the menu, and from start() or returnToMenu() until it
    // is mounted: the menu is what the next boot gets. D1 frames that arrive in
    // between are dropped (the Atari retries them), never served from the
    // previous image.
    bool menuOnD1() const noexcept { return returning_ || controller_.menuOnD1(); }

    MenuController& controller() noexcept { return controller_; }
    rb::app::DriveBay& bay() noexcept { return bay_; }
    rb::sio::SioSession& session() noexcept { return session_; }
    const MenuTransportStats& stats() const noexcept { return transport_.stats(); }

private:
    rb::app::DriveBay bay_;
    rb::app::InlineDriveMailbox mailbox_;
    MenuController controller_;
    MenuSioTransport transport_;
    rb::sio::SioCommandDispatcher dispatcher_;
    rb::app::ApplyDriveRequest apply_;
    rb::sio::SioSession session_;
    bool returning_ = false;
};

}  // namespace rb::host
