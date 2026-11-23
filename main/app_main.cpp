// RetroBridge Chile — composition root.
//
// H3b: diagnostic firmware. Wires the storage-context diagnostics, the microSD
// storage and the console. No SIO: no UART for the bus, no SIO GPIO (gate H4).
// With the default unconfirmed board profile the microSD is not initialised and
// no board-specific GPIO is configured (H3-11).

#include "esp_log.h"
#include "rb/app/diagnostics/benchmark_reads.hpp"
#include "rb/app/diagnostics/inspect_image.hpp"
#include "rb/app/diagnostics/verify_sectors.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/app/usecases/collect_drive_reply.hpp"
#include "rb/app/usecases/get_drive_status.hpp"
#include "rb/app/usecases/mount_disk_image.hpp"
#include "rb/app/usecases/read_sector.hpp"
#include "rb/app/usecases/unmount_disk_image.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "rb/console/diagnostic_console.hpp"
#include "rb/core/toolchain_checks.hpp"
#include "rb/esp/cassette_service.hpp"
#include "rb/esp/esp_clock.hpp"
#include "rb/esp/esp_system_probe.hpp"
#include "rb/esp/sd_card_storage.hpp"
#include "rb/hwdiag/diagnostics_recorder.hpp"
#include "sdkconfig.h"
#if CONFIG_RB_BENCH_C8
#include "bench_c8.hpp"
#endif
#if CONFIG_RB_MENU_SIO
#include "menu_sio.hpp"
#endif

namespace {

constexpr const char* kTag = "retrobridge";

#if defined(CONFIG_RB_BOARD_UNCONFIRMED)
constexpr rb::board::BoardProfile kBoard = rb::board::unconfirmed_profile();
#elif defined(CONFIG_RB_BOARD_FUJINET_ATARI_V1X)
constexpr rb::board::BoardProfile kBoard = rb::board::fujinet_atari_v1x_profile();  // microSD only, no SIO pins
#else
#error "No board profile selected (see main/Kconfig.projbuild)"
#endif

// Static composition: everything lives for the whole program, no heap (ADR-011).
rb::esp::EspClock g_clock;
rb::esp::SdCardStorage g_storage;
rb::console::ImageRepository g_repository{rb::esp::SdCardStorage::kMountPoint};

// Storage context (the console task, ADR-014).
rb::app::InspectImage g_inspect{g_repository};
rb::app::VerifySectors g_verify{g_repository, rb::console::rom_crc32};
rb::app::BenchmarkReads g_benchmark{g_repository, g_clock};

// Drive context, run in sequence by the console in H3 (H3-7).
rb::app::DriveBay g_bay{1};
rb::app::InlineDriveMailbox g_mailbox;
rb::app::MountDiskImage g_mount{g_repository, g_mailbox};
rb::app::UnmountDiskImage g_unmount{g_mailbox};
rb::app::CollectDriveReply g_collect{g_repository, g_mailbox};
rb::app::ApplyDriveRequest g_apply{g_bay, g_mailbox};
rb::app::GetDriveStatus g_status{g_bay};
rb::app::ReadSector g_read{g_bay};

// Autonomous diagnostics (H4): internal state only, no SIO pin, no ADC.
rb::esp::EspSystemProbe g_system{kBoard.name};
rb::hwdiag::DiagnosticsRecorder g_diagnostics{rb::console::kSectorReadBudgetUs};

// The cassette (cassette-cas C106/C107): MOTOR only behind M3a; DATA IN only behind H4 and M3a.
rb::esp::EspMotorIn g_motor;
rb::esp::EspDataIn g_dataIn;
rb::esp::CassetteService g_cassette{"/sd/", g_motor, g_dataIn};

rb::console::ConsoleContext g_console{kBoard,    g_storage, g_repository, g_inspect, g_verify,
                                      g_benchmark, g_mount, g_unmount,    g_collect, g_apply,
                                      g_status,  g_read,    g_diagnostics, g_system, g_clock,
                                      g_cassette};

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "RetroBridge Chile — diagnostic firmware (H3b), board profile '%s'", kBoard.name);

    const rb::hwdiag::BootRecord boot = g_system.registerBoot();
    g_diagnostics.recordBoot(boot, g_clock.nowMicros());
    ESP_LOGI(kTag, "reset reason %s, boot %u since power-on (watchdog resets %u, panics %u)",
             rb::hwdiag::reset_reason_name(boot.reason), static_cast<unsigned>(boot.bootsSincePowerOn),
             static_cast<unsigned>(boot.watchdogResets), static_cast<unsigned>(boot.panicResets));

#if CONFIG_RB_MENU_SIO
    // Powered only from the SIO, the board starts together with the 65XE: D1 answers before the microSD
    // mount (~0.7 s) and the catalog, so the first boot attempt of the Atari finds the menu.
    const bool menuStarted = rb::menusio::start(kBoard);
    if (!menuStarted) {
        ESP_LOGE(kTag, "menu-sio: the menu is not served");
    }
#endif

    const auto mounted = g_storage.mount(kBoard);  // refused without touching hardware if unconfirmed
    g_diagnostics.recordMount(mounted.ok(), g_storage.lastEspError(),
                              mounted.ok() ? 0 : static_cast<uint8_t>(mounted.error()), g_clock.nowMicros());
    if (!mounted.ok()) {
        if (!kBoard.hardwareConfirmed) {
            ESP_LOGW(kTag, "HARDWARE NOT VALIDATED: microSD, board GPIOs and SIO are disabled until the board "
                           "is identified (H3c/H4)");
        } else {
            ESP_LOGE(kTag, "microSD mount failed (esp_err 0x%x)", static_cast<unsigned>(g_storage.lastEspError()));
        }
    }

#if CONFIG_RB_MENU_SIO
    // Test package menu-sio (menu-sio-fisico): the menu as D1 on the physical SIO. The cassette starts
    // in finish(), after D1 (menu-cinta T6), with DATA IN not configured: it would share UART2 and GPIO21
    // with the SIO, so its outputs are discarded, and MOTOR is not read (T7 hands DATA IN over).
    if (!rb::console::start_diagnostic_console(g_console)) {
        ESP_LOGE(kTag, "diagnostic console could not start");
    }
    if (menuStarted) {
        rb::menusio::finish(mounted.ok(), boot, g_cassette);  // catalog, cassette, console commands, boot record
    }
    return;
#endif
    // The cassette needs the microSD (its tapes) and PSRAM (its bank).
    (void)g_motor.configure(kBoard);
#if CONFIG_RB_BENCH_C8
    // Bench test C8 (H4-C): DATA IN on the board alone, then released. The cassette never drives it.
    (void)rb::bench::run_c8(kBoard, g_dataIn);
#else
    // DATA IN only with both gates signed; otherwise no SIO pin is touched.
    const auto output = rb::board::validate_for_cassette_output(kBoard);
    if (output.ok()) {
        (void)g_dataIn.configure(output.value());
    }
#endif
    if (mounted.ok() && !g_cassette.start()) {
        ESP_LOGW(kTag, "cassette not started");
    }

    if (!rb::console::start_diagnostic_console(g_console)) {
        ESP_LOGE(kTag, "diagnostic console could not start");
    }
}
