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
#include "rb/board/board_profile.hpp"
#include "rb/esp/sd_card_storage.hpp"
#include "rb/storage/stdio_image_repository.hpp"

namespace rb::console {

constexpr std::size_t kImageSlots = 3;  // H3-4
using ImageRepository = rb::storage::StdioImageRepository<kImageSlots>;

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
};

// CRC-32 of the ROM (esp_rom_crc32_le), as a Crc32Function for VerifySectors.
uint32_t rom_crc32(const uint8_t* data, std::size_t length);

// Registers the commands and starts the REPL task. Returns false on failure.
bool start_diagnostic_console(ConsoleContext& context);

}  // namespace rb::console
