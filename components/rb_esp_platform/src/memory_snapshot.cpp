#include "rb/esp/memory_snapshot.hpp"

#include "esp_heap_caps.h"

namespace rb::esp {

MemorySnapshot capture_memory() noexcept
{
    return MemorySnapshot{heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                          heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)};
}

}  // namespace rb::esp
