#pragma once

// D1..D8 remembered on the microSD (menu-unidades-persistentes): two slot files of exactly one slot
// each, kSlotA and kSlotB, in a directory (the card root on the FujiNet). save() writes the slot that
// does NOT hold the newest valid memory, with the next sequence, in place, then flushes, syncs and
// closes it: ok only if all of that succeeds. load() returns the valid slot with the highest sequence.
// A power cut halfway through a write leaves that slot invalid, so the other one wins: at most that
// one write is lost. The guarantee needs one valid slot to survive and the card to be readable.
// stdio only (plus fsync): the PC and ESP-IDF both provide it. Never called from the SIO task.
//
// Stack: every buffer about a slot long (the two slots read, the read buffer, the block written and
// the load result) is a member, so load() and save() keep small frames on the FujiNet's task stacks
// (app_main has 3584 B, and stdio and FatFs need theirs). There, create the store on the heap.

#include <array>
#include <cstdint>

#include "rb/menu/drive_memory.hpp"

namespace rb::host {

class SlotFileStore {
public:
    static constexpr const char* kSlotA = "retrobridge-unidades-a.dat";
    static constexpr const char* kSlotB = "retrobridge-unidades-b.dat";
    static constexpr std::size_t kMaxDirectory = 200;

    enum class LoadStatus : uint8_t { Loaded, Empty, Error };
    struct LoadResult {
        LoadStatus status = LoadStatus::Empty;  // Empty: no valid slot; Error: a slot could not be read
        DriveMemory memory;
        uint32_t seq = 0;
        int error = 0;  // errno for Error
    };
    struct SaveResult {
        bool ok = false;
        int error = 0;  // errno when !ok
    };

    // `directory` without a trailing slash, at most kMaxDirectory characters.
    explicit SlotFileStore(const char* directory) noexcept;

    // The result stays valid until the next load() or save() of this store; copy it to keep it.
    const LoadResult& load() noexcept;

    // Calls load() first if it was never called, or if a slot could not be read last time, to
    // know which slot is the newest. Refuses (error set) while a slot cannot be read.
    SaveResult save(const DriveMemory& memory) noexcept;

private:
    static constexpr std::size_t kPathSize = kMaxDirectory + 32;
    using Path = std::array<char, kPathSize>;

    bool valid_ = false;
    Path paths_[2]{};
    bool loaded_ = false;
    int newest_ = -1;  // 0 = A, 1 = B, -1 = no valid slot
    uint32_t newestSeq_ = 0;
    int readError_ = 0;  // errno of a slot the last load() could not read; save() refuses then

    // Off the caller's stack (see the top of this file).
    LoadResult result_;
    DriveMemorySlot slots_[2];
    std::array<uint8_t, drive_memory_codec::kSlotSize + 1> readBuffer_{};  // one byte more: a longer file is not a slot
    drive_memory_codec::Block block_{};
};

}  // namespace rb::host
