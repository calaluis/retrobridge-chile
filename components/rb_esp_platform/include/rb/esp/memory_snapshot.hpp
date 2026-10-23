#pragma once

#include <cstddef>

namespace rb::esp {

// Heap figures for diagnostics and the H3-13 check (reads must not change them).
struct MemorySnapshot {
    std::size_t free8bit;          // heap_caps_get_free_size(MALLOC_CAP_8BIT)
    std::size_t minimumFree8bit;   // lowest free size since boot
    std::size_t largestFree8bit;   // largest allocatable block
    std::size_t freeInternal;      // internal RAM only (not PSRAM)
};

MemorySnapshot capture_memory() noexcept;

}  // namespace rb::esp
