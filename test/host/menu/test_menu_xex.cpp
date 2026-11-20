// Choosing and booting a .xex from the menu (spec menu-xex, X004): only D1, the
// messages for an invalid or incompatible file, and the disk A mounts. The local
// folder is this program's own (RB_TEST_MENU_DIR), written by main() before the
// tests; the TNFS side is the fake server of the rig. Every 'K' is checked by the
// rig: answered within 250 ms and without allocating on the menu thread (ADR-011),
// so the .xex is read and wrapped on the loader thread.

#include <filesystem>
#include <fstream>
#include <string>

#include "menu_tnfs_rig.hpp"
#include "xex_loader.inc"

namespace {

constexpr uint8_t KD = 0x3A;  // Atari keyboard code of D

Bytes segment(uint16_t start, const Bytes& data, bool header = true)
{
    Bytes out;
    if (header) {
        out = {0xFF, 0xFF};
    }
    const uint16_t end = static_cast<uint16_t>(start + data.size() - 1);
    out.push_back(static_cast<uint8_t>(start & 0xFF));
    out.push_back(static_cast<uint8_t>(start >> 8));
    out.push_back(static_cast<uint8_t>(end & 0xFF));
    out.push_back(static_cast<uint8_t>(end >> 8));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Bytes operator+(Bytes a, const Bytes& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// A program at $3000 (an RTS) with its RUNAD: two segments.
Bytes goodXex() { return segment(0x3000, {0x60}) + segment(0x02E0, {0x00, 0x30}); }
Bytes badXex() { return segment(0x3000, {0x60}, false); }  // no $FFFF at the start
Bytes stackXex() { return segment(0x0180, {0x00}) + segment(0x02E0, {0x00, 0x30}); }  // page 1

void writeFile(const std::string& path, const Bytes& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// The local folder, in menu order: A DISCO.ATR, B JUEGO.XEX, C PILA.XEX, D ROTO.XEX,
// E ZZ.XEX (removed by its test after the menu listed it: the last letter, so no other moves).
void writeFolder()
{
    const std::string dir = RB_TEST_MENU_DIR;
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    writeFile(dir + "/DISCO.ATR", atr(720, 1));
    writeFile(dir + "/JUEGO.XEX", goodXex());
    writeFile(dir + "/PILA.XEX", stackXex());
    writeFile(dir + "/ROTO.XEX", badXex());
    writeFile(dir + "/ZZ.XEX", goodXex());
}

std::string d1Line(const char* name)
{
    std::string line = std::string("D1 ") + name;
    line.resize(33, ' ');
    return line + "SL";
}

void addXexFiles(Rig& rig)
{
    rig.server.addFile("/Juegos/nave.xex", goodXex());  // /Juegos: A NAVE.XEX, B ROTA.ATR
}

// The slot D1 serves after A: "*tnfs*<n>".
std::size_t mountedSlot(Rig& rig)
{
    const std::string location = rig.mounted(1);
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", location.c_str(), 6);
    return static_cast<std::size_t>(std::stoul(location.substr(6)));
}

void assertServesLoaderThen(Rig& rig, const Bytes& xex)
{
    rb::domain::ImageByteSource& source = rig.repository->memory().source(mountedSlot(rig));
    std::array<uint8_t, 16 + sizeof kXexLoaderBlock + 128> disk{};
    TEST_ASSERT_TRUE(source.readAt(0, Span<uint8_t>(disk.data(), disk.size())).ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kXexLoaderBlock, disk.data() + 16, kXexLoaderLengthOffset);
    TEST_ASSERT_EQUAL_UINT8(xex.size(), disk[16 + kXexLoaderLengthOffset]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(xex.data(), disk.data() + 16 + sizeof kXexLoaderBlock, xex.size());
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// AC3 and AC5 (host): a local .xex chosen for D1 shows its name; A mounts the wrapped disk.
void test_a_local_xex_goes_to_d1_and_a_boots_the_loader_disk()
{
    Rig rig;
    rig.start();
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, rig.press(K1).layout());
    assert_line(rig.press(KB), kMainMessage, " UNIDAD PARA: JUEGO.XEX");
    const MenuReply assigned = rig.settle(K1);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, assigned.layout());
    assert_line(assigned, driveLine(1), d1Line("JUEGO.XEX").c_str());
    assert_line(assigned, kMainMessage, " D1: JUEGO.XEX (A ARRANCA)");
    TEST_ASSERT_TRUE(rig.events.has("D1 name=JUEGO.XEX assigned xex=1 segments=2 bytes=15"));
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    rig.step();
    assertServesLoaderThen(rig, goodXex());
}

// AC4: D2..D8 refuse a .xex, local or TNFS, and no drive changes.
void test_a_xex_on_another_drive_says_it_only_goes_on_d1()
{
    Rig rig;
    addXexFiles(rig);
    rig.start();
    rig.press(K1);
    rig.press(KB);
    const MenuReply local = rig.settle(K2);
    assert_line(local, kMainMessage, " UN XEX SOLO VA EN D1");
    rig.step();
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
    TEST_ASSERT_NULL(rig.controller->driveName(2));
    TEST_ASSERT_NULL(rig.controller->driveName(1));

    rig.walk({K2, KB});  // source 2, JUEGOS/
    rig.press(KA);       // NAVE.XEX
    const MenuReply tnfs = rig.settle(K3);
    assert_line(tnfs, kListMessage, " UN XEX SOLO VA EN D1");
    rig.step();
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(3));
    TEST_ASSERT_NULL(rig.controller->driveName(3));
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(rig.store()));
}

// AC8: a file that is no valid .xex is refused when chosen; D1 keeps what it had.
void test_an_invalid_xex_says_so_and_d1_does_not_change()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    rig.press(KA);
    rig.settle(K1);  // DISCO.ATR on D1
    rig.press(K1);
    rig.press(KD);  // ROTO.XEX
    const MenuReply refused = rig.settle(K1);
    assert_line(refused, kMainMessage, " XEX NO VALIDO");
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", rig.controller->driveName(1));
    TEST_ASSERT_TRUE(rig.events.has("D1 name=ROTO.XEX refused: result=invalid"));
    awaitSlots(rig.store(), 0);
}

// AC9: a segment on the loader or on page 1 is refused when chosen; D1 does not change.
void test_an_incompatible_xex_says_so_and_d1_does_not_change()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    rig.press(KC);  // PILA.XEX
    const MenuReply refused = rig.settle(K1);
    assert_line(refused, kMainMessage, " XEX NO COMPATIBLE");
    TEST_ASSERT_NULL(rig.controller->driveName(1));
    TEST_ASSERT_TRUE(rig.events.has("D1 name=PILA.XEX refused: result=incompatible"));
    awaitSlots(rig.store(), 0);
}

// AC2, AC3 and AC5 (host) over TNFS: the .xex comes whole, is wrapped, and A boots it.
void test_a_tnfs_xex_goes_to_d1_and_a_boots_the_loader_disk()
{
    Rig rig;
    addXexFiles(rig);
    rig.start();
    rig.walk({K2, KB});
    assert_line(rig.press(KA), kMainMessage, " UNIDAD PARA: NAVE.XEX");
    const MenuReply assigned = rig.settle(K1);
    assert_line(assigned, driveLine(1), d1Line("NAVE.XEX").c_str());
    assert_line(assigned, kMainMessage, " D1: NAVE.XEX (A ARRANCA)");
    TEST_ASSERT_TRUE(rig.events.has("D1 name=NAVE.XEX assigned xex=1 segments=2 bytes=15"));
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    rig.step();
    assertServesLoaderThen(rig, goodXex());
    TEST_ASSERT_EQUAL_size_t(1, slotsInUse(rig.store()));  // only the wrapped disk
}

// A listed .xex that is gone when chosen: the loader cannot open it; D1 does not change.
void test_a_local_xex_that_is_gone_says_it_could_not_open_it()
{
    Rig rig;  // the catalog lists ZZ.XEX
    std::error_code ignored;
    std::filesystem::remove(std::string(RB_TEST_MENU_DIR) + "/ZZ.XEX", ignored);
    rig.start();
    rig.press(K1);
    rig.press(KE);  // ZZ.XEX
    const MenuReply refused = rig.settle(K1);
    assert_line(refused, kMainMessage, " ERROR: NO SE PUDO ABRIR");
    TEST_ASSERT_NULL(rig.controller->driveName(1));
    awaitSlots(rig.store(), 0);
}

// AC10: an .ATR of the same folder still goes to any drive.
void test_an_atr_next_to_the_xex_still_mounts_on_d2()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    rig.press(KA);
    rig.settle(K2);
    rig.step();
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", rig.mounted(2));
}

int main(void)
{
    writeFolder();
    UNITY_BEGIN();
    RUN_TEST(test_a_local_xex_goes_to_d1_and_a_boots_the_loader_disk);
    RUN_TEST(test_a_xex_on_another_drive_says_it_only_goes_on_d1);
    RUN_TEST(test_an_invalid_xex_says_so_and_d1_does_not_change);
    RUN_TEST(test_an_incompatible_xex_says_so_and_d1_does_not_change);
    RUN_TEST(test_a_tnfs_xex_goes_to_d1_and_a_boots_the_loader_disk);
    RUN_TEST(test_a_local_xex_that_is_gone_says_it_could_not_open_it);
    RUN_TEST(test_an_atr_next_to_the_xex_still_mounts_on_d2);
    return UNITY_END();
}
