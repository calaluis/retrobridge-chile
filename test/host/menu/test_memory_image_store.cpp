// MemoryImageStore and the *tnfs*<n> locations of HostImageRepository
// (menu-tnfs T009, M41-2): nine slots that hold whole images brought over TNFS;
// a slot is never freed while its source is open or attached to a drive; the
// menu disk and the files of --images open as before.

#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

#include "host_image_repository.hpp"
#include "memory_image_store.hpp"
#include "menu_image.hpp"
#include "rb/app/image_location.hpp"
#include "rb/domain/disk_image.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif

using rb::app::ImageLocation;
using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::DiskImage;
using rb::domain::ImageByteSource;
using rb::host::HostImageRepository;
using rb::host::MemoryImageStore;
using rb::host::MenuImage;

namespace {

using Bytes = std::vector<uint8_t>;

// A single-density ATR of `sectors` sectors; byte i of the data is i & 0xFF.
Bytes atr(uint32_t sectors = 720)
{
    const uint32_t data = sectors * 128u;
    Bytes out(16 + data, 0);
    const uint32_t paragraphs = data / 16u;
    out[0] = 0x96;
    out[1] = 0x02;
    out[2] = static_cast<uint8_t>(paragraphs);
    out[3] = static_cast<uint8_t>(paragraphs >> 8);
    out[4] = 128;
    out[6] = static_cast<uint8_t>(paragraphs >> 16);
    for (uint32_t i = 0; i < data; ++i) {
        out[16 + i] = static_cast<uint8_t>(i);
    }
    return out;
}

ImageLocation location(const char* text)
{
    return ImageLocation::from(text).value();
}

void tnfs(std::size_t slot, char (&out)[8])
{
    std::memcpy(out, "*tnfs*0", 8);
    out[6] = static_cast<char>('0' + slot);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_nine_slots_then_no_free_slot(void)
{
    MemoryImageStore store;
    TEST_ASSERT_EQUAL_size_t(9, MemoryImageStore::kSlots);
    bool seen[MemoryImageStore::kSlots] = {};
    for (std::size_t i = 0; i < MemoryImageStore::kSlots; ++i) {
        auto slot = store.adopt(Bytes(10, static_cast<uint8_t>(i)));
        TEST_ASSERT_TRUE(slot.ok());
        TEST_ASSERT_TRUE(slot.value() < MemoryImageStore::kSlots);
        TEST_ASSERT_FALSE(seen[slot.value()]);
        seen[slot.value()] = true;
        TEST_ASSERT_TRUE(store.inUse(slot.value()));
    }
    auto full = store.adopt(Bytes(10));
    TEST_ASSERT_FALSE(full.ok());
    TEST_ASSERT_TRUE(full.error() == ErrorCode::NoFreeSlot);
}

static void test_a_slot_reads_its_bytes_and_nothing_past_them(void)
{
    MemoryImageStore store;
    const Bytes bytes = {1, 2, 3, 4, 5};
    const std::size_t slot = store.adopt(Bytes(bytes)).value();
    ImageByteSource& source = store.source(slot);
    TEST_ASSERT_EQUAL_UINT32(5, source.size());
    uint8_t out[3] = {};
    TEST_ASSERT_TRUE(source.readAt(2, Span<uint8_t>(out, 3)).ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&bytes[2], out, 3);
    auto past = source.readAt(3, Span<uint8_t>(out, 3));
    TEST_ASSERT_FALSE(past.ok());
    TEST_ASSERT_TRUE(past.error() == ErrorCode::OutOfBounds);
    auto far = source.readAt(UINT32_MAX, Span<uint8_t>(out, 3));  // no wrap-around
    TEST_ASSERT_FALSE(far.ok());
}

static void test_a_released_slot_can_be_adopted_again(void)
{
    MemoryImageStore store;
    std::size_t slots[MemoryImageStore::kSlots];
    for (auto& slot : slots) {
        slot = store.adopt(Bytes(4)).value();
    }
    TEST_ASSERT_TRUE(store.release(slots[3]));
    TEST_ASSERT_FALSE(store.inUse(slots[3]));
    TEST_ASSERT_FALSE(store.release(slots[3]));  // already free
    auto again = store.adopt(Bytes(7));
    TEST_ASSERT_TRUE(again.ok());
    TEST_ASSERT_EQUAL_size_t(slots[3], again.value());
    TEST_ASSERT_EQUAL_UINT32(7, store.source(again.value()).size());
}

static void test_an_open_slot_is_not_released(void)
{
    MemoryImageStore store;
    const std::size_t slot = store.adopt(atr()).value();
    auto opened = store.open(slot);
    TEST_ASSERT_TRUE(opened.ok());
    TEST_ASSERT_FALSE(store.release(slot));
    TEST_ASSERT_TRUE(store.inUse(slot));
    store.close(*opened.value());
    TEST_ASSERT_TRUE(store.release(slot));
}

static void test_an_attached_slot_is_not_released(void)
{
    MemoryImageStore store;
    const std::size_t slot = store.adopt(atr()).value();
    {
        auto disk = DiskImage::open(store.source(slot));
        TEST_ASSERT_TRUE(disk.ok());
        TEST_ASSERT_TRUE(store.source(slot).isAttached());
        TEST_ASSERT_FALSE(store.release(slot));
        TEST_ASSERT_TRUE(store.inUse(slot));
    }
    TEST_ASSERT_FALSE(store.source(slot).isAttached());
    TEST_ASSERT_TRUE(store.release(slot));
}

static void test_tnfs_locations_open_the_memory_slots(void)
{
    MenuImage menu;
    HostImageRepository repository(RB_TEST_MENU_DIR, menu);
    MemoryImageStore& store = repository.memory();
    for (std::size_t i = 0; i < MemoryImageStore::kSlots; ++i) {
        TEST_ASSERT_TRUE(store.adopt(Bytes(16 + i)).ok());
    }
    for (std::size_t i = 0; i < MemoryImageStore::kSlots; ++i) {
        char name[8];
        tnfs(i, name);
        auto opened = repository.open(location(name));
        TEST_ASSERT_TRUE_MESSAGE(opened.ok(), name);
        TEST_ASSERT_EQUAL_PTR(&store.source(i), opened.value());
        TEST_ASSERT_EQUAL_UINT32(16 + i, opened.value()->size());
        auto twice = repository.open(location(name));
        TEST_ASSERT_FALSE(twice.ok());
        TEST_ASSERT_TRUE(twice.error() == ErrorCode::NoFreeSlot);
        repository.close(opened.value());
        TEST_ASSERT_TRUE(store.release(i));
    }
    TEST_ASSERT_EQUAL_size_t(0, repository.openFiles());
}

static void test_an_empty_slot_or_a_bad_tnfs_name_is_not_found(void)
{
    MenuImage menu;
    HostImageRepository repository(RB_TEST_MENU_DIR, menu);
    const char* bad[] = {"*tnfs*0", "*tnfs*9", "*tnfs*", "*tnfs*01", "*tnfs*a", "*tnfs*-"};
    for (const char* name : bad) {
        auto opened = repository.open(location(name));
        TEST_ASSERT_FALSE_MESSAGE(opened.ok(), name);
        TEST_ASSERT_TRUE_MESSAGE(opened.error() == ErrorCode::NotFound, name);
    }
    TEST_ASSERT_EQUAL_size_t(0, repository.openFiles());
}

static void test_the_menu_and_the_files_open_as_before(void)
{
    MenuImage menu;
    HostImageRepository repository(RB_TEST_MENU_DIR, menu);
    auto disk = repository.open(location(rb::host::kMenuLocation));
    TEST_ASSERT_TRUE(disk.ok());
    TEST_ASSERT_EQUAL_PTR(&menu, disk.value());
    TEST_ASSERT_TRUE(repository.menuOpen());
    auto file = repository.open(location("demo-uno.atr"));
    TEST_ASSERT_TRUE(file.ok());
    TEST_ASSERT_EQUAL_size_t(1, repository.openFiles());
    TEST_ASSERT_EQUAL_UINT32(16 + 720 * 128, file.value()->size());
    repository.close(file.value());
    repository.close(disk.value());
    TEST_ASSERT_FALSE(repository.menuOpen());
    TEST_ASSERT_EQUAL_size_t(0, repository.openFiles());
}

static void test_the_loader_thread_can_adopt_and_release_while_slots_are_open(void)
{
    MenuImage menu;
    HostImageRepository repository(RB_TEST_MENU_DIR, menu);
    MemoryImageStore& store = repository.memory();
    const std::size_t kept = store.adopt(Bytes(32, 0x5A)).value();
    auto opened = store.open(kept);
    TEST_ASSERT_TRUE(opened.ok());
    std::thread loader([&store, kept] {
        for (int round = 0; round < 2000; ++round) {
            auto slot = store.adopt(Bytes(64));
            if (slot.ok() && slot.value() != kept) {
                (void)store.release(slot.value());
            }
        }
    });
    uint8_t out[32];
    for (int round = 0; round < 2000; ++round) {
        TEST_ASSERT_TRUE(opened.value()->readAt(0, Span<uint8_t>(out, 32)).ok());
        TEST_ASSERT_EQUAL_HEX8(0x5A, out[31]);
    }
    loader.join();
    TEST_ASSERT_FALSE(store.release(kept));
    store.close(*opened.value());
    TEST_ASSERT_TRUE(store.release(kept));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nine_slots_then_no_free_slot);
    RUN_TEST(test_a_slot_reads_its_bytes_and_nothing_past_them);
    RUN_TEST(test_a_released_slot_can_be_adopted_again);
    RUN_TEST(test_an_open_slot_is_not_released);
    RUN_TEST(test_an_attached_slot_is_not_released);
    RUN_TEST(test_tnfs_locations_open_the_memory_slots);
    RUN_TEST(test_an_empty_slot_or_a_bad_tnfs_name_is_not_found);
    RUN_TEST(test_the_menu_and_the_files_open_as_before);
    RUN_TEST(test_the_loader_thread_can_adopt_and_release_while_slots_are_open);
    return UNITY_END();
}
