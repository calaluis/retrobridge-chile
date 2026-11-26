#pragma once
// Host stand-in (cassette-motor-fujinet, fake_idf_fsk.cpp): the heap calls of the owner's FSK engine.
#include <cstddef>
#include <cstdint>
#define MALLOC_CAP_8BIT (1u << 2)
#define MALLOC_CAP_SPIRAM (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
void* heap_caps_malloc(std::size_t size, uint32_t caps);
void* heap_caps_calloc(std::size_t n, std::size_t size, uint32_t caps);
void heap_caps_free(void* ptr);
std::size_t heap_caps_get_free_size(uint32_t caps);
