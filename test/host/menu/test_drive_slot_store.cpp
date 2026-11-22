// SlotFileStore (menu-unidades-persistentes T2; C1, C5, C8, C9a, C9b, C9c): D1..D8 in two
// fixed-size slot files; a power cut that leaves one slot incomplete loses at most that write.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <string>
#include <system_error>

#include "rb/menu/drive_memory.hpp"
#include "rb/menu/drive_slot_store.hpp"
#include "unity.h"

using rb::host::DriveMemory;
using rb::host::SlotFileStore;
using Status = rb::host::SlotFileStore::LoadStatus;
namespace codec = rb::host::drive_memory_codec;

namespace {

const char* const kDir = RB_TEST_SLOT_DIR;

std::string path_of(const char* name) { return std::string(kDir) + "/" + name; }

DriveMemory memory(const char* d1, const char* d2 = "", const char* d3 = "")
{
    DriveMemory m;
    TEST_ASSERT_TRUE(m.set(1, d1));
    TEST_ASSERT_TRUE(m.set(2, d2));
    TEST_ASSERT_TRUE(m.set(3, d3));
    return m;
}

// What a power cut halfway through the write leaves: the slot file shorter than a slot.
void cut(const char* slotName, std::size_t keep = codec::kSlotSize / 2)
{
    std::error_code error;
    std::filesystem::resize_file(path_of(slotName), keep, error);
    TEST_ASSERT_FALSE(error);
}

SlotFileStore::LoadResult reload()
{
    SlotFileStore fresh{kDir};  // a new power-up: nothing remembered in RAM
    return fresh.load();
}

}  // namespace

void setUp(void)
{
    std::error_code error;
    std::filesystem::remove_all(kDir, error);
    std::filesystem::create_directories(kDir, error);
}

void tearDown(void) {}

static void test_an_empty_directory_loads_nothing(void)
{
    const SlotFileStore::LoadResult r = reload();
    TEST_ASSERT_EQUAL(Status::Empty, r.status);
}

static void test_a_saved_memory_survives_a_power_up(void)
{
    SlotFileStore store{kDir};
    TEST_ASSERT_EQUAL(Status::Empty, store.load().status);
    const DriveMemory e1 = memory("DEMO-DOS.ATR", "DATOS.ATR");
    TEST_ASSERT_TRUE(store.save(e1).ok);
    const SlotFileStore::LoadResult r = reload();
    TEST_ASSERT_EQUAL(Status::Loaded, r.status);
    TEST_ASSERT_TRUE(r.memory == e1);
}

static void test_saves_alternate_between_the_two_slots(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    TEST_ASSERT_TRUE(store.save(memory("A.ATR")).ok);
    TEST_ASSERT_TRUE(std::filesystem::exists(path_of(SlotFileStore::kSlotA)));
    TEST_ASSERT_FALSE(std::filesystem::exists(path_of(SlotFileStore::kSlotB)));
    TEST_ASSERT_TRUE(store.save(memory("B.ATR")).ok);
    TEST_ASSERT_TRUE(std::filesystem::exists(path_of(SlotFileStore::kSlotB)));
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, static_cast<uint64_t>(std::filesystem::file_size(path_of(SlotFileStore::kSlotA))));
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, static_cast<uint64_t>(std::filesystem::file_size(path_of(SlotFileStore::kSlotB))));
    TEST_ASSERT_TRUE(store.save(memory("C.ATR")).ok);  // back to A, over the oldest
    const SlotFileStore::LoadResult r = reload();
    TEST_ASSERT_TRUE(r.memory == memory("C.ATR"));
    TEST_ASSERT_EQUAL_UINT32(3, r.seq);
}

// C5: an unmount is remembered like a mount.
static void test_an_emptied_drive_is_remembered_empty(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    TEST_ASSERT_TRUE(store.save(memory("A.ATR", "B.ATR")).ok);
    TEST_ASSERT_TRUE(store.save(memory("A.ATR")).ok);
    TEST_ASSERT_EQUAL_STRING("", reload().memory.name(2));
}

// C9a: E0 and E1 confirmed; the cut lands while E2 is written over E0's slot. E1 comes back.
static void test_a_cut_during_the_second_save_brings_back_the_first(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    const DriveMemory e0 = memory("DEMO-DOS.ATR");
    const DriveMemory e1 = memory("DEMO-DOS.ATR", "D2.ATR");
    const DriveMemory e2 = memory("DEMO-DOS.ATR", "D2.ATR", "D3.ATR");
    TEST_ASSERT_TRUE(store.save(e0).ok);  // slot A
    TEST_ASSERT_TRUE(store.save(e1).ok);  // slot B: E1 confirmed
    TEST_ASSERT_TRUE(store.save(e2).ok);  // slot A...
    cut(SlotFileStore::kSlotA);           // ...left incomplete by the cut
    const SlotFileStore::LoadResult r = reload();
    TEST_ASSERT_EQUAL(Status::Loaded, r.status);
    TEST_ASSERT_TRUE(r.memory == e1);
}

// C9b-1: only E0 confirmed; the cut lands while E1 is written. E0 comes back.
static void test_a_cut_during_an_unconfirmed_save_brings_back_the_last_confirmed(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    const DriveMemory e0 = memory("DEMO-DOS.ATR");
    TEST_ASSERT_TRUE(store.save(e0).ok);                             // slot A
    TEST_ASSERT_TRUE(store.save(memory("DEMO-DOS.ATR", "D2.ATR")).ok);  // slot B...
    cut(SlotFileStore::kSlotB, 0);                                    // ...cut before any byte landed
    TEST_ASSERT_TRUE(reload().memory == e0);
    cut(SlotFileStore::kSlotB, codec::kSlotSize - 1);  // or one byte short
    TEST_ASSERT_TRUE(reload().memory == e0);
}

// C9b-2: both changes merged into one save (E2) and cut during it. E0 comes back.
static void test_a_cut_during_a_merged_save_brings_back_the_last_confirmed(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    const DriveMemory e0 = memory("DEMO-DOS.ATR");
    TEST_ASSERT_TRUE(store.save(e0).ok);
    TEST_ASSERT_TRUE(store.save(memory("DEMO-DOS.ATR", "D2.ATR", "D3.ATR")).ok);
    cut(SlotFileStore::kSlotB);
    TEST_ASSERT_TRUE(reload().memory == e0);
}

// C9c: the new slot was written whole; the menu never got to show GUARDADO. The new state wins.
static void test_a_complete_slot_wins_even_if_the_save_was_never_confirmed(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    TEST_ASSERT_TRUE(store.save(memory("DEMO-DOS.ATR")).ok);
    const DriveMemory e1 = memory("DEMO-DOS.ATR", "D2.ATR");
    TEST_ASSERT_TRUE(store.save(e1).ok);  // the result is dropped: nobody tells the controller
    TEST_ASSERT_TRUE(reload().memory == e1);
}

std::vector<unsigned char> bytes_of(const char* slotName)
{
    std::vector<unsigned char> out;
    if (std::FILE* file = std::fopen(path_of(slotName).c_str(), "rb")) {
        for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
            out.push_back(static_cast<unsigned char>(c));
        }
        std::fclose(file);
    }
    return out;
}

// C8: the write to the target slot fails while E0 stays readable. save() reports the error,
// E0's slot is byte for byte untouched, and a new power-up gets E0 back.
static void test_a_failed_save_reports_an_error_and_keeps_the_previous_slot(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    const DriveMemory e0 = memory("DEMO-DOS.ATR");
    TEST_ASSERT_TRUE(store.save(e0).ok);  // slot A; the next save goes to B
    const std::vector<unsigned char> before = bytes_of(SlotFileStore::kSlotA);
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, static_cast<uint64_t>(before.size()));
    std::error_code error;
    TEST_ASSERT_TRUE(std::filesystem::create_directory(path_of(SlotFileStore::kSlotB), error));  // B cannot be written
    const SlotFileStore::SaveResult failed = store.save(memory("DEMO-DOS.ATR", "OTRO.ATR"));
    TEST_ASSERT_FALSE(failed.ok);
    TEST_ASSERT_NOT_EQUAL(0, failed.error);
    TEST_ASSERT_TRUE(bytes_of(SlotFileStore::kSlotA) == before);  // E0 untouched
    std::filesystem::remove(path_of(SlotFileStore::kSlotB), error);  // the fault goes away
    const SlotFileStore::LoadResult r = reload();
    TEST_ASSERT_EQUAL(Status::Loaded, r.status);
    TEST_ASSERT_TRUE(r.memory == e0);
}

// The card is removed from under the store: save() reports it and nothing half-written appears.
static void test_a_save_with_the_card_gone_reports_an_error(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    const DriveMemory e0 = memory("DEMO-DOS.ATR");
    TEST_ASSERT_TRUE(store.save(e0).ok);
    std::error_code error;
    std::filesystem::remove_all(kDir, error);  // the card is gone from under the store
    const SlotFileStore::SaveResult failed = store.save(memory("OTRO.ATR"));
    TEST_ASSERT_FALSE(failed.ok);
    TEST_ASSERT_NOT_EQUAL(0, failed.error);
    std::filesystem::create_directories(kDir, error);
    TEST_ASSERT_EQUAL(Status::Empty, reload().status);  // nothing half-written appeared
}

static void test_a_slot_file_with_garbage_is_ignored_and_repaired(void)
{
    {
        std::FILE* file = std::fopen(path_of(SlotFileStore::kSlotA).c_str(), "wb");
        TEST_ASSERT_NOT_NULL(file);
        for (std::size_t i = 0; i < codec::kSlotSize + 10; ++i) {
            std::fputc(0x5A, file);
        }
        std::fclose(file);
    }
    SlotFileStore store{kDir};
    TEST_ASSERT_EQUAL(Status::Empty, store.load().status);
    const DriveMemory e1 = memory("A.ATR");
    TEST_ASSERT_TRUE(store.save(e1).ok);  // goes to A and leaves it exactly one slot long
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, static_cast<uint64_t>(std::filesystem::file_size(path_of(SlotFileStore::kSlotA))));
    TEST_ASSERT_TRUE(reload().memory == e1);
}

static void test_a_missing_directory_loads_nothing(void)
{
    std::error_code error;
    std::filesystem::remove_all(kDir, error);
    TEST_ASSERT_EQUAL(Status::Empty, reload().status);
}

// A slot file must be exactly one slot long: a valid slot with one byte more is not trusted.
static void test_a_valid_slot_with_an_extra_byte_is_ignored(void)
{
    SlotFileStore store{kDir};
    (void)store.load();
    TEST_ASSERT_TRUE(store.save(memory("A.ATR")).ok);  // slot A
    std::FILE* file = std::fopen(path_of(SlotFileStore::kSlotA).c_str(), "ab");
    TEST_ASSERT_NOT_NULL(file);
    std::fputc(0, file);
    std::fclose(file);
    TEST_ASSERT_EQUAL(Status::Empty, reload().status);
}

// A slot that cannot be read may hold the newest memory: save refuses rather than write a lower seq.
static void test_a_save_is_refused_while_a_slot_cannot_be_read(void)
{
    // No slot A; slot B cannot be read (a directory stands in for a card read error).
    std::error_code error;
    TEST_ASSERT_TRUE(std::filesystem::create_directory(path_of(SlotFileStore::kSlotB), error));
    SlotFileStore store{kDir};
    TEST_ASSERT_EQUAL(Status::Error, store.load().status);
    const SlotFileStore::SaveResult refused = store.save(memory("NUEVO.ATR"));
    TEST_ASSERT_FALSE(refused.ok);
    TEST_ASSERT_NOT_EQUAL(0, refused.error);
    TEST_ASSERT_FALSE(std::filesystem::exists(path_of(SlotFileStore::kSlotA)));  // no seq 1 over B's memory
}

static void test_a_save_after_the_last_sequence_is_refused(void)
{
    codec::Block block{};
    codec::encode(memory("A.ATR"), UINT32_MAX, block);
    std::FILE* file = std::fopen(path_of(SlotFileStore::kSlotA).c_str(), "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT64(block.size(), static_cast<uint64_t>(std::fwrite(block.data(), 1, block.size(), file)));
    std::fclose(file);
    SlotFileStore store{kDir};
    TEST_ASSERT_FALSE(store.save(memory("B.ATR")).ok);
    TEST_ASSERT_TRUE(reload().memory == memory("A.ATR"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_save_is_refused_while_a_slot_cannot_be_read);
    RUN_TEST(test_a_save_after_the_last_sequence_is_refused);
    RUN_TEST(test_a_valid_slot_with_an_extra_byte_is_ignored);
    RUN_TEST(test_an_empty_directory_loads_nothing);
    RUN_TEST(test_a_saved_memory_survives_a_power_up);
    RUN_TEST(test_saves_alternate_between_the_two_slots);
    RUN_TEST(test_an_emptied_drive_is_remembered_empty);
    RUN_TEST(test_a_cut_during_the_second_save_brings_back_the_first);
    RUN_TEST(test_a_cut_during_an_unconfirmed_save_brings_back_the_last_confirmed);
    RUN_TEST(test_a_cut_during_a_merged_save_brings_back_the_last_confirmed);
    RUN_TEST(test_a_complete_slot_wins_even_if_the_save_was_never_confirmed);
    RUN_TEST(test_a_failed_save_reports_an_error_and_keeps_the_previous_slot);
    RUN_TEST(test_a_save_with_the_card_gone_reports_an_error);
    RUN_TEST(test_a_slot_file_with_garbage_is_ignored_and_repaired);
    RUN_TEST(test_a_missing_directory_loads_nothing);
    return UNITY_END();
}
