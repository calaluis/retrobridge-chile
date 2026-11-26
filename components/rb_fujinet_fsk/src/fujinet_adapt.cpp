// RetroBridge adapter (written for RetroBridge, not taken from FujiNet): fnio over a TapeSource, the
// SYSTEM_BUS object and the DATA IN pin for the owner's FSK code (adapt/fnio.h, adapt/bus.h,
// lib/include/pinmap.h).
#include <atomic>
#include <cstdint>

#include "bus.h"
#include "fnio.h"
#include "rb/cassette/tape_source.hpp"
#include "rb/core/span.hpp"
#include "../lib/include/pinmap.h"

RbFujinetBus SYSTEM_BUS;

namespace rb::fujinet {
namespace {
std::atomic<int> g_uart2Tx{-1};
}

int uart2TxPin() noexcept { return g_uart2Tx.load(std::memory_order_relaxed); }

void setUart2TxPin(int pin) noexcept { g_uart2Tx.store(pin, std::memory_order_relaxed); }
}  // namespace rb::fujinet

namespace fnio {

int fseek(fnFile* file, long offset, int whence)
{
    if (file == nullptr || file->source == nullptr) {
        return -1;
    }
    const long size = static_cast<long>(file->source->size());
    long base = 0;
    if (whence == SEEK_CUR) {
        base = static_cast<long>(file->position);
    } else if (whence == SEEK_END) {
        base = size;
    } else if (whence != SEEK_SET) {
        return -1;
    }
    const long target = base + offset;
    if (target < 0 || target > size) {
        return -1;
    }
    file->position = static_cast<std::size_t>(target);
    return 0;
}

// Like fread on a regular file: as many whole items as the file still holds from the position.
std::size_t fread(void* dst, std::size_t size, std::size_t count, fnFile* file)
{
    if (file == nullptr || file->source == nullptr || dst == nullptr || size == 0 || count == 0) {
        return 0;
    }
    const std::size_t total = file->source->size();
    const std::size_t left = file->position < total ? total - file->position : 0;
    const std::size_t items = (count <= left / size) ? count : left / size;
    if (items == 0) {
        return 0;
    }
    auto* out = static_cast<uint8_t*>(dst);
    if (!file->source->read(static_cast<uint32_t>(file->position), rb::core::Span<uint8_t>(out, items * size))) {
        return 0;
    }
    file->position += items * size;
    return items;
}

long ftell(fnFile* file)
{
    return file == nullptr ? -1L : static_cast<long>(file->position);
}

}  // namespace fnio
