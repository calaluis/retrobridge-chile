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
#include "rb/esp/esp_clock.hpp"
#include "rb/esp/sd_card_storage.hpp"
#include "sdkconfig.h"

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

rb::console::ConsoleContext g_console{kBoard,    g_storage, g_repository, g_inspect, g_verify,
                                      g_benchmark, g_mount, g_unmount,    g_collect, g_apply,
                                      g_status,  g_read};

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "RetroBridge Chile — diagnostic firmware (H3b), board profile '%s'", kBoard.name);

    const auto mounted = g_storage.mount(kBoard);  // refused without touching hardware if unconfirmed
    if (!mounted.ok()) {
        if (!kBoard.hardwareConfirmed) {
            ESP_LOGW(kTag, "HARDWARE NOT VALIDATED: microSD, board GPIOs and SIO are disabled until the board "
                           "is identified (H3c/H4)");
        } else {
            ESP_LOGE(kTag, "microSD mount failed (esp_err 0x%x)", static_cast<unsigned>(g_storage.lastEspError()));
        }
    }

    if (!rb::console::start_diagnostic_console(g_console)) {
        ESP_LOGE(kTag, "diagnostic console could not start");
    }
}
