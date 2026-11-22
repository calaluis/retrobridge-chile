#include "rb/menu/drive_slot_store.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <io.h>
#define RB_FSYNC _commit
#else
#include <unistd.h>
#define RB_FSYNC fsync
#endif

namespace rb::host {

namespace codec = drive_memory_codec;

SlotFileStore::SlotFileStore(const char* directory) noexcept
{
    if (directory == nullptr || std::strlen(directory) > kMaxDirectory) {
        return;
    }
    const char* names[2] = {kSlotA, kSlotB};
    for (int i = 0; i < 2; ++i) {
        std::snprintf(paths_[i].data(), paths_[i].size(), "%s/%s", directory, names[i]);
    }
    valid_ = true;
}

const SlotFileStore::LoadResult& SlotFileStore::load() noexcept
{
    LoadResult& result = result_;
    result = LoadResult{};
    loaded_ = true;
    newest_ = -1;
    newestSeq_ = 0;
    if (!valid_) {
        result.status = LoadStatus::Error;
        result.error = ENAMETOOLONG;
        return result;
    }
    DriveMemorySlot* const slots = slots_;
    bool ok[2] = {false, false};
    int ioError = 0;
    for (int i = 0; i < 2; ++i) {
        errno = 0;
        std::FILE* file = std::fopen(paths_[i].data(), "rb");
        if (file == nullptr) {
            if (errno != ENOENT) {
                ioError = errno != 0 ? errno : EIO;
            }
            continue;
        }
        const std::size_t size = std::fread(readBuffer_.data(), 1, readBuffer_.size(), file);
        if (std::ferror(file) != 0) {
            ioError = EIO;
        } else {
            ok[i] = codec::decode(readBuffer_.data(), size, slots[i]);
        }
        std::fclose(file);
    }
    readError_ = ioError;
    const DriveMemorySlot* best = codec::newest(ok[0] ? &slots[0] : nullptr, ok[1] ? &slots[1] : nullptr);
    if (best != nullptr) {
        newest_ = best == &slots[0] ? 0 : 1;
        newestSeq_ = best->seq;
        result.status = LoadStatus::Loaded;
        result.memory = best->memory;
        result.seq = best->seq;
    } else if (ioError != 0) {
        result.status = LoadStatus::Error;
        result.error = ioError;
    }
    return result;
}

SlotFileStore::SaveResult SlotFileStore::save(const DriveMemory& memory) noexcept
{
    SaveResult result;
    if (!loaded_ || readError_ != 0) {
        (void)load();  // a slot that could not be read may hold the newest memory: read again
    }
    if (!valid_) {
        result.error = ENAMETOOLONG;
        return result;
    }
    if (readError_ != 0) {
        result.error = readError_;  // writing now could put a lower seq over newer memory
        return result;
    }
    if (newest_ >= 0 && newestSeq_ == UINT32_MAX) {
        result.error = EOVERFLOW;  // seq 0 would lose to the old slot
        return result;
    }
    const int target = newest_ == 0 ? 1 : 0;
    const uint32_t seq = newest_ < 0 ? 1 : newestSeq_ + 1;
    codec::Block& block = block_;
    codec::encode(memory, seq, block);

    // In place when the file already is one slot long (the FAT chain does not change); otherwise
    // created or truncated to exactly one slot.
    errno = 0;
    std::FILE* file = std::fopen(paths_[target].data(), "r+b");
    if (file != nullptr && (std::fseek(file, 0, SEEK_END) != 0 || std::ftell(file) != static_cast<long>(codec::kSlotSize) ||
                            std::fseek(file, 0, SEEK_SET) != 0)) {
        std::fclose(file);
        file = nullptr;
        errno = 0;
    }
    if (file == nullptr) {
        file = std::fopen(paths_[target].data(), "wb");
    }
    if (file == nullptr) {
        result.error = errno != 0 ? errno : EIO;
        return result;
    }
    errno = 0;
    bool written = std::fwrite(block.data(), 1, block.size(), file) == block.size();
    written = written && std::fflush(file) == 0;
    written = written && RB_FSYNC(fileno(file)) == 0;
    const int writeError = errno != 0 ? errno : EIO;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        result.error = writeError;
        return result;  // newest_ unchanged: the other slot still holds the last valid memory
    }
    newest_ = target;
    newestSeq_ = seq;
    result.ok = true;
    return result;
}

}  // namespace rb::host
