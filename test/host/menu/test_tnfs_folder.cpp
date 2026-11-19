// TnfsFolder (menu-tnfs T010, AC1, AC9, AC10): the listing of a TNFS folder
// filtered, ordered and cut to 20 entries, with the names in display form and
// the title cut on the left.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "tnfs_folder.hpp"
#include "unity.h"

using rb::host::TnfsFolder;
using rb::host::TnfsListing;
using rb::host::tnfsDisplay;
using rb::host::tnfsTitle;

namespace {

TnfsListing file(const char* name) { return TnfsListing{name, false}; }
TnfsListing dir(const char* name) { return TnfsListing{name, true}; }

std::string notice(const TnfsFolder& folder)
{
    char out[TnfsFolder::kNoticeSize];
    folder.notice(out);
    return out;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// /Atari_Chile/ATR in pulgarcito: 129 entries, 128 after the filter (AC9).
static void test_129_entries_keep_128_and_show_20(void)
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
    TEST_ASSERT_EQUAL_size_t(20, folder.shown());
    TEST_ASSERT_EQUAL_STRING("MOSTRANDO 20 DE 128", notice(folder).c_str());
    for (std::size_t i = 0; i < 20; ++i) {
        std::snprintf(name, sizeof name, "GAME%03u.ATR", static_cast<unsigned>(i));
        TEST_ASSERT_EQUAL_STRING(name, folder.entry(i).display);
        TEST_ASSERT_EQUAL_STRING((std::string("/Atari_Chile/ATR/") + name).c_str(), folder.entry(i).path.c_str());
        TEST_ASSERT_FALSE(folder.entry(i).isDir);
    }
}

static void test_folders_first_marked_with_a_slash_then_images_without_case(void)
{
    const TnfsFolder folder = TnfsFolder::build(
        "/", {file("zork.atr"), dir("juegos"), file("Alpha.ATR"), dir("Demos"), file("beta.Atr"), dir("arcade")});
    TEST_ASSERT_EQUAL_size_t(6, folder.total());
    TEST_ASSERT_EQUAL_size_t(6, folder.shown());
    const char* expected[] = {"ARCADE/", "DEMOS/", "JUEGOS/", "ALPHA.ATR", "BETA.ATR", "ZORK.ATR"};
    const char* paths[] = {"/arcade", "/Demos", "/juegos", "/Alpha.ATR", "/beta.Atr", "/zork.atr"};
    for (std::size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], folder.entry(i).display);
        TEST_ASSERT_EQUAL_STRING(paths[i], folder.entry(i).path.c_str());
        TEST_ASSERT_EQUAL(i < 3, folder.entry(i).isDir);
    }
    TEST_ASSERT_EQUAL_STRING("", notice(folder).c_str());
}

static void test_a_folder_is_kept_whatever_its_name(void)
{
    const TnfsFolder folder = TnfsFolder::build("/a", {dir("0- Prueba"), dir("ATR"), file("notas.txt")});
    TEST_ASSERT_EQUAL_size_t(2, folder.total());
    TEST_ASSERT_EQUAL_STRING("0- PRUEBA/", folder.entry(0).display);
    TEST_ASSERT_EQUAL_STRING("/a/0- Prueba", folder.entry(0).path.c_str());
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
    const TnfsFolder folder = TnfsFolder::build("/vacia", {file("leeme.txt"), file("a.xex"), dir(".x")});
    TEST_ASSERT_EQUAL_size_t(0, folder.total());
    TEST_ASSERT_EQUAL_size_t(0, folder.shown());
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
    TEST_ASSERT_EQUAL_STRING("/Aportes/Atari Tarots Espa\xC3\xB1ol5.atr", folder.entry(1).path.c_str());
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
    TEST_ASSERT_EQUAL_STRING(("/" + longName + ".atr").c_str(), folder.entry(1).path.c_str());
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
    RUN_TEST(test_129_entries_keep_128_and_show_20);
    RUN_TEST(test_folders_first_marked_with_a_slash_then_images_without_case);
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
