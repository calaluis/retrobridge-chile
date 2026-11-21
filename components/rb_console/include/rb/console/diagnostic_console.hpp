#pragma once

#include "rb/app/diagnostics/benchmark_reads.hpp"
#include "rb/app/diagnostics/inspect_image.hpp"
#include "rb/app/diagnostics/verify_sectors.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/app/usecases/collect_drive_reply.hpp"
#include "rb/app/usecases/get_drive_status.hpp"
#include "rb/app/usecases/mount_disk_image.hpp"
#include "rb/app/usecases/read_sector.hpp"
#include "rb/app/usecases/unmount_disk_image.hpp"
#include "rb/app/ports/clock.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/esp/cassette_service.hpp"
#include "rb/esp/sd_card_storage.hpp"
#include "rb/hwdiag/diagnostics_recorder.hpp"
#include "rb/hwdiag/system_probe.hpp"
#include "rb/storage/stdio_image_repository.hpp"

namespace rb::console {

constexpr std::size_t kImageSlots = 3;  // H3-4
using ImageRepository = rb::storage::StdioImageRepository<kImageSlots>;

constexpr uint32_t kSectorReadBudgetUs = 5000;  // architecture §9.3 / H3-12

// Everything the console commands use, wired by the composition root (main).
// The console task is the storage context (ADR-014). The `drive` commands run
// the drive-context use cases in the same task, strictly in sequence (H3-7);
// that is why the mailbox is the single-task InlineDriveMailbox.
struct ConsoleContext {
    const rb::board::BoardProfile& board;
    rb::esp::SdCardStorage& storage;
    ImageRepository& repository;
    rb::app::InspectImage& inspect;
    rb::app::VerifySectors& verify;
    rb::app::BenchmarkReads& benchmark;
    rb::app::MountDiskImage& mountImage;
    rb::app::UnmountDiskImage& unmountImage;
    rb::app::CollectDriveReply& collectReply;
    rb::app::ApplyDriveRequest& applyRequest;
    rb::app::GetDriveStatus& driveStatus;
    rb::app::ReadSector& readSector;
    // Autonomous diagnostics (H4): what the commands observe, and the platform's state.
    rb::hwdiag::DiagnosticsRecorder& diagnostics;
    const rb::hwdiag::SystemProbe& system;
    rb::app::Clock& clock;
    // The cassette (cassette-cas C106): `cas ...` runs on its own loop task.
    rb::esp::CassetteService& cassette;
};

// CRC-32 of the ROM (esp_rom_crc32_le), as a Crc32Function for VerifySectors.
uint32_t rom_crc32(const uint8_t* data, std::size_t length);

// Registers the commands and starts the REPL task. Returns false on failure.
bool start_diagnostic_console(ConsoleContext& context);

}  // namespace rb::console
