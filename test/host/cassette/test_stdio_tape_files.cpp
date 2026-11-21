// StdioTapeFiles and the discard outputs (cassette-cas C106): tapes read from files through
// stdio, three slots; outputs that drive no pin while the tape still moves with MOTOR. The
// real case mounts PAL Alien Ambush from RB_CAS_SAMPLES and plays it to the end.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "rb/cassette/cassette_commands.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/discard_outputs.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/cassette/stdio_tape_files.hpp"
#include "unity.h"

using rb::cassette::CasSegment;
using rb::cassette::CassetteCommands;
using rb::cassette::CassettePlayer;
using rb::cassette::DiscardByteOut;
using rb::cassette::DiscardWaveOut;
using rb::cassette::FskLoader;
using rb::cassette::LoaderStep;
using rb::cassette::MotorIn;
using rb::cassette::StdioTapeFiles;
using rb::cassette::TapeSource;
using rb::core::Span;

namespace {

const char* kFile = "rb_stdio_tape_test.bin";

void writeFile(const char* path, const std::vector<uint8_t>& bytes)
{
    std::FILE* f = std::fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_size_t(bytes.size(), std::fwrite(bytes.data(), 1, bytes.size(), f));
    std::fclose(f);
}

std::vector<uint8_t> sample()
{
    std::vector<uint8_t> bytes(1000);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(i * 7);
    }
    return bytes;
}

struct FakeMotor final : MotorIn {
    bool motor = false;
    bool on() const override { return motor; }
};

}  // namespace

void setUp() { writeFile(kFile, sample()); }
void tearDown() { std::remove(kFile); }

static void test_open_reads_size_and_bytes(void)
{
    StdioTapeFiles files("");
    TapeSource* tape = files.open(kFile);
    TEST_ASSERT_NOT_NULL(tape);
    TEST_ASSERT_EQUAL_UINT32(1000, tape->size());
    uint8_t buffer[10];
    TEST_ASSERT_TRUE(tape->read(500, Span<uint8_t>(buffer, sizeof buffer)));
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>((500 + i) * 7), buffer[i]);
    }
    TEST_ASSERT_TRUE(tape->read(990, Span<uint8_t>(buffer, sizeof buffer)));   // up to the end
    TEST_ASSERT_FALSE(tape->read(991, Span<uint8_t>(buffer, sizeof buffer)));  // past the end
    TEST_ASSERT_FALSE(tape->read(5000, Span<uint8_t>(buffer, 1)));
    TEST_ASSERT_TRUE(tape->read(1000, Span<uint8_t>()));  // nothing to read at the end
    files.close(tape);
    TEST_ASSERT_EQUAL_INT(0, files.openCount());
}

static void test_reads_after_seeking_back(void)
{
    StdioTapeFiles files("");
    TapeSource* tape = files.open(kFile);
    uint8_t a[4];
    uint8_t b[4];
    TEST_ASSERT_TRUE(tape->read(900, Span<uint8_t>(a, 4)));
    TEST_ASSERT_TRUE(tape->read(0, Span<uint8_t>(b, 4)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(900 * 7), a[0]);
    TEST_ASSERT_EQUAL_UINT8(0, b[0]);
    TEST_ASSERT_EQUAL_UINT8(21, b[3]);
    files.close(tape);
}

static void test_missing_file_or_long_path_is_null(void)
{
    StdioTapeFiles files("");
    TEST_ASSERT_NULL(files.open("no_such_tape.cas"));
    const std::string longPath(400, 'x');
    TEST_ASSERT_NULL(files.open(longPath.c_str()));
    TEST_ASSERT_NULL(files.open(nullptr));
    TEST_ASSERT_EQUAL_INT(0, files.openCount());
}

static void test_root_is_prefixed(void)
{
    StdioTapeFiles files("./");
    TapeSource* tape = files.open(kFile);
    TEST_ASSERT_NOT_NULL(tape);
    files.close(tape);
    StdioTapeFiles wrong("no_such_dir/");
    TEST_ASSERT_NULL(wrong.open(kFile));
}

static void test_three_slots(void)
{
    StdioTapeFiles files("");
    TapeSource* a = files.open(kFile);
    TapeSource* b = files.open(kFile);
    TapeSource* c = files.open(kFile);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(a != b && b != c && a != c);
    TEST_ASSERT_NULL(files.open(kFile));
    TEST_ASSERT_EQUAL_INT(3, files.openCount());
    files.close(b);
    TEST_ASSERT_EQUAL_INT(2, files.openCount());
    TapeSource* d = files.open(kFile);
    TEST_ASSERT_NOT_NULL(d);
    files.close(nullptr);  // harmless
    StdioTapeFiles other("");
    files.close(other.open(kFile));  // not ours: harmless
    TEST_ASSERT_EQUAL_INT(3, files.openCount());
}

static void test_destructor_closes_everything(void)
{
    {
        StdioTapeFiles files("");
        (void)files.open(kFile);
        (void)files.open(kFile);
    }
    TEST_ASSERT_EQUAL_INT(0, std::remove(kFile));  // Windows refuses to remove an open file
    writeFile(kFile, sample());
}

// Two tasks read one tape (player records, loader runs): every read gets its own bytes.
static void test_two_threads_read_one_tape(void)
{
    StdioTapeFiles files("");
    TapeSource* tape = files.open(kFile);
    TEST_ASSERT_NOT_NULL(tape);
    std::atomic<int> wrong{0};
    auto reader = [&](uint32_t offset) {
        uint8_t buffer[64];
        for (int i = 0; i < 20000; ++i) {
            if (!tape->read(offset, Span<uint8_t>(buffer, sizeof buffer))) {
                ++wrong;
                continue;
            }
            for (uint32_t k = 0; k < sizeof buffer; ++k) {
                if (buffer[k] != static_cast<uint8_t>((offset + k) * 7)) {
                    ++wrong;
                    break;
                }
            }
        }
    };
    std::thread a(reader, 0u);
    std::thread b(reader, 900u);
    a.join();
    b.join();
    TEST_ASSERT_EQUAL_INT(0, wrong.load());
    files.close(tape);
}

static void test_status_without_tape_shows_motor(void)
{
    StdioTapeFiles files("");
    std::vector<uint8_t> bank(1024);
    FskLoader loader;
    loader.bind(Span<uint8_t>(bank.data(), bank.size()));
    CassettePlayer player(loader);
    std::vector<CasSegment> a(4);
    std::vector<CasSegment> b(4);
    CassetteCommands cas(player, loader, files, Span<CasSegment>(a.data(), a.size()),
                         Span<CasSegment>(b.data(), b.size()));
    FakeMotor motor;
    DiscardWaveOut wave;
    char out[64];
    cas.run("status", motor, wave, Span<char>(out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("no tape, motor off", out);
    motor.motor = true;
    cas.run("status", motor, wave, Span<char>(out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("no tape, motor on", out);
}

// The real tape through stdio, played with MOTOR on into the discard outputs: every record
// byte and FSK portion reaches the outputs, and the tape ends.
static void test_real_alien_ambush_plays_into_discard_outputs(void)
{
    const char* samples = std::getenv("RB_CAS_SAMPLES");
    if (samples == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    const std::string root = std::string(samples) + "/";
    StdioTapeFiles files(root.c_str());
    static uint8_t bank[1u << 20];
    FskLoader loader;
    loader.bind(Span<uint8_t>(bank, sizeof bank));
    CassettePlayer player(loader);
    std::vector<CasSegment> a(256);
    std::vector<CasSegment> b(256);
    CassetteCommands cas(player, loader, files, Span<CasSegment>(a.data(), a.size()),
                         Span<CasSegment>(b.data(), b.size()));
    FakeMotor motor;
    motor.motor = true;
    DiscardByteOut bytes;
    DiscardWaveOut wave;
    char out[128];
    cas.run("mount PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas", motor, wave, Span<char>(out, sizeof out));
    if (std::strncmp(out, "cannot open", 11) == 0) {
        TEST_IGNORE_MESSAGE("PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas not found");
    }
    TEST_ASSERT_EQUAL_STRING("mounted PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas: 51 data, 49 fsk", out);
    uint64_t now = 0;
    for (int i = 0; i < 200000 && !player.atEnd(); ++i) {
        while (loader.step() == LoaderStep::Progress) {
        }
        player.tick(now, motor, bytes, wave);
        now += 100000;  // 100 ms
    }
    TEST_ASSERT_TRUE(player.atEnd());
    TEST_ASSERT_EQUAL_UINT32(8190, bytes.bytes());
    TEST_ASSERT_TRUE(wave.portions() > 0);
    cas.run("status", motor, wave, Span<char>(out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas: end, motor on", out);
    cas.run("eject", motor, wave, Span<char>(out, sizeof out));
    TEST_ASSERT_EQUAL_INT(0, files.openCount());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_reads_size_and_bytes);
    RUN_TEST(test_reads_after_seeking_back);
    RUN_TEST(test_missing_file_or_long_path_is_null);
    RUN_TEST(test_root_is_prefixed);
    RUN_TEST(test_three_slots);
    RUN_TEST(test_destructor_closes_everything);
    RUN_TEST(test_two_threads_read_one_tape);
    RUN_TEST(test_status_without_tape_shows_motor);
    RUN_TEST(test_real_alien_ambush_plays_into_discard_outputs);
    return UNITY_END();
}
