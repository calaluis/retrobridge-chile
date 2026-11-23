// DriveMemory and its slot codec (menu-unidades-persistentes T1, C1 and C9): the names of D1..D8
// survive a round trip through one fixed-size slot, and any damaged slot is rejected.

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

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
    version[4] = 4;  // menu-tnfs-persistente: 3 is the current version; 2 and 1 have their own sizes
    reseal(version);
    TEST_ASSERT_FALSE_MESSAGE(codec::decode(version.data(), version.size(), slot), "version");
    version[4] = 2;  // version 2 at the version 3 size
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

static void test_a_slot_rejected_at_the_last_drive_leaves_the_output_unchanged(void)
{
    codec::Block bad{};
    codec::encode(sample(), 9, bad);
    bad[codec::kNamesOffset + (DriveMemory::kDrives - 1) * codec::kNameField] = 0x07;  // D8 starts with BEL
    reseal(bad);
    DriveMemorySlot slot{DriveMemory{}, 3};
    TEST_ASSERT_TRUE(slot.memory.set(1, "KEEP.ATR"));
    TEST_ASSERT_FALSE(codec::decode(bad.data(), bad.size(), slot));
    TEST_ASSERT_EQUAL_STRING("KEEP.ATR", slot.memory.name(1));  // D1..D7 of `bad` were valid: none copied
    TEST_ASSERT_EQUAL_STRING("", slot.memory.name(2));
    TEST_ASSERT_EQUAL_UINT32(3, slot.seq);
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

// --- menu-cinta T3 (AC5): the tape on C: is remembered too -----------------------

// A version 1 slot as the firmware before menu-cinta writes it: 8 name fields, no tape.
std::array<uint8_t, codec::kSlotSizeV1> versionOne(const DriveMemory& memory, uint32_t seq)
{
    std::array<uint8_t, codec::kSlotSizeV1> out{};
    const uint32_t magic = codec::kMagic;
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<uint8_t>(magic >> (8 * i));
        out[8 + i] = static_cast<uint8_t>(seq >> (8 * i));
    }
    out[4] = 1;
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        const char* name = memory.name(drive);
        std::memcpy(out.data() + codec::kNamesOffset + (drive - 1) * codec::kNameField, name, std::strlen(name));
    }
    const uint32_t crc = rb::host::crc32(out.data(), codec::kSlotSizeV1 - 4);
    for (int i = 0; i < 4; ++i) {
        out[codec::kSlotSizeV1 - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    return out;
}

static void test_the_tape_is_empty_by_default_and_takes_only_valid_names(void)
{
    DriveMemory memory;
    TEST_ASSERT_EQUAL_STRING("", memory.tape());
    TEST_ASSERT_TRUE(memory.setTape("KARATE.CAS"));
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", memory.tape());
    TEST_ASSERT_FALSE(memory.setTape("CON\x07" "CONTROL.CAS"));
    char tooLong[DriveMemory::kMaxName + 2];
    std::memset(tooLong, 'A', sizeof tooLong - 1);
    tooLong[sizeof tooLong - 1] = '\0';
    TEST_ASSERT_FALSE(memory.setTape(tooLong));
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", memory.tape());  // a refused name changes nothing
    DriveMemory other = memory;
    TEST_ASSERT_TRUE(other == memory);
    TEST_ASSERT_TRUE(other.setTape(""));
    TEST_ASSERT_TRUE(other != memory);  // the tape counts for equality
}

static void test_a_version_3_slot_round_trips_the_tape(void)
{
    DriveMemory memory = sample();
    TEST_ASSERT_TRUE(memory.setTape("KARATE.CAS"));
    codec::Block block{};
    codec::encode(memory, 12, block);
    TEST_ASSERT_EQUAL_UINT8(3, block[4]);
    TEST_ASSERT_EQUAL_size_t(codec::kSlotSizeV1 + codec::kNameField, codec::kSlotSizeV2);
    TEST_ASSERT_EQUAL_size_t(3488, codec::kSlotSize);  // plan menu-tnfs-persistente §4
    DriveMemorySlot slot{};
    TEST_ASSERT_TRUE(codec::decode(block.data(), block.size(), slot));
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", slot.memory.tape());
    TEST_ASSERT_TRUE(slot.memory == memory);

    codec::Block tail = block;  // garbage after the tape's NUL
    tail[codec::kNamesOffset + DriveMemory::kDrives * codec::kNameField + codec::kNameField - 1] = 'Z';
    reseal(tail);
    TEST_ASSERT_FALSE(codec::decode(tail.data(), tail.size(), slot));
}

static void test_a_version_1_slot_is_read_with_no_tape(void)
{
    const auto old = versionOne(sample(), 30);
    DriveMemorySlot slot{};
    TEST_ASSERT_TRUE(slot.memory.setTape("STALE.CAS"));
    TEST_ASSERT_TRUE(codec::decode(old.data(), old.size(), slot));
    TEST_ASSERT_TRUE(slot.memory == sample());  // D1..D8 as written, and no tape
    TEST_ASSERT_EQUAL_STRING("", slot.memory.tape());
    TEST_ASSERT_EQUAL_UINT32(30, slot.seq);

    auto wrongVersion = old;  // version 2 at the version 1 size
    wrongVersion[4] = 2;
    const uint32_t crc = rb::host::crc32(wrongVersion.data(), codec::kSlotSizeV1 - 4);
    for (int i = 0; i < 4; ++i) {
        wrongVersion[codec::kSlotSizeV1 - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    TEST_ASSERT_FALSE(codec::decode(wrongVersion.data(), wrongVersion.size(), slot));
    auto damaged = old;
    damaged[20] ^= 0x01;
    TEST_ASSERT_FALSE(codec::decode(damaged.data(), damaged.size(), slot));
    TEST_ASSERT_FALSE(codec::decode(old.data(), old.size() - 1, slot));
}

// --- menu-tnfs-persistente T1 (AC7, AC9): network drives, version 3 ------------------

// A version 2 slot as the firmware of menu-cinta writes it: 9 name fields, no network fields.
std::array<uint8_t, codec::kSlotSizeV2> versionTwo(const DriveMemory& memory, uint32_t seq)
{
    std::array<uint8_t, codec::kSlotSizeV2> out{};
    const uint32_t magic = codec::kMagic;
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<uint8_t>(magic >> (8 * i));
        out[8 + i] = static_cast<uint8_t>(seq >> (8 * i));
    }
    out[4] = 2;
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        const char* name = memory.name(drive);
        std::memcpy(out.data() + codec::kNamesOffset + (drive - 1) * codec::kNameField, name, std::strlen(name));
    }
    const char* tape = memory.tape();
    std::memcpy(out.data() + codec::kNamesOffset + DriveMemory::kDrives * codec::kNameField, tape, std::strlen(tape));
    const uint32_t crc = rb::host::crc32(out.data(), codec::kSlotSizeV2 - 4);
    for (int i = 0; i < 4; ++i) {
        out[codec::kSlotSizeV2 - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    return out;
}

DriveMemory withNetwork()
{
    DriveMemory memory = sample();
    TEST_ASSERT_TRUE(memory.setNet(2, DriveMemory::Origin::NetAtr, "tnfs.example.org", "/juegos/ARCADE.ATR",
                                   "ARCADE.ATR"));
    TEST_ASSERT_TRUE(memory.setNet(3, DriveMemory::Origin::NetXex, "10.0.0.7", "/xex/Ca\xC3\xB1on.xex", "CANON.XEX"));
    TEST_ASSERT_TRUE(memory.setTape("KARATE.CAS"));
    return memory;
}

static void test_a_version_2_slot_is_read_as_local_drives(void)
{
    DriveMemory memory = sample();
    TEST_ASSERT_TRUE(memory.setTape("KARATE.CAS"));
    const auto old = versionTwo(memory, 41);
    DriveMemorySlot slot{};
    TEST_ASSERT_TRUE(codec::decode(old.data(), old.size(), slot));
    TEST_ASSERT_TRUE(slot.memory == memory);  // D1..D8 and C: as written
    TEST_ASSERT_EQUAL_UINT32(41, slot.seq);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriveMemory::Origin::Local),
                            static_cast<uint8_t>(slot.memory.origin(1)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriveMemory::Origin::Empty),
                            static_cast<uint8_t>(slot.memory.origin(3)));
    TEST_ASSERT_EQUAL_STRING("", slot.memory.server(1));

    auto damaged = old;
    damaged[30] ^= 0x01;
    TEST_ASSERT_FALSE(codec::decode(damaged.data(), damaged.size(), slot));
    TEST_ASSERT_FALSE(codec::decode(old.data(), old.size() - 1, slot));
}

static void test_a_network_drive_is_set_checked_and_replaced(void)
{
    DriveMemory memory;
    using O = DriveMemory::Origin;
    TEST_ASSERT_TRUE(memory.setNet(4, O::NetAtr, "srv-a", "/a/B.ATR", "B.ATR"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(O::NetAtr), static_cast<uint8_t>(memory.origin(4)));
    TEST_ASSERT_EQUAL_STRING("srv-a", memory.server(4));
    TEST_ASSERT_EQUAL_STRING("/a/B.ATR", memory.path(4));
    TEST_ASSERT_EQUAL_STRING("B.ATR", memory.name(4));

    const DriveMemory before = memory;  // every refusal below leaves it unchanged
    TEST_ASSERT_FALSE(memory.setNet(4, O::Local, "srv-b", "/C.ATR", "C.ATR"));
    TEST_ASSERT_FALSE(memory.setNet(4, O::Empty, "srv-b", "/C.ATR", "C.ATR"));
    TEST_ASSERT_FALSE(memory.setNet(0, O::NetAtr, "srv-b", "/C.ATR", "C.ATR"));
    TEST_ASSERT_FALSE(memory.setNet(9, O::NetAtr, "srv-b", "/C.ATR", "C.ATR"));
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, "", "/C.ATR", "C.ATR"));
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, "srv-b", "C.ATR", "C.ATR"));  // not absolute
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, "srv-b", "", "C.ATR"));
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, "srv-b", "/C\x07.ATR", "C.ATR"));  // control byte
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, "srv-b", "/C.ATR", ""));
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, nullptr, "/C.ATR", "C.ATR"));
    char longServer[DriveMemory::kMaxServer + 2];
    std::memset(longServer, 's', sizeof longServer - 1);
    longServer[sizeof longServer - 1] = '\0';
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, longServer, "/C.ATR", "C.ATR"));
    char longPath[DriveMemory::kMaxPath + 2];
    std::memset(longPath, 'p', sizeof longPath - 1);
    longPath[0] = '/';
    longPath[sizeof longPath - 1] = '\0';
    TEST_ASSERT_FALSE(memory.setNet(4, O::NetAtr, "srv-b", longPath, "C.ATR"));
    TEST_ASSERT_TRUE(memory == before);

    longServer[DriveMemory::kMaxServer] = '\0';  // exactly at the limits: accepted
    longPath[DriveMemory::kMaxPath] = '\0';
    TEST_ASSERT_TRUE(memory.setNet(4, O::NetXex, longServer, longPath, "C.XEX"));

    TEST_ASSERT_TRUE(memory.set(4, "LOCAL.ATR"));  // a local name replaces the network fields
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(O::Local), static_cast<uint8_t>(memory.origin(4)));
    TEST_ASSERT_EQUAL_STRING("", memory.server(4));
    TEST_ASSERT_EQUAL_STRING("", memory.path(4));
    TEST_ASSERT_TRUE(memory.set(4, ""));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(O::Empty), static_cast<uint8_t>(memory.origin(4)));
}

static void test_a_version_3_slot_round_trips_network_drives(void)
{
    const DriveMemory memory = withNetwork();
    codec::Block block{};
    codec::encode(memory, 77, block);
    DriveMemorySlot slot{};
    TEST_ASSERT_TRUE(codec::decode(block.data(), block.size(), slot));
    TEST_ASSERT_TRUE(slot.memory == memory);
    TEST_ASSERT_EQUAL_UINT32(77, slot.seq);
    TEST_ASSERT_EQUAL_STRING("tnfs.example.org", slot.memory.server(2));
    TEST_ASSERT_EQUAL_STRING("/xex/Ca\xC3\xB1on.xex", slot.memory.path(3));  // UTF-8 kept as is
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriveMemory::Origin::NetXex),
                            static_cast<uint8_t>(slot.memory.origin(3)));
}

static void test_a_version_3_slot_with_inconsistent_network_fields_is_rejected(void)
{
    codec::Block good{};
    codec::encode(withNetwork(), 8, good);
    DriveMemorySlot slot{DriveMemory{}, 3};
    TEST_ASSERT_TRUE(slot.memory.set(1, "KEEP.ATR"));
    const DriveMemorySlot before = slot;
    auto net = [](codec::Block& b, unsigned drive) { return b.data() + codec::kNetOffset + (drive - 1) * codec::kNetField; };
    const std::size_t pathAt = 1 + DriveMemory::kMaxServer + 1;

    auto rejects = [&](codec::Block& b, const char* what) {
        reseal(b);
        TEST_ASSERT_FALSE_MESSAGE(codec::decode(b.data(), b.size(), slot), what);
        TEST_ASSERT_TRUE_MESSAGE(slot.memory == before.memory && slot.seq == before.seq, what);
    };

    codec::Block unknown = good;
    net(unknown, 2)[0] = 9;
    rejects(unknown, "unknown origin");

    codec::Block emptyNamed = good;
    net(emptyNamed, 1)[0] = 0;  // D1 is named in sample(): Empty with a name
    rejects(emptyNamed, "empty origin with a name");

    codec::Block localWithServer = good;
    net(localWithServer, 1)[1] = 'x';  // D1 Local with a server
    rejects(localWithServer, "local with a server");

    codec::Block netNoServer = good;
    std::memset(net(netNoServer, 2) + 1, 0, DriveMemory::kMaxServer + 1);
    rejects(netNoServer, "network without server");

    codec::Block netRelative = good;
    net(netRelative, 2)[pathAt] = 'j';
    rejects(netRelative, "network path not absolute");

    codec::Block netNoName = good;
    std::memset(netNoName.data() + codec::kNamesOffset + codec::kNameField, 0, codec::kNameField);  // D2's name
    rejects(netNoName, "network without a name");

    codec::Block pathTail = good;
    net(pathTail, 2)[pathAt + DriveMemory::kMaxPath] = 'Z';  // garbage after the path's NUL
    rejects(pathTail, "bytes after the path");

    codec::Block pathControl = good;
    net(pathControl, 2)[pathAt + 1] = 0x7F;
    rejects(pathControl, "DEL in the path");

    reseal(good);  // the positive control
    TEST_ASSERT_TRUE(codec::decode(good.data(), good.size(), slot));
}

// Review of af5974c: decode() of a version 1 or 2 slot touches nothing past its size. The bytes
// are copied into a heap block of exactly that size, so a read past it is a heap overflow for
// ASan (the sanitizer job of the CI); the network fields are not read for those versions.
static void test_old_slots_decode_from_a_buffer_of_exactly_their_size(void)
{
    const auto one = versionOne(sample(), 30);
    const auto two = versionTwo(sample(), 31);
    const std::unique_ptr<uint8_t[]> exactOne(new uint8_t[one.size()]);
    const std::unique_ptr<uint8_t[]> exactTwo(new uint8_t[two.size()]);
    std::memcpy(exactOne.get(), one.data(), one.size());
    std::memcpy(exactTwo.get(), two.data(), two.size());
    static DriveMemorySlot slot{};  // ~3.5 KB: not on the stack
    TEST_ASSERT_TRUE(codec::decode(exactOne.get(), one.size(), slot));
    TEST_ASSERT_TRUE(slot.memory == sample());
    TEST_ASSERT_TRUE(codec::decode(exactTwo.get(), two.size(), slot));
    TEST_ASSERT_EQUAL_UINT32(31, slot.seq);
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        const bool named = sample().name(drive)[0] != '\0';
        TEST_ASSERT_TRUE(slot.memory.origin(drive) ==
                         (named ? DriveMemory::Origin::Local : DriveMemory::Origin::Empty));
        TEST_ASSERT_EQUAL_STRING("", slot.memory.server(drive));
        TEST_ASSERT_EQUAL_STRING("", slot.memory.path(drive));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_tape_is_empty_by_default_and_takes_only_valid_names);
    RUN_TEST(test_a_version_3_slot_round_trips_the_tape);
    RUN_TEST(test_a_version_2_slot_is_read_as_local_drives);
    RUN_TEST(test_old_slots_decode_from_a_buffer_of_exactly_their_size);
    RUN_TEST(test_a_network_drive_is_set_checked_and_replaced);
    RUN_TEST(test_a_version_3_slot_round_trips_network_drives);
    RUN_TEST(test_a_version_3_slot_with_inconsistent_network_fields_is_rejected);
    RUN_TEST(test_a_version_1_slot_is_read_with_no_tape);
    RUN_TEST(test_a_new_memory_has_every_drive_empty);
    RUN_TEST(test_set_rejects_drives_out_of_range_and_names_too_long);
    RUN_TEST(test_a_slot_round_trips_the_names_and_the_sequence);
    RUN_TEST(test_a_slot_with_a_changed_byte_is_rejected);
    RUN_TEST(test_a_truncated_slot_is_rejected);
    RUN_TEST(test_a_sealed_slot_with_a_wrong_header_or_name_field_is_rejected);
    RUN_TEST(test_a_slot_rejected_at_the_last_drive_leaves_the_output_unchanged);
    RUN_TEST(test_newest_picks_the_valid_slot_with_the_highest_sequence);
    return UNITY_END();
}
