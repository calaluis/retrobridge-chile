// Creating a blank ATR from the menu (menu-formatear-disco, F003): the name rule
// (M46-3), the file (16-byte header + zeroed sectors, exclusive open) and its
// place in the local list (MenuCatalog::insert).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "rb/menu/create_atr.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "unity.h"

using rb::domain::Density;
using rb::host::createAtr;
using rb::host::CreateAtrResult;
using rb::host::isNewAtrName;
using rb::host::MenuCatalog;

namespace {

const char* const kDir = RB_TEST_CREATE_DIR;

std::string path_of(const char* name)
{
    return std::string(kDir) + "/" + name;
}

std::vector<uint8_t> file_bytes(const std::string& path)
{
    std::vector<uint8_t> bytes;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return bytes;
    }
    int c = 0;
    while ((c = std::fgetc(file)) != EOF) {
        bytes.push_back(static_cast<uint8_t>(c));
    }
    std::fclose(file);
    return bytes;
}

std::size_t files_in_dir()
{
    std::error_code error;
    std::size_t count = 0;
    for (std::filesystem::directory_iterator it{kDir, error}, end; !error && it != end; it.increment(error)) {
        ++count;
    }
    return count;
}

void assert_blank_atr(const std::vector<uint8_t>& bytes, uint32_t sectors)
{
    const uint32_t data = sectors * 128u;
    const uint32_t paragraphs = data / 16u;
    TEST_ASSERT_EQUAL_size_t(16u + data, bytes.size());
    TEST_ASSERT_EQUAL_HEX8(0x96, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(paragraphs & 0xFF, bytes[2]);
    TEST_ASSERT_EQUAL_HEX8((paragraphs >> 8) & 0xFF, bytes[3]);
    TEST_ASSERT_EQUAL_HEX8(0x80, bytes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, bytes[5]);
    TEST_ASSERT_EQUAL_HEX8((paragraphs >> 16) & 0xFF, bytes[6]);
    for (std::size_t i = 7; i < bytes.size(); ++i) {
        TEST_ASSERT_EQUAL_HEX8(0x00, bytes[i]);
    }
}

}  // namespace

void setUp(void)
{
    std::error_code error;
    std::filesystem::remove_all(kDir, error);
    std::filesystem::create_directories(kDir, error);
}

void tearDown(void) {}

static void test_name_rule(void)
{
    TEST_ASSERT_TRUE(isNewAtrName("NUEVO"));
    TEST_ASSERT_TRUE(isNewAtrName("A"));
    TEST_ASSERT_TRUE(isNewAtrName("JUEGO-2_B9"));
    TEST_ASSERT_TRUE(isNewAtrName("ABCDEFGHIJKLMNOPQRSTUVWXY"));  // 25
    TEST_ASSERT_FALSE(isNewAtrName(""));
    TEST_ASSERT_FALSE(isNewAtrName("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));  // 26
    TEST_ASSERT_FALSE(isNewAtrName("NUE VO"));
    TEST_ASSERT_FALSE(isNewAtrName("NUEVO.ATR"));
    TEST_ASSERT_FALSE(isNewAtrName("..\\X"));
    TEST_ASSERT_FALSE(isNewAtrName("A/B"));
    TEST_ASSERT_FALSE(isNewAtrName("C:X"));
    TEST_ASSERT_FALSE(isNewAtrName("nuevo"));  // lower case is outside A-Z
    TEST_ASSERT_FALSE(isNewAtrName(nullptr));
    // Windows device names would open the device, not a file.
    TEST_ASSERT_FALSE(isNewAtrName("CON"));
    TEST_ASSERT_FALSE(isNewAtrName("PRN"));
    TEST_ASSERT_FALSE(isNewAtrName("AUX"));
    TEST_ASSERT_FALSE(isNewAtrName("NUL"));
    TEST_ASSERT_FALSE(isNewAtrName("COM1"));
    TEST_ASSERT_FALSE(isNewAtrName("LPT9"));
    TEST_ASSERT_FALSE(isNewAtrName("COM0"));
    TEST_ASSERT_TRUE(isNewAtrName("CONSOLA"));
    TEST_ASSERT_TRUE(isNewAtrName("COM10"));
    TEST_ASSERT_TRUE(isNewAtrName("NULO"));
}

static void test_creates_a_single_density_atr(void)
{
    TEST_ASSERT_TRUE(createAtr(kDir, "NUEVO", Density::Single) == CreateAtrResult::Created);
    assert_blank_atr(file_bytes(path_of("NUEVO.ATR")), 720);
}

static void test_creates_an_enhanced_density_atr(void)
{
    TEST_ASSERT_TRUE(createAtr(kDir, "MEDIA", Density::Enhanced) == CreateAtrResult::Created);
    assert_blank_atr(file_bytes(path_of("MEDIA.ATR")), 1040);
}

static void test_an_existing_file_is_never_overwritten(void)
{
    std::FILE* file = std::fopen(path_of("NUEVO.ATR").c_str(), "wb");
    TEST_ASSERT_NOT_NULL(file);
    std::fputs("no tocar", file);
    std::fclose(file);
    TEST_ASSERT_TRUE(createAtr(kDir, "NUEVO", Density::Single) == CreateAtrResult::AlreadyExists);
    const std::vector<uint8_t> bytes = file_bytes(path_of("NUEVO.ATR"));
    TEST_ASSERT_EQUAL_size_t(8, bytes.size());
    TEST_ASSERT_EQUAL_MEMORY("no tocar", bytes.data(), 8);
    TEST_ASSERT_EQUAL_size_t(1, files_in_dir());
}

static void test_invalid_name_or_density_creates_nothing(void)
{
    TEST_ASSERT_TRUE(createAtr(kDir, "", Density::Single) == CreateAtrResult::InvalidName);
    TEST_ASSERT_TRUE(createAtr(kDir, "..\\FUERA", Density::Single) == CreateAtrResult::InvalidName);
    TEST_ASSERT_TRUE(createAtr(kDir, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", Density::Single) == CreateAtrResult::InvalidName);
    TEST_ASSERT_TRUE(createAtr(kDir, "NUL", Density::Single) == CreateAtrResult::InvalidName);
    TEST_ASSERT_TRUE(createAtr(kDir, "DOBLE", Density::Double) == CreateAtrResult::InvalidDensity);
    TEST_ASSERT_EQUAL_size_t(0, files_in_dir());
}

static void test_missing_folder_is_an_io_error(void)
{
    const std::string missing = path_of("no-existe");
    TEST_ASSERT_TRUE(createAtr(missing.c_str(), "NUEVO", Density::Single) == CreateAtrResult::IoError);
    TEST_ASSERT_EQUAL_size_t(0, files_in_dir());
}

static void test_insert_returns_the_position_in_order(void)
{
    MenuCatalog catalog;
    TEST_ASSERT_TRUE(catalog.offer("ALFA.ATR"));
    TEST_ASSERT_TRUE(catalog.offer("ZULU.ATR"));
    TEST_ASSERT_EQUAL_size_t(1, catalog.insert("NUEVO.ATR"));
    TEST_ASSERT_EQUAL_size_t(3, catalog.count());
    TEST_ASSERT_EQUAL_STRING("ALFA.ATR", catalog.name(0));
    TEST_ASSERT_EQUAL_STRING("NUEVO.ATR", catalog.name(1));
    TEST_ASSERT_EQUAL_STRING("ZULU.ATR", catalog.name(2));
    TEST_ASSERT_EQUAL_size_t(0, catalog.insert("AAA.ATR"));
    TEST_ASSERT_EQUAL_size_t(4, catalog.insert("ZZZ.ATR"));
}

static void test_insert_refuses_a_duplicate_a_bad_name_and_a_full_list(void)
{
    MenuCatalog catalog;
    TEST_ASSERT_TRUE(catalog.offer("NUEVO.ATR"));
    TEST_ASSERT_EQUAL_size_t(MenuCatalog::kNotInserted, catalog.insert("NUEVO.ATR"));
    TEST_ASSERT_EQUAL_size_t(MenuCatalog::kNotInserted, catalog.insert("notas.txt"));
    TEST_ASSERT_EQUAL_size_t(1, catalog.count());

    MenuCatalog full;
    char name[32];
    for (int i = 0; i < 999; ++i) {
        std::snprintf(name, sizeof name, "G%04d.ATR", i);
        TEST_ASSERT_TRUE(full.offer(name));
    }
    TEST_ASSERT_TRUE(full.full());
    TEST_ASSERT_EQUAL_size_t(MenuCatalog::kNotInserted, full.insert("A.ATR"));  // even before every entry
    TEST_ASSERT_EQUAL_STRING("G0000.ATR", full.name(0));
    TEST_ASSERT_EQUAL_size_t(999, full.count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_name_rule);
    RUN_TEST(test_creates_a_single_density_atr);
    RUN_TEST(test_creates_an_enhanced_density_atr);
    RUN_TEST(test_an_existing_file_is_never_overwritten);
    RUN_TEST(test_invalid_name_or_density_creates_nothing);
    RUN_TEST(test_missing_folder_is_an_io_error);
    RUN_TEST(test_insert_returns_the_position_in_order);
    RUN_TEST(test_insert_refuses_a_duplicate_a_bad_name_and_a_full_list);
    return UNITY_END();
}
