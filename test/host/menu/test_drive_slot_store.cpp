// SlotFileStore (menu-unidades-persistentes T2; C1, C5, C8, C9a, C9b, C9c): D1..D8 in two
// fixed-size slot files; a power cut that leaves one slot incomplete loses at most that write.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <string>
#include <system_error>

#include "rb/menu/drive_memory.hpp"
#include "rb/menu/drive_slot_store.hpp"
#include "rb/menu/sio_write_log.hpp"
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

// --- menu-tnfs-persistente T1 (AC7, plan §5 and R5): version 2 cards and going back ----------

// A slot file as the firmware of menu-cinta writes it: version 2, 9 name fields.
void writeVersionTwo(const char* slotName, const DriveMemory& memory, uint32_t seq)
{
    std::vector<uint8_t> old(codec::kSlotSizeV2, 0);
    const uint32_t magic = codec::kMagic;
    for (int i = 0; i < 4; ++i) {
        old[i] = static_cast<uint8_t>(magic >> (8 * i));
        old[8 + i] = static_cast<uint8_t>(seq >> (8 * i));
    }
    old[4] = 2;
    for (unsigned drive = 1; drive <= DriveMemory::kDrives + 1; ++drive) {
        const char* name = drive <= DriveMemory::kDrives ? memory.name(drive) : memory.tape();
        std::copy(name, name + std::char_traits<char>::length(name),
                  old.begin() + static_cast<long>(codec::kNamesOffset + (drive - 1) * codec::kNameField));
    }
    const uint32_t crc = rb::host::crc32(old.data(), codec::kSlotSizeV2 - 4);
    for (int i = 0; i < 4; ++i) {
        old[codec::kSlotSizeV2 - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    std::FILE* file = std::fopen(path_of(slotName).c_str(), "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(old.size(), std::fwrite(old.data(), 1, old.size(), file));
    std::fclose(file);
}

// What the firmware of menu-cinta reads: only version 1 and 2 sizes; a version 3 file is not a slot.
bool readAsVersionTwo(const char* slotName, rb::host::DriveMemorySlot& out)
{
    std::vector<uint8_t> data(codec::kSlotSize + 1, 0);
    std::FILE* file = std::fopen(path_of(slotName).c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t size = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    return (size == codec::kSlotSizeV2 || size == codec::kSlotSizeV1) && codec::decode(data.data(), size, out);
}

DriveMemory versionTwoMemory()
{
    DriveMemory m = memory("DOS.ATR", "DATOS.ATR", "JUEGO.XEX");
    TEST_ASSERT_TRUE(m.set(8, "UTIL.ATR"));
    TEST_ASSERT_TRUE(m.setTape("KARATE.CAS"));
    return m;
}

DriveMemory withNetworkDrive()
{
    DriveMemory m = versionTwoMemory();
    TEST_ASSERT_TRUE(m.setNet(2, DriveMemory::Origin::NetAtr, "tnfs.example.org", "/juegos/ARCADE.ATR", "ARCADE.ATR"));
    return m;
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

// menu-cinta T3 (AC5): a card written by the firmware before menu-cinta (version 1 slots) keeps
// its drives; the next saves write version 2, with the tape, at the new size.
static void test_a_version_1_card_is_read_and_the_next_saves_write_version_2(void)  // now version 3
{
    std::vector<uint8_t> old(codec::kSlotSizeV1, 0);
    const uint32_t magic = codec::kMagic;
    const uint32_t seq = 5;
    for (int i = 0; i < 4; ++i) {
        old[i] = static_cast<uint8_t>(magic >> (8 * i));
        old[8 + i] = static_cast<uint8_t>(seq >> (8 * i));
    }
    old[4] = 1;
    const char name[] = "VIEJO.ATR";
    std::copy(name, name + sizeof name - 1, old.begin() + codec::kNamesOffset);
    const uint32_t crc = rb::host::crc32(old.data(), codec::kSlotSizeV1 - 4);
    for (int i = 0; i < 4; ++i) {
        old[codec::kSlotSizeV1 - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    std::FILE* file = std::fopen(path_of(SlotFileStore::kSlotA).c_str(), "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(old.size(), std::fwrite(old.data(), 1, old.size(), file));
    std::fclose(file);

    SlotFileStore store{kDir};
    const SlotFileStore::LoadResult& loaded = store.load();
    TEST_ASSERT_EQUAL(Status::Loaded, loaded.status);
    TEST_ASSERT_TRUE(loaded.memory == memory("VIEJO.ATR"));
    TEST_ASSERT_EQUAL_STRING("", loaded.memory.tape());

    DriveMemory withTape = memory("VIEJO.ATR");
    TEST_ASSERT_TRUE(withTape.setTape("KARATE.CAS"));
    TEST_ASSERT_TRUE(store.save(withTape).ok);  // slot B, seq 6
    TEST_ASSERT_TRUE(store.save(withTape).ok);  // slot A again: the old, shorter file is rewritten
    const SlotFileStore::LoadResult again = reload();
    TEST_ASSERT_EQUAL(Status::Loaded, again.status);
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", again.memory.tape());
    TEST_ASSERT_EQUAL_UINT32(7, again.seq);
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, std::filesystem::file_size(path_of(SlotFileStore::kSlotA)));
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, std::filesystem::file_size(path_of(SlotFileStore::kSlotB)));
}

static void test_a_version_2_card_moves_to_version_3_and_keeps_its_slot(void)
{
    writeVersionTwo(SlotFileStore::kSlotA, versionTwoMemory(), 4);
    SlotFileStore store{kDir};
    const SlotFileStore::LoadResult& loaded = store.load();
    TEST_ASSERT_EQUAL(Status::Loaded, loaded.status);
    TEST_ASSERT_TRUE(loaded.memory == versionTwoMemory());  // D1..D8 and C: as written
    TEST_ASSERT_EQUAL_UINT32(4, loaded.seq);

    TEST_ASSERT_TRUE(store.save(withNetworkDrive()).ok);  // slot B, version 3, seq 5
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSizeV2, std::filesystem::file_size(path_of(SlotFileStore::kSlotA)));
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, std::filesystem::file_size(path_of(SlotFileStore::kSlotB)));
    const SlotFileStore::LoadResult newest = reload();  // slots of two sizes: the higher sequence wins
    TEST_ASSERT_EQUAL(Status::Loaded, newest.status);
    TEST_ASSERT_EQUAL_UINT32(5, newest.seq);
    TEST_ASSERT_TRUE(newest.memory == withNetworkDrive());
    rb::host::DriveMemorySlot stillThere{};
    TEST_ASSERT_TRUE(readAsVersionTwo(SlotFileStore::kSlotA, stillThere));  // the version 2 slot is still valid
}

static void test_a_damaged_version_3_slot_falls_back_to_the_version_2_one(void)
{
    writeVersionTwo(SlotFileStore::kSlotA, versionTwoMemory(), 4);
    {
        SlotFileStore store{kDir};
        (void)store.load();
        TEST_ASSERT_TRUE(store.save(withNetworkDrive()).ok);  // slot B, seq 5
    }
    // A bad CRC: one byte of a network field changed.
    std::vector<uint8_t> b(codec::kSlotSize);
    std::FILE* file = std::fopen(path_of(SlotFileStore::kSlotB).c_str(), "r+b");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(b.size(), std::fread(b.data(), 1, b.size(), file));
    b[codec::kNetOffset + codec::kNetField + 3] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(0, std::fseek(file, 0, SEEK_SET));
    TEST_ASSERT_EQUAL_size_t(b.size(), std::fwrite(b.data(), 1, b.size(), file));
    std::fclose(file);
    SlotFileStore::LoadResult back = reload();
    TEST_ASSERT_EQUAL(Status::Loaded, back.status);
    TEST_ASSERT_EQUAL_UINT32(4, back.seq);
    TEST_ASSERT_TRUE(back.memory == versionTwoMemory());

    cut(SlotFileStore::kSlotB, codec::kSlotSizeV2);  // a cut write the size of a version 2 slot
    back = reload();
    TEST_ASSERT_EQUAL_UINT32(4, back.seq);
    TEST_ASSERT_TRUE(back.memory == versionTwoMemory());
}

// R5: going back to the firmware of menu-cinta after the first version 3 save.
static void test_going_back_to_a_version_2_firmware_and_forward_again(void)
{
    writeVersionTwo(SlotFileStore::kSlotA, versionTwoMemory(), 4);
    {
        SlotFileStore store{kDir};
        (void)store.load();
        TEST_ASSERT_TRUE(store.save(withNetworkDrive()).ok);  // slot B, version 3, seq 5
    }
    // The old firmware: slot B is no slot for it, so slot A (seq 4) is its newest; it restores the
    // older assignments, without the network drive.
    rb::host::DriveMemorySlot a{};
    rb::host::DriveMemorySlot b{};
    TEST_ASSERT_TRUE(readAsVersionTwo(SlotFileStore::kSlotA, a));
    TEST_ASSERT_FALSE(readAsVersionTwo(SlotFileStore::kSlotB, b));
    TEST_ASSERT_TRUE(a.memory == versionTwoMemory());
    TEST_ASSERT_EQUAL_STRING("DATOS.ATR", a.memory.name(2));
    // It saves a change the way it always did: over the slot that is not its newest (B), seq 5.
    DriveMemory oldSaved = versionTwoMemory();
    TEST_ASSERT_TRUE(oldSaved.set(3, "OTRO.ATR"));
    writeVersionTwo(SlotFileStore::kSlotB, oldSaved, 5);
    TEST_ASSERT_TRUE(readAsVersionTwo(SlotFileStore::kSlotA, a));  // A is still valid for it

    // Forward again: the valid slot with the higher sequence wins, and it is what was saved last.
    SlotFileStore store{kDir};
    const SlotFileStore::LoadResult& loaded = store.load();
    TEST_ASSERT_EQUAL(Status::Loaded, loaded.status);
    TEST_ASSERT_EQUAL_UINT32(5, loaded.seq);
    TEST_ASSERT_TRUE(loaded.memory == oldSaved);  // D1..D8 and C: as last saved
    TEST_ASSERT_TRUE(store.save(withNetworkDrive()).ok);  // and A/B keep alternating: A, seq 6
    const SlotFileStore::LoadResult after = reload();
    TEST_ASSERT_EQUAL_UINT32(6, after.seq);
    TEST_ASSERT_EQUAL_UINT64(codec::kSlotSize, std::filesystem::file_size(path_of(SlotFileStore::kSlotA)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_version_1_card_is_read_and_the_next_saves_write_version_2);
    RUN_TEST(test_a_version_2_card_moves_to_version_3_and_keeps_its_slot);
    RUN_TEST(test_a_damaged_version_3_slot_falls_back_to_the_version_2_one);
    RUN_TEST(test_going_back_to_a_version_2_firmware_and_forward_again);
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
