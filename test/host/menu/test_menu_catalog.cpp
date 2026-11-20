// MenuCatalog (menu-navegacion N002, AC10, M42-3): the local list keeps up to 999
// .atr names in case-insensitive order, apart from the 20 rows of a page.

#include <cstdio>
#include <cstring>

#include "menu_catalog.hpp"
#include "unity.h"

using rb::host::MenuCatalog;

void setUp(void) {}
void tearDown(void) {}

// menu-xex X003 (AC1): .xex files are listed with the .atr ones, in the same order.
static void test_xex_files_are_listed_with_the_atr_files(void)
{
    MenuCatalog catalog;
    TEST_ASSERT_TRUE(catalog.offer("juego.xex"));
    TEST_ASSERT_TRUE(catalog.offer("B.ATR"));
    TEST_ASSERT_TRUE(catalog.offer("A.XEX"));
    TEST_ASSERT_TRUE(catalog.offer("c.Xex"));
    TEST_ASSERT_FALSE(catalog.offer("x.xexx"));
    TEST_ASSERT_FALSE(catalog.offer(".xex"));
    TEST_ASSERT_FALSE(catalog.offer("d.com"));
    TEST_ASSERT_EQUAL_size_t(4, catalog.count());
    const char* expected[] = {"A.XEX", "B.ATR", "c.Xex", "juego.xex"};
    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], catalog.name(i));
    }
    TEST_ASSERT_TRUE(rb::host::hasXexExtension("JUEGO.XEX"));
    TEST_ASSERT_FALSE(rb::host::hasXexExtension("JUEGO.ATR"));
    TEST_ASSERT_TRUE(rb::host::hasImageExtension("a.atr"));
    TEST_ASSERT_TRUE(rb::host::hasImageExtension("a.xex"));
    TEST_ASSERT_FALSE(rb::host::hasImageExtension("xex"));
}

static void test_25_local_images_are_all_kept_in_order(void)
{
    MenuCatalog catalog;
    char name[32];
    for (int i = 24; i >= 0; --i) {
        std::snprintf(name, sizeof name, i % 2 ? "game%02d.ATR" : "GAME%02d.atr", i);
        TEST_ASSERT_TRUE(catalog.offer(name));
    }
    TEST_ASSERT_FALSE(catalog.offer("notas.txt"));
    TEST_ASSERT_EQUAL_size_t(25, catalog.count());
    TEST_ASSERT_EQUAL_size_t(25, catalog.atrSeen());
    for (int i = 0; i < 25; ++i) {
        std::snprintf(name, sizeof name, i % 2 ? "game%02d.ATR" : "GAME%02d.atr", i);
        TEST_ASSERT_EQUAL_STRING(name, catalog.name(static_cast<std::size_t>(i)));
    }
}

static void test_1200_local_images_keep_the_first_999(void)
{
    MenuCatalog catalog;
    char name[32];
    for (int i = 1199; i >= 0; --i) {
        std::snprintf(name, sizeof name, "G%04d.atr", i);
        catalog.offer(name);
    }
    TEST_ASSERT_EQUAL_size_t(999, MenuCatalog::kMaxEntries);
    TEST_ASSERT_EQUAL_size_t(999, catalog.count());
    TEST_ASSERT_EQUAL_size_t(1200, catalog.atrSeen());
    TEST_ASSERT_EQUAL_STRING("G0000.atr", catalog.name(0));
    TEST_ASSERT_EQUAL_STRING("G0998.atr", catalog.name(998));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_xex_files_are_listed_with_the_atr_files);
    RUN_TEST(test_25_local_images_are_all_kept_in_order);
    RUN_TEST(test_1200_local_images_keep_the_first_999);
    return UNITY_END();
}
