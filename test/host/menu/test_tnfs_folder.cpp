// TnfsFolder (menu-tnfs T010, AC1, AC9, AC10): the listing of a TNFS folder
// filtered, ordered and cut to 20 entries, with the names in display form and
// the title cut on the left.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "tnfs_folder.hpp"
#include "unity.h"

using rb::host::TnfsFolder;
using rb::host::TnfsListing;
using rb::host::tnfsDisplay;
using rb::host::tnfsTitle;

// Every allocation of this test program (menu-navegacion N002: swap() must not allocate).
static unsigned long g_allocations = 0;
static bool g_refuseNothrow = false;  // new (std::nothrow) answers nullptr

void* operator new(std::size_t size)
{
    ++g_allocations;
    void* p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    ++g_allocations;
    if (g_refuseNothrow) {
        return nullptr;
    }
    return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

TnfsListing file(const char* name) { return TnfsListing{name, false}; }
TnfsListing dir(const char* name) { return TnfsListing{name, true}; }

std::string pathOf(const TnfsFolder& folder, std::size_t index)
{
    char out[TnfsFolder::kMaxPath + 1];
    const std::size_t length = folder.path(index, out);
    TEST_ASSERT_EQUAL_size_t(std::strlen(out), length);
    return out;
}

// count images named G0000.ATR.. listed in reverse order.
std::vector<TnfsListing> images(int count)
{
    std::vector<TnfsListing> listed;
    char name[32];
    for (int i = count - 1; i >= 0; --i) {
        std::snprintf(name, sizeof name, "G%04d.ATR", i);
        listed.push_back(file(name));
    }
    return listed;
}

std::string notice(const TnfsFolder& folder)
{
    char out[TnfsFolder::kNoticeSize];
    folder.notice(0, out);
    return out;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// /Atari_Chile/ATR in pulgarcito: 129 entries, 128 after the filter (AC9).
static void test_129_entries_keep_128(void)
{
    std::vector<TnfsListing> listed;
    char name[32];
    for (int i = 0; i < 128; ++i) {
        std::snprintf(name, sizeof name, "GAME%03d.ATR", 127 - i);  // listed in reverse order
        listed.push_back(file(name));
    }
    listed.push_back(file("LEEME.TXT"));  // not an image: filtered out
    const TnfsFolder folder = TnfsFolder::build("/Atari_Chile/ATR", std::move(listed));
    TEST_ASSERT_EQUAL_size_t(128, folder.total());
    TEST_ASSERT_EQUAL_size_t(128, folder.count());
    TEST_ASSERT_EQUAL_STRING("PAGINA 1 DE 7 (128)", notice(folder).c_str());
    for (std::size_t i = 0; i < 128; ++i) {
        std::snprintf(name, sizeof name, "GAME%03u.ATR", static_cast<unsigned>(i));
        TEST_ASSERT_EQUAL_STRING(name, folder.entry(i).display);
        TEST_ASSERT_EQUAL_STRING((std::string("/Atari_Chile/ATR/") + name).c_str(), pathOf(folder, i).c_str());
        TEST_ASSERT_FALSE(folder.entry(i).isDir);
    }
}

// menu-navegacion N002 (AC2, AC10, M42-3): up to 999 entries are kept.
static void test_205_entries_are_all_kept(void)
{
    const TnfsFolder folder = TnfsFolder::build("/OMS_ATR", images(205));
    TEST_ASSERT_TRUE(folder.ready());
    TEST_ASSERT_EQUAL_size_t(205, folder.total());
    TEST_ASSERT_EQUAL_size_t(205, folder.count());
    TEST_ASSERT_EQUAL_STRING("G0204.ATR", folder.entry(204).display);
    TEST_ASSERT_EQUAL_STRING("/OMS_ATR/G0204.ATR", pathOf(folder, 204).c_str());
}

static void test_1200_entries_keep_the_first_999_and_the_total(void)
{
    const TnfsFolder folder = TnfsFolder::build("/", images(1200));
    TEST_ASSERT_EQUAL_size_t(1200, folder.total());
    TEST_ASSERT_EQUAL_size_t(TnfsFolder::kCapacity, folder.count());
    TEST_ASSERT_EQUAL_size_t(999, TnfsFolder::kCapacity);
    TEST_ASSERT_EQUAL_STRING("G0000.ATR", folder.entry(0).display);
    TEST_ASSERT_EQUAL_STRING("G0127.ATR", folder.entry(127).display);
    TEST_ASSERT_EQUAL_STRING("G0128.ATR", folder.entry(128).display);
    TEST_ASSERT_EQUAL_STRING("G0998.ATR", folder.entry(998).display);
    TEST_ASSERT_EQUAL_STRING("/G0998.ATR", pathOf(folder, 998).c_str());
    TEST_ASSERT_EQUAL_STRING("PAGINA 1 DE 50 (999 DE 1200)", notice(folder).c_str());
}

// The loader hands a listing over by exchanging the storage (plan §8 point 3): the
// same cost with 20 entries as with 999, no allocation, no entry visited.
static void test_swap_exchanges_the_storage_without_allocating_or_walking_it(void)
{
    TnfsFolder big = TnfsFolder::build("/a", images(999));
    TnfsFolder small = TnfsFolder::build("/b", images(20));
    const TnfsFolder::Entry* bigFirst = &big.entry(0);
    const TnfsFolder::Entry* smallFirst = &small.entry(0);
    const unsigned long before = g_allocations;
    big.swap(small);
    TEST_ASSERT_EQUAL_UINT32(0, g_allocations - before);
    TEST_ASSERT_EQUAL_PTR(smallFirst, &big.entry(0));
    TEST_ASSERT_EQUAL_PTR(bigFirst, &small.entry(0));
    TEST_ASSERT_EQUAL_size_t(20, big.count());
    TEST_ASSERT_EQUAL_size_t(999, small.count());
    TEST_ASSERT_EQUAL_STRING("/b/G0019.ATR", pathOf(big, 19).c_str());
    TEST_ASSERT_EQUAL_STRING("/a/G0998.ATR", pathOf(small, 998).c_str());
}

// The loader never offers one (it could not be opened); build() keeps the same rule.
static void test_a_path_longer_than_tnfs_allows_is_not_kept(void)
{
    const std::string fits(TnfsFolder::kMaxPath - 5 - 4, 'f');  // "/abc/" + name + ".atr" = 255
    const std::string over(TnfsFolder::kMaxPath - 5 - 3, 'o');  // 256
    const TnfsFolder folder = TnfsFolder::build("/abc", {file((fits + ".atr").c_str()), file((over + ".atr").c_str())});
    TEST_ASSERT_EQUAL_size_t(1, folder.total());
    TEST_ASSERT_EQUAL_size_t(TnfsFolder::kMaxPath, pathOf(folder, 0).size());
}

static void test_without_memory_the_folder_is_not_ready_and_keeps_nothing(void)
{
    g_refuseNothrow = true;
    TnfsFolder folder;
    g_refuseNothrow = false;
    TEST_ASSERT_FALSE(folder.ready());
    TEST_ASSERT_FALSE(folder.fill("/", images(3)));
    TEST_ASSERT_EQUAL_size_t(0, folder.total());
    TEST_ASSERT_EQUAL_size_t(0, folder.count());
    TEST_ASSERT_EQUAL_STRING("CARPETA VACIA", notice(folder).c_str());
    folder.clear();  // no block: nothing to forget, no crash
}

// menu-navegacion N003: 20 entries need no page line; 21, 40 and 41 count pages right.
static void test_page_line_at_the_edges_of_a_page(void)
{
    char out[TnfsFolder::kNoticeSize];
    rb::host::pageLine(20, 20, 0, out);
    TEST_ASSERT_EQUAL_STRING("", out);
    rb::host::pageLine(21, 21, 0, out);
    TEST_ASSERT_EQUAL_STRING("PAGINA 1 DE 2 (21)", out);
    rb::host::pageLine(40, 40, 1, out);
    TEST_ASSERT_EQUAL_STRING("PAGINA 2 DE 2 (40)", out);
    rb::host::pageLine(41, 41, 2, out);
    TEST_ASSERT_EQUAL_STRING("PAGINA 3 DE 3 (41)", out);
    rb::host::pageLine(999, 1200, 49, out);
    TEST_ASSERT_EQUAL_STRING("PAGINA 50 DE 50 (999 DE 1200)", out);
}

static void test_folders_first_marked_with_a_slash_then_images_without_case(void)
{
    const TnfsFolder folder = TnfsFolder::build(
        "/", {file("zork.atr"), dir("juegos"), file("Alpha.ATR"), dir("Demos"), file("beta.Atr"), dir("arcade")});
    TEST_ASSERT_EQUAL_size_t(6, folder.total());
    TEST_ASSERT_EQUAL_size_t(6, folder.count());
    const char* expected[] = {"ARCADE/", "DEMOS/", "JUEGOS/", "ALPHA.ATR", "BETA.ATR", "ZORK.ATR"};
    const char* paths[] = {"/arcade", "/Demos", "/juegos", "/Alpha.ATR", "/beta.Atr", "/zork.atr"};
    for (std::size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], folder.entry(i).display);
        TEST_ASSERT_EQUAL_STRING(paths[i], pathOf(folder, i).c_str());
        TEST_ASSERT_EQUAL(i < 3, folder.entry(i).isDir);
    }
    TEST_ASSERT_EQUAL_STRING("", notice(folder).c_str());
}

// menu-xex X003 (AC2): a TNFS folder lists its .xex files with the .atr ones.
static void test_xex_files_are_listed_with_the_atr_files(void)
{
    const TnfsFolder folder =
        TnfsFolder::build("/j", {file("zeta.xex"), file("Alfa.ATR"), dir("sub"), file("beta.XEX"), file("x.xexx")});
    TEST_ASSERT_EQUAL_size_t(4, folder.total());
    const char* expected[] = {"SUB/", "ALFA.ATR", "BETA.XEX", "ZETA.XEX"};
    const char* paths[] = {"/j/sub", "/j/Alfa.ATR", "/j/beta.XEX", "/j/zeta.xex"};
    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], folder.entry(i).display);
        TEST_ASSERT_EQUAL_STRING(paths[i], pathOf(folder, i).c_str());
    }
}

static void test_a_folder_is_kept_whatever_its_name(void)
{
    const TnfsFolder folder = TnfsFolder::build("/a", {dir("0- Prueba"), dir("ATR"), file("notas.txt")});
    TEST_ASSERT_EQUAL_size_t(2, folder.total());
    TEST_ASSERT_EQUAL_STRING("0- PRUEBA/", folder.entry(0).display);
    TEST_ASSERT_EQUAL_STRING("/a/0- Prueba", pathOf(folder, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("ATR/", folder.entry(1).display);
}

static void test_no_entry_starting_with_a_dot(void)
{
    const TnfsFolder folder =
        TnfsFolder::build("/", {dir("."), dir(".."), dir(".git"), file(".oculto.atr"), file("ok.atr")});
    TEST_ASSERT_EQUAL_size_t(1, folder.total());
    TEST_ASSERT_EQUAL_STRING("OK.ATR", folder.entry(0).display);
}

static void test_a_folder_without_folders_or_images_says_carpeta_vacia(void)
{
    const TnfsFolder folder = TnfsFolder::build("/vacia", {file("leeme.txt"), file("a.com"), dir(".x")});
    TEST_ASSERT_EQUAL_size_t(0, folder.total());
    TEST_ASSERT_EQUAL_size_t(0, folder.count());
    TEST_ASSERT_EQUAL_STRING("CARPETA VACIA", notice(folder).c_str());
    TEST_ASSERT_EQUAL_STRING("CARPETA VACIA", notice(TnfsFolder::build("/", {})).c_str());
}

static void test_exactly_20_entries_show_no_count(void)
{
    std::vector<TnfsListing> listed;
    char name[16];
    for (int i = 0; i < 20; ++i) {
        std::snprintf(name, sizeof name, "J%02d.ATR", i);
        listed.push_back(file(name));
    }
    const TnfsFolder folder = TnfsFolder::build("/", std::move(listed));
    TEST_ASSERT_EQUAL_size_t(20, folder.total());
    TEST_ASSERT_EQUAL_STRING("", notice(folder).c_str());
}

// Names with characters that cannot be shown are replaced, and still open by
// their original bytes (AC10).
static void test_names_that_cannot_be_shown_are_replaced(void)
{
    const TnfsFolder folder = TnfsFolder::build(
        "/Aportes", {file("Atari Tarots Espa\xC3\xB1ol5.atr"), file("tab\there.atr"), file("x\xFF" "y.atr"),
                     file("Atari Logo.atr"), file("caf\xE2\x82\xAC.atr")});
    TEST_ASSERT_EQUAL_size_t(5, folder.total());
    TEST_ASSERT_EQUAL_STRING("ATARI LOGO.ATR", folder.entry(0).display);
    TEST_ASSERT_EQUAL_STRING("ATARI TAROTS ESPA?OL5.ATR", folder.entry(1).display);
    TEST_ASSERT_EQUAL_STRING("/Aportes/Atari Tarots Espa\xC3\xB1ol5.atr", pathOf(folder, 1).c_str());
    TEST_ASSERT_EQUAL_STRING("CAF?.ATR", folder.entry(2).display);  // three bytes, one '?'
    TEST_ASSERT_EQUAL_STRING("TAB?HERE.ATR", folder.entry(3).display);
    TEST_ASSERT_EQUAL_STRING("X?Y.ATR", folder.entry(4).display);  // a stray byte, one '?'
}

static void test_a_long_name_is_cut_and_a_folder_keeps_its_slash(void)
{
    const std::string longName(50, 'n');
    const TnfsFolder folder = TnfsFolder::build("/", {dir(longName.c_str()), file((longName + ".atr").c_str())});
    TEST_ASSERT_EQUAL_size_t(2, folder.total());
    const std::string shownDir = std::string(TnfsFolder::kDisplayWidth - 1, 'N') + "/";
    TEST_ASSERT_EQUAL_STRING(shownDir.c_str(), folder.entry(0).display);
    TEST_ASSERT_EQUAL_STRING(std::string(TnfsFolder::kDisplayWidth, 'N').c_str(), folder.entry(1).display);
    TEST_ASSERT_EQUAL_STRING(("/" + longName + ".atr").c_str(), pathOf(folder, 1).c_str());
}

static void test_display_counts_a_utf8_character_as_one(void)
{
    char out[8];
    const char text[] = "\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1";  // eight "ñ"
    TEST_ASSERT_EQUAL_size_t(7, tnfsDisplay(text, sizeof text - 1, out, 7));
    TEST_ASSERT_EQUAL_STRING("???????", out);
    TEST_ASSERT_EQUAL_size_t(2, tnfsDisplay("ab", 2, out, 7));
    TEST_ASSERT_EQUAL_STRING("AB", out);
}

static void test_title_is_cut_on_the_left(void)
{
    char out[21];
    tnfsTitle("/", 20, out);
    TEST_ASSERT_EQUAL_STRING("/", out);
    tnfsTitle("/Aportes_Juan_Larrain/0- Prueba", 20, out);  // 31 characters
    TEST_ASSERT_EQUAL_STRING(".._LARRAIN/0- PRUEBA", out);
    tnfsTitle("/12345678901234567/x", 20, out);  // exactly 20: not cut
    TEST_ASSERT_EQUAL_STRING("/12345678901234567/X", out);
    tnfsTitle("/Juegos/Espa\xC3\xB1ol/Aventuras", 20, out);  // 25 characters: "ñ" counts as one
    TEST_ASSERT_EQUAL_STRING("../ESPA?OL/AVENTURAS", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_129_entries_keep_128);
    RUN_TEST(test_205_entries_are_all_kept);
    RUN_TEST(test_1200_entries_keep_the_first_999_and_the_total);
    RUN_TEST(test_swap_exchanges_the_storage_without_allocating_or_walking_it);
    RUN_TEST(test_a_path_longer_than_tnfs_allows_is_not_kept);
    RUN_TEST(test_without_memory_the_folder_is_not_ready_and_keeps_nothing);
    RUN_TEST(test_page_line_at_the_edges_of_a_page);
    RUN_TEST(test_folders_first_marked_with_a_slash_then_images_without_case);
    RUN_TEST(test_xex_files_are_listed_with_the_atr_files);
    RUN_TEST(test_a_folder_is_kept_whatever_its_name);
    RUN_TEST(test_no_entry_starting_with_a_dot);
    RUN_TEST(test_a_folder_without_folders_or_images_says_carpeta_vacia);
    RUN_TEST(test_exactly_20_entries_show_no_count);
    RUN_TEST(test_names_that_cannot_be_shown_are_replaced);
    RUN_TEST(test_a_long_name_is_cut_and_a_folder_keeps_its_slash);
    RUN_TEST(test_display_counts_a_utf8_character_as_one);
    RUN_TEST(test_title_is_cut_on_the_left);
    return UNITY_END();
}
