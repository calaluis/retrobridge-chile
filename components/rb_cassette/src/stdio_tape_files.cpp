#include "rb/cassette/stdio_tape_files.hpp"

namespace rb::cassette {

StdioTapeFiles::~StdioTapeFiles()
{
    for (Slot& slot : slots_) {
        if (slot.file != nullptr) {
            std::fclose(slot.file);
            slot.file = nullptr;
        }
    }
}

TapeSource* StdioTapeFiles::open(const char* path)
{
    if (path == nullptr) {
        return nullptr;
    }
    Slot* free = nullptr;
    for (Slot& slot : slots_) {
        if (slot.file == nullptr) {
            free = &slot;
            break;
        }
    }
    if (free == nullptr) {
        return nullptr;
    }
    char full[kPathMax];
    const int written = std::snprintf(full, sizeof full, "%s%s", root_, path);
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof full) {
        return nullptr;
    }
    std::FILE* file = std::fopen(full, "rb");
    if (file == nullptr) {
        return nullptr;
    }
    // The size, once: a .cas is at most a few MB, well inside 32 bits.
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return nullptr;
    }
    const long size = std::ftell(file);
    if (size < 0 || static_cast<unsigned long>(size) > 0xFFFFFFFFul) {
        std::fclose(file);
        return nullptr;
    }
    free->file = file;
    free->size_ = static_cast<uint32_t>(size);
    return free;
}

void StdioTapeFiles::close(TapeSource* source)
{
    for (Slot& slot : slots_) {
        if (&slot == source && slot.file != nullptr) {
            std::fclose(slot.file);
            slot.file = nullptr;
            slot.size_ = 0;
        }
    }
}

int StdioTapeFiles::openCount() const noexcept
{
    int count = 0;
    for (const Slot& slot : slots_) {
        count += slot.file != nullptr ? 1 : 0;
    }
    return count;
}

bool StdioTapeFiles::Slot::read(uint32_t offset, rb::core::Span<uint8_t> dst)
{
    if (file == nullptr || offset > size_ || dst.size() > size_ - offset) {
        return false;
    }
    if (dst.size() == 0) {
        return true;
    }
    const std::lock_guard<std::mutex> guard(lock);
    if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
        return false;
    }
    return std::fread(dst.data(), 1, dst.size(), file) == dst.size();
}

}  // namespace rb::cassette
