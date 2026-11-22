// DriveMemory and its slot codec (menu-unidades-persistentes T1, C1 and C9): the names of D1..D8
// survive a round trip through one fixed-size slot, and any damaged slot is rejected.

#include <cstdint>
#include <cstring>

#include "rb/menu/drive_memory.hpp"
#include "rb/menu/sio_write_log.hpp"
#include "unity.h"

using rb::host::DriveMemory;
using rb::host::DriveMemorySlot;
namespace codec = rb::host::drive_memory_codec;

void setUp(void) {}
void tearDown(void) {}

namespace {

DriveMemory sample()
{
    DriveMemory memory;
    TEST_ASSERT_TRUE(memory.set(1, "DEMO-DOS.ATR"));
    TEST_ASSERT_TRUE(memory.set(2, "DATOS.ATR"));
    TEST_ASSERT_TRUE(memory.set(8, "juego con espacios.xex"));
    return memory;
}

// Rewrites the trailing CRC so that only the field under test is wrong.
void reseal(codec::Block& block)
{
    const uint32_t crc = rb::host::crc32(block.data(), codec::kSlotSize - 4);
    for (int i = 0; i < 4; ++i) {
        block[codec::kSlotSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
}

}  // namespace

static void test_a_new_memory_has_every_drive_empty(void)
{
    const DriveMemory memory;
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        TEST_ASSERT_EQUAL_STRING("", memory.name(drive));
    }
}

static void test_set_rejects_drives_out_of_range_and_names_too_long(void)
{
    DriveMemory memory;
    TEST_ASSERT_FALSE(memory.set(0, "A.ATR"));
    TEST_ASSERT_FALSE(memory.set(9, "A.ATR"));
    char longName[DriveMemory::kMaxName + 2];
    std::memset(longName, 'A', sizeof longName - 1);
    longName[sizeof longName - 1] = '\0';
    TEST_ASSERT_FALSE(memory.set(3, longName));
    longName[DriveMemory::kMaxName] = '\0';  // exactly kMaxName characters
    TEST_ASSERT_TRUE(memory.set(3, longName));
    TEST_ASSERT_FALSE(memory.set(4, "tab\there.atr"));
    TEST_ASSERT_TRUE(memory.set(3, "OK.ATR"));
    longName[DriveMemory::kMaxName] = 'A';  // kMaxName + 1 characters again
    TEST_ASSERT_FALSE(memory.set(3, longName));
    TEST_ASSERT_EQUAL_STRING("OK.ATR", memory.name(3));  // a rejected set changes nothing
    TEST_ASSERT_TRUE(memory.set(3, ""));  // empties the drive
    TEST_ASSERT_TRUE(memory == DriveMemory{});
    TEST_ASSERT_EQUAL_STRING("", memory.name(0));
    TEST_ASSERT_EQUAL_STRING("", memory.name(9));
}

static void test_a_slot_round_trips_the_names_and_the_sequence(void)
{
    codec::Block block{};
    codec::encode(sample(), 41, block);
    DriveMemorySlot slot{};
    TEST_ASSERT_TRUE(codec::decode(block.data(), block.size(), slot));
    TEST_ASSERT_TRUE(slot.memory == sample());
    TEST_ASSERT_EQUAL_UINT32(41, slot.seq);
}

static void test_a_slot_with_a_changed_byte_is_rejected(void)
{
    codec::Block block{};
    codec::encode(sample(), 7, block);
    for (std::size_t at : {std::size_t{0}, std::size_t{9}, std::size_t{200}, codec::kSlotSize - 1}) {
        codec::Block damaged = block;
        damaged[at] ^= 0x01;
        DriveMemorySlot slot{};
        TEST_ASSERT_FALSE_MESSAGE(codec::decode(damaged.data(), damaged.size(), slot), "one byte changed");
    }
}

static void test_a_truncated_slot_is_rejected(void)
{
    codec::Block block{};
    codec::encode(sample(), 7, block);
    DriveMemorySlot slot{};
    TEST_ASSERT_FALSE(codec::decode(block.data(), block.size() - 1, slot));
    TEST_ASSERT_FALSE(codec::decode(block.data(), 0, slot));
    TEST_ASSERT_FALSE(codec::decode(nullptr, codec::kSlotSize, slot));
    uint8_t longer[codec::kSlotSize + 1] = {};
    std::memcpy(longer, block.data(), codec::kSlotSize);
    TEST_ASSERT_FALSE(codec::decode(longer, sizeof longer, slot));
}

static void test_a_sealed_slot_with_a_wrong_header_or_name_field_is_rejected(void)
{
    codec::Block good{};
    codec::encode(sample(), 7, good);
    DriveMemorySlot slot{};

    codec::Block magic = good;
    magic[0] ^= 0xFF;
    reseal(magic);
    TEST_ASSERT_FALSE_MESSAGE(codec::decode(magic.data(), magic.size(), slot), "magic");

    codec::Block version = good;
    version[4] = 2;
    reseal(version);
    TEST_ASSERT_FALSE_MESSAGE(codec::decode(version.data(), version.size(), slot), "version");

    codec::Block versionHigh = good;
    versionHigh[5] = 1;  // version 0x0101
    reseal(versionHigh);
    TEST_ASSERT_FALSE_MESSAGE(codec::decode(versionHigh.data(), versionHigh.size(), slot), "version high byte");

    for (std::size_t at : {std::size_t{6}, std::size_t{7}}) {
        codec::Block reserved = good;
        reserved[at] = 1;
        reseal(reserved);
        TEST_ASSERT_FALSE_MESSAGE(codec::decode(reserved.data(), reserved.size(), slot), "reserved");
    }

    codec::Block noNul = good;
    std::memset(noNul.data() + codec::kNamesOffset, 'A', codec::kNameField);  // D1 field without its NUL
    reseal(noNul);
    TEST_ASSERT_FALSE_MESSAGE(codec::decode(noNul.data(), noNul.size(), slot), "name without NUL");

    codec::Block tail = good;
    tail[codec::kNamesOffset + codec::kNameField - 1] = 'Z';  // garbage after D1's NUL
    reseal(tail);
    TEST_ASSERT_FALSE_MESSAGE(codec::decode(tail.data(), tail.size(), slot), "bytes after the NUL");

    codec::Block control = good;
    control[codec::kNamesOffset] = 0x07;  // not printable ASCII
    reseal(control);
    TEST_ASSERT_FALSE_MESSAGE(codec::decode(control.data(), control.size(), slot), "control character");

    reseal(good);  // the positive control: resealing an intact slot keeps it valid
    TEST_ASSERT_TRUE(codec::decode(good.data(), good.size(), slot));
}

static void test_newest_picks_the_valid_slot_with_the_highest_sequence(void)
{
    const DriveMemorySlot a{sample(), 5};
    const DriveMemorySlot b{DriveMemory{}, 6};
    TEST_ASSERT_EQUAL_PTR(&b, codec::newest(&a, &b));
    TEST_ASSERT_EQUAL_PTR(&b, codec::newest(&b, &a));
    TEST_ASSERT_EQUAL_PTR(&a, codec::newest(&a, nullptr));
    TEST_ASSERT_EQUAL_PTR(&b, codec::newest(nullptr, &b));
    TEST_ASSERT_NULL(codec::newest(nullptr, nullptr));
    const DriveMemorySlot tie{DriveMemory{}, 5};
    TEST_ASSERT_EQUAL_PTR(&a, codec::newest(&a, &tie));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_new_memory_has_every_drive_empty);
    RUN_TEST(test_set_rejects_drives_out_of_range_and_names_too_long);
    RUN_TEST(test_a_slot_round_trips_the_names_and_the_sequence);
    RUN_TEST(test_a_slot_with_a_changed_byte_is_rejected);
    RUN_TEST(test_a_truncated_slot_is_rejected);
    RUN_TEST(test_a_sealed_slot_with_a_wrong_header_or_name_field_is_rejected);
    RUN_TEST(test_newest_picks_the_valid_slot_with_the_highest_sequence);
    return UNITY_END();
}
