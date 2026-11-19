// Cursor and pages in the local and TNFS lists (menu-navegacion N003; AC2 to AC5,
// AC6 with the digit, AC7, AC8 on the host, AC9 to AC13): MenuController over a
// real TnfsLoader and FakeTnfsServer, as in test_menu_tnfs. Every key goes through
// Rig::press, so each one is timed (250 ms at most) and must not allocate (AC9).
//
// The visible numbering starts at 1: entry 128 is index 127, the last value of
// the former int8_t, and entry 999 is index 998 (plan §8, point 1).

#include "menu_tnfs_rig.hpp"

namespace {

constexpr uint8_t kUp = rb::host::kMenuNavUp;
constexpr uint8_t kDown = rb::host::kMenuNavDown;
constexpr uint8_t kLeft = rb::host::kMenuNavLeft;
constexpr uint8_t kRight = rb::host::kMenuNavRight;
constexpr uint8_t kFire = rb::host::kMenuNavFire;
constexpr uint8_t kReturn = 0x0C;
constexpr uint8_t KD = 0x3A, KF = 0x38;  // Atari keyboard codes of D and F
constexpr uint8_t kEquals = 0x0F;  // '=' without Control: a plain key

// /OMS_ATR with `count` images G000.ATR..; `valid` is a real ATR.
void addImages(Rig& rig, const char* folder, const char* format, int count, int valid)
{
    char name[64];
    char path[96];
    for (int i = 0; i < count; ++i) {
        std::snprintf(name, sizeof name, format, i);
        std::snprintf(path, sizeof path, "%s/%s", folder, name);
        rig.server.addFile(path, i == valid ? atr(720, 7) : Bytes(16, 0));
    }
}

MenuReply repeat(Rig& rig, uint8_t code, int times)
{
    MenuReply reply;
    for (int i = 0; i < times; ++i) {
        reply = rig.settle(code);
    }
    return reply;
}

// Root of source 2: APORTES_JUAN_LARRAIN/, JUEGOS/, <added folders>, VACIA/, images.
MenuReply openOms(Rig& rig)
{
    rig.settle(K2);
    return rig.settle(KC);  // APORTES_JUAN_LARRAIN/, JUEGOS/, OMS_ATR/
}

bool sameScreen(const MenuReply& a, const MenuReply& b)
{
    return std::memcmp(a.screen(), b.screen(), rb::host::kMenuScreenSize) == 0;
}

// True if the 40 bytes at offset are all in inverse video; false if none is.
bool marked(const MenuReply& reply, unsigned offset)
{
    unsigned inverse = 0;
    for (unsigned i = 0; i < 40; ++i) {
        inverse += (reply.screen()[offset + i] & 0x80) != 0 ? 1u : 0u;
    }
    TEST_ASSERT_TRUE_MESSAGE(inverse == 0 || inverse == 40, "a line half in inverse video");
    return inverse == 40;
}

// The one marked line among the 8 at `first`, 1-based; 0 if none.
unsigned markedOf(const MenuReply& reply, unsigned first)
{
    unsigned found = 0;
    for (unsigned n = 1; n <= 8; ++n) {
        if (marked(reply, first + (n - 1) * 40)) {
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, found, "two marked lines");
            found = n;
        }
    }
    return found;
}

}  // namespace

void setUp() {}
void tearDown() {}

// AC2: 205 entries, page 1, the cursor on row 1.
void test_205_entries_open_on_page_1_with_the_cursor_on_row_1()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, -1);
    rig.start();
    const MenuReply oms = openOms(rig);
    assert_title(oms, " FUENTE 2: /OMS_ATR");
    assert_marked(oms, row(0), "A G000.ATR");
    assert_line(oms, row(1), "B G001.ATR");
    assert_line(oms, row(19), "T G019.ATR");
    assert_line(oms, kListMessage, " PAGINA 1 DE 11 (205)");
}

// AC3: right ten times, page 11 with 5 rows; once more changes nothing.
void test_right_ten_times_reaches_page_11_and_stops()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, -1);
    rig.start();
    openOms(rig);
    const MenuReply last = repeat(rig, kRight, 10);
    assert_marked(last, row(0), "A G200.ATR");
    assert_line(last, row(4), "E G204.ATR");
    assert_line(last, row(5), "");
    assert_line(last, kListMessage, " PAGINA 11 DE 11 (205)");
    TEST_ASSERT_TRUE(sameScreen(last, rig.settle(kRight)));
    // Down stops on the last row of the last page.
    const MenuReply bottom = repeat(rig, kDown, 4);
    assert_marked(bottom, row(4), "E G204.ATR");
    TEST_ASSERT_TRUE(sameScreen(bottom, rig.settle(kDown)));
    // Left goes back a page, the cursor on its first row.
    const MenuReply back = rig.settle(kLeft);
    assert_marked(back, row(0), "A G180.ATR");
    assert_line(back, kListMessage, " PAGINA 10 DE 11 (205)");
}

// AC4 and the step from visible entry 20 to 21 (plan §8).
void test_down_from_row_20_shows_page_2_on_its_first_row_and_up_comes_back()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, -1);
    rig.start();
    const MenuReply top = openOms(rig);
    TEST_ASSERT_TRUE(sameScreen(top, rig.settle(kUp)));  // first row of page 1: no further
    const MenuReply twenty = repeat(rig, kDown, 19);
    assert_marked(twenty, row(19), "T G019.ATR");
    assert_line(twenty, row(18), "S G018.ATR");
    const MenuReply next = rig.settle(kDown);
    assert_marked(next, row(0), "A G020.ATR");
    assert_line(next, kListMessage, " PAGINA 2 DE 11 (205)");
    const MenuReply up = rig.settle(kUp);
    assert_marked(up, row(19), "T G019.ATR");
    assert_line(up, kListMessage, " PAGINA 1 DE 11 (205)");
}

// AC5: a letter chooses on the visible page.
void test_c_on_page_3_chooses_entry_43()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, -1);
    rig.start();
    openOms(rig);
    repeat(rig, kRight, 2);
    const MenuReply pick = rig.settle(KC);
    assert_line(pick, kMainMessage, " UNIDAD PARA: G042.ATR");
    // Esc keeps the page; the cursor is on the row chosen.
    const MenuReply back = rig.settle(KESC);
    assert_marked(back, row(2), "C G042.ATR");
    assert_line(back, kListMessage, " PAGINA 3 DE 11 (205)");
}

// AC6 (drive by digit; the cursor over D1-D8 is N004): RETURN and fire act as the letter.
void test_return_on_a_marked_image_then_2_mounts_d2_as_the_letter_does()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, 42);
    rig.start();
    openOms(rig);
    repeat(rig, kRight, 2);
    repeat(rig, kDown, 2);
    const MenuReply pick = rig.settle(kReturn);
    assert_line(pick, kMainMessage, " UNIDAD PARA: G042.ATR");
    rig.settle(K2);
    TEST_ASSERT_EQUAL_STRING("G042.ATR", rig.controller->driveName(2));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(rig.mounted(2)));
}

void test_fire_on_a_marked_folder_enters_it()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 3, -1);
    rig.start();
    rig.settle(K2);
    repeat(rig, kDown, 2);
    const MenuReply oms = rig.settle(kFire);
    assert_title(oms, " FUENTE 2: /OMS_ATR");
    assert_marked(oms, row(0), "A G000.ATR");
    assert_line(oms, kListMessage, " PULSA LA LETRA DE LA IMAGEN");
}

// AC7 and AC12: Esc comes back to page 2, row 7; an empty folder has no marked row.
void test_esc_from_a_subfolder_restores_page_and_row()
{
    Rig rig;
    char path[32];
    for (int i = 0; i < 30; ++i) {
        std::snprintf(path, sizeof path, "/Big/D%02d", i);
        rig.server.addDirectory(path);
    }
    rig.start();
    rig.settle(K2);
    const MenuReply big = rig.settle(KB);  // APORTES_JUAN_LARRAIN/, BIG/, JUEGOS/, VACIA/
    assert_title(big, " FUENTE 2: /BIG");
    rig.settle(kRight);
    const MenuReply seven = repeat(rig, kDown, 6);
    assert_marked(seven, row(6), "G D26/");
    const MenuReply empty = rig.settle(kReturn);
    assert_title(empty, " FUENTE 2: /BIG/D26");
    assert_line(empty, row(0), "");
    assert_line(empty, kListMessage, " CARPETA VACIA");
    TEST_ASSERT_TRUE(sameScreen(empty, rig.settle(kReturn)));
    TEST_ASSERT_TRUE(sameScreen(empty, rig.settle(kFire)));
    TEST_ASSERT_TRUE(sameScreen(empty, rig.settle(kDown)));
    const MenuReply back = rig.settle(KESC);
    assert_title(back, " FUENTE 2: /BIG");
    assert_marked(back, row(6), "G D26/");
    assert_line(back, row(0), "A D20/");
    assert_line(back, kListMessage, " PAGINA 2 DE 2 (30)");
    // Esc again: the root, at the row of BIG/.
    const MenuReply root = rig.settle(KESC);
    assert_title(root, " FUENTE 2: /");
    assert_marked(root, row(1), "B BIG/");
}

// Plan §8, point 1: entries 128 to 129 (indices 127 to 128) and entry 999 (index 998).
void test_entries_128_129_and_999_are_marked_chosen_and_mounted()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "M%03d.ATR", 999, 998);
    rig.start();
    openOms(rig);
    repeat(rig, kRight, 6);  // page 7: indices 120..139
    const MenuReply e128 = repeat(rig, kDown, 7);
    assert_marked(e128, row(7), "H M127.ATR");
    const MenuReply e129 = rig.settle(kDown);
    assert_marked(e129, row(8), "I M128.ATR");
    assert_line(e129, row(7), "H M127.ATR");
    const MenuReply pick129 = rig.settle(kReturn);
    assert_line(pick129, kMainMessage, " UNIDAD PARA: M128.ATR");
    rig.settle(KESC);
    const MenuReply last = repeat(rig, kRight, 43);  // page 50: indices 980..998
    assert_line(last, kListMessage, " PAGINA 50 DE 50 (999)");
    const MenuReply e999 = repeat(rig, kDown, 18);
    assert_marked(e999, row(18), "S M998.ATR");
    assert_line(e999, row(19), "");
    rig.settle(kReturn);
    rig.settle(K2);
    TEST_ASSERT_EQUAL_STRING("M998.ATR", rig.controller->driveName(2));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(rig.mounted(2)));
}

// AC8 on the host: '=' alone is a plain key and moves nothing.
void test_a_plain_equals_moves_nothing()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, -1);
    rig.start();
    const MenuReply oms = openOms(rig);
    TEST_ASSERT_TRUE(sameScreen(oms, rig.settle(kEquals)));
    assert_marked(rig.settle(kDown), row(1), "B G001.ATR");
}

// AC10: the local list pages with the same rules; index 200 (beyond int8_t) mounts.
void test_the_local_list_pages_and_mounts_an_entry_beyond_128()
{
    Rig rig;
    char name[16];
    for (int i = 0; i < 200; ++i) {
        std::snprintf(name, sizeof name, "c%03d.atr", i);  // before demo-dos.atr, demo-uno.atr, rota.atr
        TEST_ASSERT_TRUE(rig.catalog.offer(name));
    }
    rig.start();
    const MenuReply list = rig.settle(K1);
    assert_marked(list, row(0), " A  C000.ATR");
    assert_line(list, row(1), " B  C001.ATR");
    assert_line(list, kListMessage, " PAGINA 1 DE 11 (203)");
    const MenuReply page11 = repeat(rig, kRight, 10);
    assert_marked(page11, row(0), " A  DEMO-DOS.ATR");
    assert_line(page11, row(2), " C  ROTA.ATR");
    assert_line(page11, row(3), "");
    assert_line(page11, kListMessage, " PAGINA 11 DE 11 (203)");
    const MenuReply pick = rig.settle(kReturn);
    assert_line(pick, kMainMessage, " UNIDAD PARA: DEMO-DOS.ATR");
    rig.settle(K2);
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.controller->driveName(2));
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(2));
    // Opening the list again starts on page 1, row 1.
    const MenuReply again = rig.settle(K1);
    assert_marked(again, row(0), " A  C000.ATR");
}

// AC11: while "CARGANDO nn%", moves and RETURN change nothing; Esc cancels.
void test_moves_and_return_do_nothing_while_loading()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 30, -1);
    rig.start();
    rig.settle(K2);
    repeat(rig, kDown, 2);
    rig.gated.gate(tnfs::cmd::kStat);
    const MenuReply loading = rig.press(kReturn);
    assert_loading(loading, kListMessage, "0");
    rig.gated.awaitHeld();
    for (uint8_t code : {kUp, kDown, kLeft, kRight, kFire, kReturn}) {
        const MenuReply still = rig.press(code);
        TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, still.action());
        assert_title(still, " FUENTE 2: /");
        assert_marked(still, row(2), "C OMS_ATR/");
    }
    const MenuReply cancelled = rig.press(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, cancelled.action());
    assert_title(cancelled, " FUENTE 2: /");
    assert_marked(cancelled, row(2), "C OMS_ATR/");
    rig.gated.open();
}

// Past 32 levels the position is not kept: Esc shows row 1 there, and the 32 levels
// kept above it still come back where they were left.
void test_the_position_stack_keeps_32_levels()
{
    Rig rig;
    std::string path;
    for (int level = 1; level <= 33; ++level) {
        path += "/N";
        rig.server.addDirectory(path);
        rig.server.addDirectory(path + "/A");
        if (level == 31) {
            rig.server.addDirectory(path + "/Z");  // A/, N/, Z/
        }
        if (level == 32) {
            rig.server.addDirectory(path + "/0");  // 0/, A/, N/: N/ is on row 3 here
        }
    }
    rig.start();
    rig.settle(K2);
    rig.settle(KC);  // APORTES_JUAN_LARRAIN/, JUEGOS/, N/: level 1 (root kept)
    MenuReply deepest;
    for (int level = 2; level <= 33; ++level) {
        deepest = rig.settle(level == 33 ? KC : KB);  // level 2..33 (levels 1..32 kept, the 33rd push is lost)
    }
    assert_marked(deepest, row(0), "A A/");
    const MenuReply lost = rig.settle(KESC);  // level 32: its position (row 3) was not kept
    assert_marked(lost, row(0), "A 0/");
    const MenuReply kept = rig.settle(KESC);  // level 31: kept, not overwritten by level 32
    assert_marked(kept, row(1), "B N/");
    assert_line(kept, row(2), "C Z/");
    MenuReply up = kept;
    for (int level = 30; level >= 1; --level) {
        up = rig.settle(KESC);
        assert_marked(up, row(1), "B N/");
    }
    const MenuReply top = rig.settle(KESC);
    assert_title(top, " FUENTE 2: /");
    assert_marked(top, row(2), "C N/");
}

// Opening a source again starts on row 1 of page 1; left and right do nothing on the main screen.
void test_reopening_a_source_starts_on_row_1_and_the_main_screen_ignores_moves()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, -1);
    rig.start();
    openOms(rig);
    repeat(rig, kRight, 2);
    rig.settle(KESC);
    const MenuReply main = rig.settle(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
    for (uint8_t code : {kLeft, kRight}) {
        TEST_ASSERT_TRUE(sameScreen(main, rig.settle(code)));
    }
    const MenuReply again = rig.settle(kFire);  // the cursor is on source 2
    assert_marked(again, row(0), "A APORTES_JUAN_LARRAIN/");
    const MenuReply oms = rig.settle(KC);
    assert_marked(oms, row(0), "A G000.ATR");
    assert_line(oms, kListMessage, " PAGINA 1 DE 11 (205)");
}

// A full local catalog counts every .atr seen.
void test_a_full_local_list_shows_999_of_the_total()
{
    Rig rig;
    char name[16];
    for (int i = 0; i < 1200; ++i) {
        std::snprintf(name, sizeof name, "c%04d.atr", i);
        rig.catalog.offer(name);
    }
    rig.start();
    const MenuReply list = rig.settle(K1);
    assert_line(list, kListMessage, " PAGINA 1 DE 50 (999 DE 1203)");
}

// The parent shrinks on the server while a subfolder is open: Esc keeps page and row
// in range, and RETURN chooses the entry the cursor shows.
void test_esc_to_a_parent_that_shrank_keeps_the_cursor_in_range()
{
    Rig rig;
    char path[32];
    for (int i = 0; i < 45; ++i) {
        std::snprintf(path, sizeof path, "/P/D%02d", i);
        rig.server.addDirectory(path);
    }
    rig.server.addFile("/P/D44/x.atr", Bytes(16, 0));
    rig.start();
    rig.settle(K2);
    rig.settle(KC);  // APORTES_JUAN_LARRAIN/, JUEGOS/, P/
    repeat(rig, kRight, 2);  // page 3: D40..D44
    const MenuReply page3 = repeat(rig, kDown, 4);
    assert_marked(page3, row(4), "E D44/");
    assert_line(page3, kListMessage, " PAGINA 3 DE 3 (45)");
    assert_title(rig.settle(kReturn), " FUENTE 2: /P/D44");
    for (int i = 0; i < 30; ++i) {  // /P keeps D30..D44: one page of 15
        std::snprintf(path, sizeof path, "/P/D%02d", i);
        rig.server.remove(path);
    }
    const MenuReply back = rig.settle(KESC);
    assert_title(back, " FUENTE 2: /P");
    assert_line(back, kListMessage, " PULSA LA LETRA DE LA IMAGEN");  // no page line: 15 entries
    assert_line(back, row(0), "A D30/");
    assert_marked(back, row(14), "O D44/");  // the last entry: page 1, row 15
    assert_line(back, row(15), "");
    const MenuReply chosen = rig.settle(kReturn);
    assert_title(chosen, " FUENTE 2: /P/D44");
    assert_marked(chosen, row(0), "A X.ATR");
}

// With one level kept, a cancelled entry and a failed one keep the folder, page and row;
// the next Esc still goes back to the right parent.
void test_a_cancelled_or_failed_entry_keeps_the_position_and_the_history()
{
    Rig rig;
    char path[32];
    for (int i = 0; i < 30; ++i) {
        std::snprintf(path, sizeof path, "/Big/D%02d", i);
        rig.server.addDirectory(path);
    }
    rig.server.addFile("/Big/D26/x.atr", Bytes(16, 0));
    rig.start();
    rig.settle(K2);
    rig.settle(KB);  // BIG/: the root (row 2) is the level kept
    rig.settle(kRight);
    const MenuReply at26 = repeat(rig, kDown, 6);
    assert_marked(at26, row(6), "G D26/");

    // Cancelled: the STAT of x.atr is held, Esc cancels.
    rig.gated.gate(tnfs::cmd::kStat);
    assert_loading(rig.press(kReturn), kListMessage, "0");
    rig.gated.awaitHeld();
    const MenuReply cancelled = rig.press(KESC);
    rig.gated.open();
    assert_title(cancelled, " FUENTE 2: /BIG");
    assert_marked(cancelled, row(6), "G D26/");
    assert_line(cancelled, kListMessage, " PAGINA 2 DE 2 (30)");

    // Failed: OPENDIR of D27 is refused.
    rig.settle(kDown);
    rig.server.reject(tnfs::cmd::kOpenDir, FakeTnfsServer::kNoEntry);
    const MenuReply failed = rig.settle(kReturn);
    assert_title(failed, " FUENTE 2: /BIG");
    assert_marked(failed, row(7), "H D27/");
    assert_line(failed, kListMessage, " ERROR: NO SE PUDO ABRIR");

    // Esc: the parent of /BIG, where it was left.
    const MenuReply parent = rig.settle(KESC);
    assert_title(parent, " FUENTE 2: /");
    assert_marked(parent, row(1), "B BIG/");
    const MenuReply main = rig.settle(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
}

// --- N004: the main screen and the drive choice (AC1, AC6, M42-4) ---------------------

// AC1: down twice and fire open source 3, exactly as '3' does; the cursor stays on it.
void test_down_twice_and_fire_open_source_3_as_3_does()
{
    Rig byDigit;
    byDigit.start();
    const MenuReply three = byDigit.settle(K3);
    Rig byCursor;
    byCursor.start();
    const MenuReply first = byCursor.settle(kRedraw);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, first.layout());
    TEST_ASSERT_EQUAL_UINT(1, markedOf(first, rb::host::kMainSources));
    TEST_ASSERT_EQUAL_UINT(0, markedOf(first, rb::host::kMainDrives));
    TEST_ASSERT_EQUAL_UINT(2, markedOf(byCursor.settle(kDown), rb::host::kMainSources));
    TEST_ASSERT_EQUAL_UINT(3, markedOf(byCursor.settle(kDown), rb::host::kMainSources));
    const MenuReply fired = byCursor.settle(kFire);
    TEST_ASSERT_TRUE(sameScreen(three, fired));
    TEST_ASSERT_EQUAL_UINT(3, markedOf(fired, rb::host::kMainSources));
}

// Empty sources are on the path; RETURN on one says so as its digit does. The cursor
// stops at sources 1 and 8; left and right change nothing.
void test_the_main_cursor_covers_the_8_sources_and_return_on_an_empty_one()
{
    Rig byDigit;
    byDigit.start();
    const MenuReply five = byDigit.settle(K5);
    Rig rig;
    rig.start();
    const MenuReply top = rig.settle(kRedraw);
    TEST_ASSERT_TRUE(sameScreen(top, rig.settle(kUp)));
    TEST_ASSERT_TRUE(sameScreen(top, rig.settle(kLeft)));
    TEST_ASSERT_TRUE(sameScreen(top, rig.settle(kRight)));
    const MenuReply bottom = repeat(rig, kDown, 10);
    TEST_ASSERT_EQUAL_UINT(8, markedOf(bottom, rb::host::kMainSources));
    const MenuReply at5 = repeat(rig, kUp, 3);
    TEST_ASSERT_EQUAL_UINT(5, markedOf(at5, rb::host::kMainSources));
    const MenuReply pressed = rig.settle(kReturn);
    assert_line(pressed, kMainMessage, " FUENTE 5 SIN CONFIGURAR");
    TEST_ASSERT_TRUE(sameScreen(five, pressed));
    // Esc back from a source leaves the cursor on it.
    rig.settle(K2);
    TEST_ASSERT_EQUAL_UINT(2, markedOf(rig.settle(KESC), rb::host::kMainSources));
}

// AC6: RETURN on an image, then D2 with the cursor and RETURN, mounts D2 as the letter and '2'.
void test_d2_chosen_with_the_cursor_mounts_as_the_letter_and_2()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, 42);
    rig.start();
    openOms(rig);
    repeat(rig, kRight, 2);
    repeat(rig, kDown, 2);
    const MenuReply pick = rig.settle(kReturn);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, pick.layout());
    assert_line(pick, kMainMessage, " UNIDAD PARA: G042.ATR");
    TEST_ASSERT_EQUAL_UINT(1, markedOf(pick, rb::host::kMainDrives));
    TEST_ASSERT_EQUAL_UINT(0, markedOf(pick, rb::host::kMainSources));
    TEST_ASSERT_TRUE(sameScreen(pick, rig.settle(kUp)));  // D1: no further
    // Esc from the drive choice goes back to the folder, page and row kept.
    const MenuReply back = rig.settle(KESC);
    assert_title(back, " FUENTE 2: /OMS_ATR");
    assert_marked(back, row(2), "C G042.ATR");
    assert_line(back, kListMessage, " PAGINA 3 DE 11 (205)");
    rig.settle(kReturn);
    const MenuReply d2 = rig.settle(kDown);
    TEST_ASSERT_EQUAL_UINT(2, markedOf(d2, rb::host::kMainDrives));
    const MenuReply mounted = rig.settle(kReturn);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, mounted.layout());  // as with '2': the main screen
    assert_line(mounted, kMainMessage, " D2: G042.ATR");
    TEST_ASSERT_EQUAL_STRING("G042.ATR", rig.controller->driveName(2));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(rig.mounted(2)));
    TEST_ASSERT_EQUAL_UINT(2, markedOf(mounted, rb::host::kMainSources));  // source 2, where it came from
}

// The local list too: fire on the drive marked by the cursor.
void test_a_local_image_goes_to_the_drive_under_the_cursor()
{
    Rig rig;
    rig.start();
    rig.settle(K1);
    const MenuReply pick = rig.settle(kReturn);  // demo-dos.atr
    assert_line(pick, kMainMessage, " UNIDAD PARA: DEMO-DOS.ATR");
    TEST_ASSERT_EQUAL_UINT(1, markedOf(pick, rb::host::kMainDrives));
    repeat(rig, kDown, 2);
    rig.settle(kFire);
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(3));
    // Esc from the drive choice goes back to the list on the same row.
    rig.settle(K1);
    rig.settle(kDown);
    const MenuReply again = rig.settle(kReturn);
    TEST_ASSERT_EQUAL_UINT(1, markedOf(again, rb::host::kMainDrives));  // a new choice starts on D1, not D3
    assert_marked(rig.settle(KESC), row(1), " B  DEMO-UNO.ATR");
}

// U: the cursor chooses the drive to empty.
void test_unmount_with_the_cursor()
{
    Rig rig;
    rig.start();
    rig.walk({K1, KA, K2});  // demo-dos.atr on D2
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(2));
    const MenuReply unmount = rig.settle(KU);
    assert_line(unmount, kMainMessage, " DESMONTAR: PULSA 1-8, ESC CANCELA");
    TEST_ASSERT_EQUAL_UINT(1, markedOf(unmount, rb::host::kMainDrives));
    // Esc cancels: nothing emptied, the cursor back on the sources.
    rig.settle(kDown);
    const MenuReply cancelled = rig.settle(KESC);
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(2));
    TEST_ASSERT_EQUAL_UINT(0, markedOf(cancelled, rb::host::kMainDrives));
    TEST_ASSERT_EQUAL_UINT(1, markedOf(cancelled, rb::host::kMainSources));
    rig.settle(KU);
    rig.settle(kDown);
    const MenuReply done = rig.settle(kReturn);
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
    TEST_ASSERT_EQUAL_UINT(0, markedOf(done, rb::host::kMainDrives));
    TEST_ASSERT_NOT_EQUAL(0, markedOf(done, rb::host::kMainSources));
}

// A source that is loading keeps its mark on the main screen; no drive is marked.
void test_a_source_loading_from_the_main_screen_keeps_its_mark()
{
    Rig rig;
    rig.start();
    rig.settle(kDown);
    rig.gated.gate(tnfs::cmd::kStat);
    const MenuReply loading = rig.press(kFire);
    assert_loading(loading, kMainMessage, "0");
    TEST_ASSERT_EQUAL_UINT(2, markedOf(loading, rb::host::kMainSources));
    TEST_ASSERT_EQUAL_UINT(0, markedOf(loading, rb::host::kMainDrives));
    TEST_ASSERT_EQUAL_UINT8(0, loading.screen()[rb::host::kMenuCursor + 2]);  // no local bar while loading (N007)
    rig.gated.awaitHeld();
    rig.press(KESC);
    rig.gated.open();
}

// --- N007: the bar moves on the Atari; only RETURN, fire and a page change ask the host ---

struct Block {
    unsigned base, rows, row, paged;
};

Block blockOf(const MenuReply& reply)
{
    const uint8_t* b = reply.screen() + rb::host::kMenuCursor;
    return {static_cast<unsigned>(b[0] | (b[1] << 8)), b[2], b[3], b[4]};
}

void assert_block(const MenuReply& reply, unsigned base, unsigned rows, unsigned row, unsigned paged)
{
    const Block b = blockOf(reply);
    TEST_ASSERT_EQUAL_UINT(base, b.base);
    TEST_ASSERT_EQUAL_UINT(rows, b.rows);
    TEST_ASSERT_EQUAL_UINT(row, b.row);
    TEST_ASSERT_EQUAL_UINT(paged, b.paged);
}

// The host tells the 6502 where the bar is and how far it may go on each screen.
void test_every_shown_screen_carries_its_cursor_block()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, 42);
    rig.start();
    assert_block(rig.settle(kRedraw), rb::host::kMainSources, 8, 0, 0);
    const MenuReply oms = openOms(rig);
    assert_block(oms, rb::host::kListEntries, 20, 0, 1);
    assert_block(repeat(rig, kRight, 10), rb::host::kListEntries, 5, 0, 1);  // page 11: 5 rows
    const MenuReply pick = rig.settle(kReturn);                               // G200.ATR
    assert_block(pick, rb::host::kMainDrives, 8, 0, 0);
    rig.settle(KESC);
    rig.settle(KESC);  // back to the root of source 2
    const MenuReply root = rig.settle(kRedraw);
    TEST_ASSERT_EQUAL_UINT(0, blockOf(root).paged);  // one page: the edges stay on the Atari
    // An empty folder has no bar; a load in progress neither.
    const MenuReply empty = rig.settle(KD);  // VACIA/
    assert_title(empty, " FUENTE 2: /VACIA");
    TEST_ASSERT_EQUAL_UINT(0, blockOf(empty).rows);
    rig.settle(KESC);
    rig.gated.gate(tnfs::cmd::kStat);
    const MenuReply loading = rig.press(KC);  // OMS_ATR/, the load waits
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, loading.action());
    TEST_ASSERT_EQUAL_UINT(0, blockOf(loading).rows);
    rig.gated.awaitHeld();
    rig.press(KESC);  // cancel
    rig.gated.open();
    // The local list too.
    Rig local;
    local.start();
    assert_block(local.settle(K1), rb::host::kListEntries, 3, 0, 0);  // demo-dos, demo-uno, rota
}

// Moving inside a page asks the host nothing; RETURN asks once, with the row.
void test_moves_inside_a_page_do_not_reach_the_host()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, 42);
    rig.start();
    openOms(rig);
    repeat(rig, kRight, 2);  // page 3
    const unsigned before = rig.hostCalls;
    const MenuReply down = repeat(rig, kDown, 19);
    assert_marked(down, row(19), "T G059.ATR");
    const MenuReply up = repeat(rig, kUp, 17);
    assert_marked(up, row(2), "C G042.ATR");
    TEST_ASSERT_EQUAL_UINT(before, rig.hostCalls);
    const MenuReply pick = rig.settle(kFire);
    TEST_ASSERT_EQUAL_UINT(before + 1, rig.hostCalls);
    assert_line(pick, kMainMessage, " UNIDAD PARA: G042.ATR");
    // The drive choice: moves on the Atari too.
    repeat(rig, kDown, 3);
    TEST_ASSERT_EQUAL_UINT(before + 1, rig.hostCalls);
    rig.settle(kReturn);
    TEST_ASSERT_EQUAL_STRING("G042.ATR", rig.controller->driveName(4));
}

// The edges: the host takes the row the bar was on, not the one it drew last.
void test_an_edge_move_starts_from_the_row_the_atari_shows()
{
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, 42);
    rig.start();
    openOms(rig);
    rig.settle(kRight);                       // page 2, the host draws row 0
    repeat(rig, kDown, 19);                   // the Atari: row 19, the host still on 0
    const MenuReply page3 = rig.settle(kDown);
    assert_line(page3, kListMessage, " PAGINA 3 DE 11 (205)");
    assert_marked(page3, row(0), "A G040.ATR");
    rig.settle(KF);                           // the host: row 5 (G045)
    rig.settle(KESC);                         // back from the drive choice, row 5 kept
    repeat(rig, kUp, 5);                      // the Atari: row 0, the host still on 5
    const MenuReply page2 = rig.settle(kUp);
    assert_line(page2, kListMessage, " PAGINA 2 DE 11 (205)");
    assert_marked(page2, row(19), "T G039.ATR");
    // Page 1 has no page above: up from the top row leaves the bar there, not on the
    // row the host drew last.
    repeat(rig, kLeft, 1);
    rig.settle(KF);
    rig.settle(KESC);
    repeat(rig, kUp, 5);
    const MenuReply top = rig.settle(kUp);
    assert_line(top, kListMessage, " PAGINA 1 DE 11 (205)");
    assert_marked(top, row(0), "A G000.ATR");
}

// The confirm code itself: row n of the screen shown; a row off the screen does nothing.
void test_a_confirmed_row_is_chosen_as_its_letter_and_off_rows_do_nothing()
{
    Rig byLetter;
    addImages(byLetter, "/OMS_ATR", "G%03d.ATR", 205, 42);
    byLetter.start();
    openOms(byLetter);
    byLetter.settle(kRight);
    const MenuReply letter = byLetter.settle(KC);
    Rig rig;
    addImages(rig, "/OMS_ATR", "G%03d.ATR", 205, 42);
    rig.start();
    openOms(rig);
    rig.settle(kRight);
    const MenuReply confirmed = rig.rawPress(static_cast<uint8_t>(rb::host::kMenuConfirmRow + 2));
    TEST_ASSERT_TRUE(sameScreen(letter, confirmed));
    rig.settle(KESC);
    const MenuReply last = repeat(rig, kRight, 9);  // page 11: 5 rows
    TEST_ASSERT_TRUE(sameScreen(last, rig.rawPress(static_cast<uint8_t>(rb::host::kMenuConfirmRow + 5))));
    TEST_ASSERT_TRUE(sameScreen(last, rig.rawPress(static_cast<uint8_t>(rb::host::kMenuConfirmRow + 19))));
    // The local list: 3 images, so row 3 is not on the screen.
    Rig local;
    local.start();
    const MenuReply images = local.settle(K1);
    TEST_ASSERT_TRUE(sameScreen(images, local.rawPress(static_cast<uint8_t>(rb::host::kMenuConfirmRow + 3))));
    // The main screen: row 2 is source 3, as '3'; row 8 is not a source.
    Rig main;
    main.start();
    const MenuReply top = main.settle(kRedraw);
    TEST_ASSERT_TRUE(sameScreen(top, main.rawPress(static_cast<uint8_t>(rb::host::kMenuConfirmRow + 8))));
    Rig three;
    three.start();
    TEST_ASSERT_TRUE(sameScreen(three.settle(K3), main.rawPress(static_cast<uint8_t>(rb::host::kMenuConfirmRow + 2))));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_205_entries_open_on_page_1_with_the_cursor_on_row_1);
    RUN_TEST(test_right_ten_times_reaches_page_11_and_stops);
    RUN_TEST(test_down_from_row_20_shows_page_2_on_its_first_row_and_up_comes_back);
    RUN_TEST(test_c_on_page_3_chooses_entry_43);
    RUN_TEST(test_return_on_a_marked_image_then_2_mounts_d2_as_the_letter_does);
    RUN_TEST(test_fire_on_a_marked_folder_enters_it);
    RUN_TEST(test_esc_from_a_subfolder_restores_page_and_row);
    RUN_TEST(test_entries_128_129_and_999_are_marked_chosen_and_mounted);
    RUN_TEST(test_a_plain_equals_moves_nothing);
    RUN_TEST(test_the_local_list_pages_and_mounts_an_entry_beyond_128);
    RUN_TEST(test_moves_and_return_do_nothing_while_loading);
    RUN_TEST(test_the_position_stack_keeps_32_levels);
    RUN_TEST(test_reopening_a_source_starts_on_row_1_and_the_main_screen_ignores_moves);
    RUN_TEST(test_a_full_local_list_shows_999_of_the_total);
    RUN_TEST(test_esc_to_a_parent_that_shrank_keeps_the_cursor_in_range);
    RUN_TEST(test_a_cancelled_or_failed_entry_keeps_the_position_and_the_history);
    RUN_TEST(test_down_twice_and_fire_open_source_3_as_3_does);
    RUN_TEST(test_the_main_cursor_covers_the_8_sources_and_return_on_an_empty_one);
    RUN_TEST(test_d2_chosen_with_the_cursor_mounts_as_the_letter_and_2);
    RUN_TEST(test_a_local_image_goes_to_the_drive_under_the_cursor);
    RUN_TEST(test_unmount_with_the_cursor);
    RUN_TEST(test_every_shown_screen_carries_its_cursor_block);
    RUN_TEST(test_moves_inside_a_page_do_not_reach_the_host);
    RUN_TEST(test_an_edge_move_starts_from_the_row_the_atari_shows);
    RUN_TEST(test_a_confirmed_row_is_chosen_as_its_letter_and_off_rows_do_nothing);
    RUN_TEST(test_a_source_loading_from_the_main_screen_keeps_its_mark);
    return UNITY_END();
}
