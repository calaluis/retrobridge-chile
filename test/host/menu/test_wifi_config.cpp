// retrobridge-wifi.txt (menu-sio-wifi-tnfs plan §3 WifiConfig, C7, C9; T5 review): the two
// lines, their limits, nothing after line 2, and the key never in a reason.

#include <cstdio>
#include <cstring>
#include <string>

#include "rb/menu/wifi_config.hpp"
#include "unity.h"

using rb::host::WifiConfig;
using Result = WifiConfig::Result;

namespace {

constexpr const char* kFile = RB_TEST_WIFI_DIR "/wifi-config-test.txt";
constexpr const char* kSentinel = "CENTINELA-clave-9x7";

Result readText(WifiConfig& wifi, const std::string& text)
{
    std::FILE* out = std::fopen(kFile, "wb");
    TEST_ASSERT_NOT_NULL(out);
    std::fwrite(text.data(), 1, text.size(), out);
    std::fclose(out);
    std::FILE* in = std::fopen(kFile, "rb");
    TEST_ASSERT_NOT_NULL(in);
    const Result result = wifi.read(in);
    std::fclose(in);
    std::remove(kFile);
    return result;
}

void assertEmpty(const WifiConfig& wifi)
{
    TEST_ASSERT_EQUAL_UINT(0, wifi.nameLength());
    TEST_ASSERT_EQUAL_UINT(0, wifi.keyLength());
    TEST_ASSERT_EQUAL_STRING("", wifi.name());
    TEST_ASSERT_EQUAL_STRING("", wifi.key());
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_two_lines_give_the_name_and_the_key_with_their_spaces()
{
    WifiConfig wifi;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, "Mi Red 2.4G\r\n clave con espacios \r\n")));
    TEST_ASSERT_EQUAL_STRING("Mi Red 2.4G", wifi.name());
    TEST_ASSERT_EQUAL_STRING(" clave con espacios ", wifi.key());
    TEST_ASSERT_TRUE(wifi.hasKey());
    wifi.wipe();
    assertEmpty(wifi);
}

void test_an_open_network_has_one_line_or_an_empty_second_line()
{
    WifiConfig wifi;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, "abierta")));
    TEST_ASSERT_FALSE(wifi.hasKey());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, "abierta\n\n")));
    TEST_ASSERT_EQUAL_STRING("abierta", wifi.name());
    TEST_ASSERT_FALSE(wifi.hasKey());
}

void test_the_limits_of_the_name_and_the_key()
{
    WifiConfig wifi;
    const std::string name32(32, 'n');
    const std::string key8(8, 'k');
    const std::string key63(63, 'k');
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, name32 + "\n" + key8 + "\n")));
    TEST_ASSERT_EQUAL_UINT(32, wifi.nameLength());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, "red\n" + key63)));
    TEST_ASSERT_EQUAL_UINT(63, wifi.keyLength());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BadName), static_cast<int>(readText(wifi, name32 + "n\n" + key8)));
    assertEmpty(wifi);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BadName), static_cast<int>(readText(wifi, "\r\n" + key8)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BadName), static_cast<int>(readText(wifi, "")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BadKey), static_cast<int>(readText(wifi, "red\n1234567\n")));
    assertEmpty(wifi);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BadKey), static_cast<int>(readText(wifi, "red\n" + key63 + "k\n")));
    assertEmpty(wifi);
}

// T5 review: a file saved on Windows (CR LF) takes the same limits as one with LF.
void test_crlf_lines_take_the_full_limits_and_a_cr_inside_is_text()
{
    WifiConfig wifi;
    const std::string name32(32, 'n');
    const std::string key63(63, 'k');
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, name32 + "\r\n" + key63 + "\r\n")));
    TEST_ASSERT_EQUAL_UINT(32, wifi.nameLength());
    TEST_ASSERT_EQUAL_UINT(63, wifi.keyLength());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, name32 + "\r\n" + key63 + "\r")));
    TEST_ASSERT_EQUAL_UINT(63, wifi.keyLength());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BadName), static_cast<int>(readText(wifi, name32 + "n\r\n" + key63)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::BadKey), static_cast<int>(readText(wifi, "red\r\n" + key63 + "k\r\n")));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok), static_cast<int>(readText(wifi, "a\rb\r\nclave1234\r\n")));
    TEST_ASSERT_EQUAL_STRING("a\rb", wifi.name());
}

void test_a_third_line_makes_the_file_not_valid_and_keeps_nothing()
{
    WifiConfig wifi;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::ExtraLines),
                          static_cast<int>(readText(wifi, std::string("red\n") + kSentinel + "\notra cosa\n")));
    assertEmpty(wifi);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::ExtraLines),
                          static_cast<int>(readText(wifi, std::string("red\n") + kSentinel + "\n\n#")));
    // Line ends alone after line 2 are fine.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Result::Ok),
                          static_cast<int>(readText(wifi, std::string("red\n") + kSentinel + "\r\n\r\n\n")));
    TEST_ASSERT_EQUAL_STRING(kSentinel, wifi.key());
}

void test_no_reason_holds_the_key()
{
    const Result all[] = {Result::Ok, Result::BadName, Result::BadKey, Result::ExtraLines, Result::ReadError};
    for (Result result : all) {
        const char* reason = WifiConfig::reason(result);
        TEST_ASSERT_NOT_NULL(reason);
        TEST_ASSERT_NULL(std::strstr(reason, kSentinel));
        TEST_ASSERT_NULL(std::strstr(reason, "CENTINELA"));
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_two_lines_give_the_name_and_the_key_with_their_spaces);
    RUN_TEST(test_an_open_network_has_one_line_or_an_empty_second_line);
    RUN_TEST(test_the_limits_of_the_name_and_the_key);
    RUN_TEST(test_crlf_lines_take_the_full_limits_and_a_cr_inside_is_text);
    RUN_TEST(test_a_third_line_makes_the_file_not_valid_and_keeps_nothing);
    RUN_TEST(test_no_reason_holds_the_key);
    return UNITY_END();
}
