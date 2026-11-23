// Remembered TNFS drives (menu-tnfs-persistente T5; AC1 and AC9 on the controller side): an
// image mounted or assigned from a TNFS source is remembered by the server its source named
// when it was chosen and its absolute path (NetAtr, or NetXex for a .xex), and snapshot()
// writes it in a DriveMemory v3. Local drives are remembered as before (regressions), a local
// .xex stays empty, and restore() never looks a TNFS drive up in the local catalog (restoring
// it over the network is T6). The rig is test_menu_tnfs's, with its own local folder.

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "menu_tnfs_rig.hpp"

namespace {

using rb::host::DriveMemory;
using Origin = rb::host::DriveMemory::Origin;

Bytes goodXex()  // a program at $3000 (an RTS) with its RUNAD: two segments
{
    return {0xFF, 0xFF, 0x00, 0x30, 0x00, 0x30, 0x60, 0xE0, 0x02, 0xE1, 0x02, 0x00, 0x30};
}

void writeFile(const std::string& path, const Bytes& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// The local folder, in menu order: A DISCO.ATR, B JUEGO.XEX.
void writeFolder()
{
    const std::string dir = RB_TEST_MENU_DIR;
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    writeFile(dir + "/DISCO.ATR", atr(720, 1));
    writeFile(dir + "/JUEGO.XEX", goodXex());
}

// DriveMemory is ~3.5 KB: on the heap, never on the stack (plan R3).
std::unique_ptr<DriveMemory> snapshotOf(Rig& rig)
{
    auto out = std::make_unique<DriveMemory>();
    (void)rig.controller->snapshot(*out);
    return out;
}

// Source 2, /Aportes_Juan_Larrain/0- Prueba/Atari Logo.atr, on drive `digit`.
void mountLogo(Rig& rig, uint8_t digit)
{
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(digit);
    rig.step();
}

void assertNet(const DriveMemory& memory, unsigned drive, Origin origin, const char* server, const char* path,
               const char* name)
{
    TEST_ASSERT_TRUE(memory.origin(drive) == origin);
    TEST_ASSERT_EQUAL_STRING(server, memory.server(drive));
    TEST_ASSERT_EQUAL_STRING(path, memory.path(drive));
    TEST_ASSERT_EQUAL_STRING(name, memory.name(drive));
}

void assertEmptyExcept(const DriveMemory& memory, unsigned keep)
{
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        if (drive != keep) {
            TEST_ASSERT_TRUE_MESSAGE(memory.origin(drive) == Origin::Empty, "another drive is remembered");
        }
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_a_tnfs_atr_on_d2_is_remembered_by_its_server_and_path()
{
    Rig rig;
    rig.start();
    mountLogo(rig, K2);
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(2), 6);
    const auto memory = snapshotOf(rig);
    assertNet(*memory, 2, Origin::NetAtr, "pulgarcito", kLogoPath, "ATARI LOGO.ATR");
    assertEmptyExcept(*memory, 2);
}

void test_a_tnfs_atr_assigned_to_d1_is_remembered_too()
{
    Rig rig;
    rig.start();
    mountLogo(rig, K1);
    TEST_ASSERT_EQUAL_STRING("ATARI LOGO.ATR", rig.controller->driveName(1));
    const auto memory = snapshotOf(rig);
    assertNet(*memory, 1, Origin::NetAtr, "pulgarcito", kLogoPath, "ATARI LOGO.ATR");
    assertEmptyExcept(*memory, 1);
}

void test_a_tnfs_xex_on_d1_is_remembered_as_net_xex()
{
    Rig rig;
    rig.server.addFile("/Juegos/nave.xex", goodXex());  // /Juegos: A NAVE.XEX, B ROTA.ATR
    rig.start();
    rig.walk({K2, KB});
    rig.press(KA);
    rig.settle(K1);
    TEST_ASSERT_EQUAL_STRING("NAVE.XEX", rig.controller->driveName(1));
    const auto memory = snapshotOf(rig);
    assertNet(*memory, 1, Origin::NetXex, "pulgarcito", "/Juegos/nave.xex", "NAVE.XEX");
    assertEmptyExcept(*memory, 1);
}

// AC9 (controller side): the server is the one source 2 named when the image was chosen; a
// later E on source 2 does not move the remembered drive.
void test_the_remembered_server_is_the_one_when_chosen_not_after_e()
{
    Rig rig;
    rig.start();
    mountLogo(rig, K2);
    TEST_ASSERT_EQUAL_UINT(2, rig.sources.set(2, "srv-b"));
    TEST_ASSERT_EQUAL_STRING("srv-b", rig.sources.name(2));
    const auto memory = snapshotOf(rig);
    assertNet(*memory, 2, Origin::NetAtr, "pulgarcito", kLogoPath, "ATARI LOGO.ATR");
}

// Regressions: a local image is remembered by its catalog name (Local) and replaces a TNFS one;
// a local .xex on D1 stays empty, as before; an unmounted drive is forgotten.
void test_local_drives_are_remembered_as_before_and_a_local_xex_stays_empty()
{
    Rig rig;
    rig.start();
    mountLogo(rig, K2);
    rig.press(K1);
    rig.press(KA);  // DISCO.ATR
    rig.settle(K2);
    rig.step();
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", rig.mounted(2));
    rig.press(K1);
    rig.press(KB);  // JUEGO.XEX
    rig.settle(K1);
    TEST_ASSERT_EQUAL_STRING("JUEGO.XEX", rig.controller->driveName(1));
    const auto memory = snapshotOf(rig);
    TEST_ASSERT_TRUE(memory->origin(2) == Origin::Local);
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", memory->name(2));
    TEST_ASSERT_EQUAL_STRING("", memory->server(2));
    TEST_ASSERT_EQUAL_STRING("", memory->path(2));
    TEST_ASSERT_TRUE(memory->origin(1) == Origin::Empty);  // a local .xex: not remembered
    assertEmptyExcept(*memory, 2);
}

// What snapshot() gives survives the v3 codec: a NetXex D1, a Local D2 and a NetAtr D3.
void test_a_mixed_snapshot_round_trips_through_the_v3_codec()
{
    Rig rig;
    rig.server.addFile("/Juegos/nave.xex", goodXex());
    rig.start();
    rig.walk({K2, KB});
    rig.press(KA);
    rig.settle(K1);
    rig.press(K1);
    rig.press(KA);
    rig.settle(K2);
    rig.step();
    mountLogo(rig, K3);
    const auto memory = snapshotOf(rig);
    TEST_ASSERT_TRUE(memory->origin(1) == Origin::NetXex);
    TEST_ASSERT_TRUE(memory->origin(2) == Origin::Local);
    TEST_ASSERT_TRUE(memory->origin(3) == Origin::NetAtr);

    auto block = std::make_unique<rb::host::drive_memory_codec::Block>();
    rb::host::drive_memory_codec::encode(*memory, 7, *block);
    auto slot = std::make_unique<rb::host::DriveMemorySlot>();
    TEST_ASSERT_TRUE(rb::host::drive_memory_codec::decode(block->data(), block->size(), *slot));
    TEST_ASSERT_EQUAL_UINT32(7, slot->seq);
    assertNet(slot->memory, 1, Origin::NetXex, "pulgarcito", "/Juegos/nave.xex", "NAVE.XEX");
    TEST_ASSERT_TRUE(slot->memory.origin(2) == Origin::Local);
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", slot->memory.name(2));
    assertNet(slot->memory, 3, Origin::NetAtr, "pulgarcito", kLogoPath, "ATARI LOGO.ATR");
}

// restore(): a remembered TNFS drive is not looked up in the local catalog, even when a local
// image has the same name; the local drives are restored as before and nothing says FALTA.
void test_restore_leaves_a_tnfs_drive_to_the_network_and_restores_the_local_ones()
{
    Rig rig;
    rig.start();
    auto memory = std::make_unique<DriveMemory>();
    TEST_ASSERT_TRUE(memory->setNet(2, Origin::NetAtr, "pulgarcito", "/DISCO.ATR", "DISCO.ATR"));
    TEST_ASSERT_TRUE(memory->set(3, "DISCO.ATR"));
    rig.controller->beginRestore();
    rig.controller->restore(*memory);
    for (int i = 0; i < 20 && rig.controller->restoring(); ++i) {
        rig.step();
    }
    TEST_ASSERT_FALSE(rig.controller->restoring());
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", rig.mounted(3));
    TEST_ASSERT_TRUE(rig.events.has("D2 net not restored"));
    TEST_ASSERT_FALSE(rig.events.has("FALTA"));
}

int main(void)
{
    writeFolder();
    UNITY_BEGIN();
    RUN_TEST(test_a_tnfs_atr_on_d2_is_remembered_by_its_server_and_path);
    RUN_TEST(test_a_tnfs_atr_assigned_to_d1_is_remembered_too);
    RUN_TEST(test_a_tnfs_xex_on_d1_is_remembered_as_net_xex);
    RUN_TEST(test_the_remembered_server_is_the_one_when_chosen_not_after_e);
    RUN_TEST(test_local_drives_are_remembered_as_before_and_a_local_xex_stays_empty);
    RUN_TEST(test_a_mixed_snapshot_round_trips_through_the_v3_codec);
    RUN_TEST(test_restore_leaves_a_tnfs_drive_to_the_network_and_restores_the_local_ones);
    return UNITY_END();
}
