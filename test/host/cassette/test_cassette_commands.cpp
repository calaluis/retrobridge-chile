// CassetteCommands (spec cassette-cas, C105, AC6, AC8): `cas mount|status|eject|rewind`.
// A bad file leaves the mounted tape as it was; a tape that leaves is closed only once the
// loader's producer has stopped; no allocation. The real case mounts PAL Alien Ambush from
// RB_CAS_SAMPLES.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/cassette_commands.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "unity.h"

using rb::cassette::ByteOut;
using rb::cassette::CasSegment;
using rb::cassette::CassetteCommands;
using rb::cassette::CassettePlayer;
using rb::cassette::FskLoader;
using rb::cassette::FskPortion;
using rb::cassette::LoaderState;
using rb::cassette::MemoryTapeSource;
using rb::cassette::MotorIn;
using rb::cassette::PlayerPhase;
using rb::cassette::TapeFiles;
using rb::cassette::TapeSource;
using rb::cassette::WaveOut;
using rb::core::Span;

// ---- allocation counter (same mechanism as test_cassette_player) ----
namespace {
unsigned long g_allocations = 0;
void* counted(std::size_t size) noexcept
{
    ++g_allocations;
    return std::malloc(size == 0 ? 1 : size);
}
}  // namespace

void* operator new(std::size_t size)
{
    void* p = counted(size);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

using Bytes = std::vector<uint8_t>;

Bytes chunk(const char* type, uint16_t aux, const Bytes& data)
{
    Bytes out(type, type + 4);
    out.push_back(static_cast<uint8_t>(data.size() & 0xFF));
    out.push_back(static_cast<uint8_t>(data.size() >> 8));
    out.push_back(static_cast<uint8_t>(aux & 0xFF));
    out.push_back(static_cast<uint8_t>(aux >> 8));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Bytes operator+(Bytes a, const Bytes& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Bytes fuji() { return chunk("FUJI", 0, {}); }

Bytes repeat(std::size_t count, uint16_t units)
{
    Bytes out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<uint8_t>(units & 0xFF));
        out.push_back(static_cast<uint8_t>(units >> 8));
    }
    return out;
}

// One record and one FSK run of two chunks: "1 data, 2 fsk".
Bytes tapeA()
{
    return fuji() + chunk("baud", 600, {}) + chunk("data", 250, {0x55, 0x55, 0xFC}) +
           chunk("fsk ", 100, repeat(4, 300)) + chunk("fsk ", 0, repeat(4, 300));
}

// Two records.
Bytes tapeB() { return fuji() + chunk("baud", 600, {}) + chunk("data", 0, {1}) + chunk("data", 0, {2}); }

// A long FSK run: the producer needs several steps.
Bytes tapeFsk() { return fuji() + chunk("fsk ", 0, repeat(3000, 300)); }

struct FakeFiles final : TapeFiles {
    std::map<std::string, Bytes> files;
    std::vector<std::unique_ptr<MemoryTapeSource>> open_;  // reserved before the test runs
    std::vector<const TapeSource*> closed;
    int openNow = 0;
    int maxOpen = 0;

    FakeFiles()
    {
        open_.reserve(16);
        closed.reserve(16);
    }

    TapeSource* open(const char* path) override
    {
        auto it = files.find(path);
        if (it == files.end()) {
            return nullptr;
        }
        open_.emplace_back(new MemoryTapeSource(Span<const uint8_t>(it->second.data(), it->second.size())));
        ++openNow;
        maxOpen = openNow > maxOpen ? openNow : maxOpen;
        return open_.back().get();
    }

    void close(TapeSource* source) override
    {
        closed.push_back(source);
        --openNow;
    }
};

struct FakeMotor final : MotorIn {
    bool motor = true;
    bool on() const override { return motor; }
};

struct FakeBytes final : ByteOut {
    std::vector<uint8_t> sent;
    bool setBaud(uint32_t) override { return true; }
    bool send(Span<const uint8_t> bytes) override
    {
        sent.insert(sent.end(), bytes.data(), bytes.data() + bytes.size());
        return true;
    }
    bool drained() const override { return true; }
};

struct FakeWave final : WaveOut {
    uint32_t stops = 0;
    bool push(const FskPortion&) override { return true; }
    bool idle() const override { return true; }
    void stop() override { ++stops; }
};

struct Rig {
    FakeFiles files;
    FakeMotor motor;
    FakeBytes bytes;
    FakeWave wave;
    std::vector<uint8_t> bank = std::vector<uint8_t>(16384);
    FskLoader loader;
    CassettePlayer player{loader};
    std::vector<CasSegment> tableA = std::vector<CasSegment>(8);
    std::vector<CasSegment> tableB = std::vector<CasSegment>(8);
    std::unique_ptr<CassetteCommands> cas;
    char out[128] = {};
    uint64_t now = 0;

    Rig()
    {
        loader.bind(Span<uint8_t>(bank.data(), bank.size()));
        files.files["a.cas"] = tapeA();
        files.files["dir/b.cas"] = tapeB();
        files.files["fsk.cas"] = tapeFsk();
        cas.reset(new CassetteCommands(player, loader, files, Span<CasSegment>(tableA.data(), tableA.size()),
                                       Span<CasSegment>(tableB.data(), tableB.size())));
    }

    const char* run(const char* args)
    {
        cas->run(args, motor, wave, Span<char>(out, sizeof(out)));
        return out;
    }

    void tick(uint64_t ms)
    {
        now += ms * 1000u;
        player.tick(now, motor, bytes, wave);
    }
};

// ---- mount ----

void test_mount_replies_name_and_blocks()
{
    Rig rig;
    TEST_ASSERT_EQUAL_STRING("mounted a.cas: 1 data, 2 fsk", rig.run("mount a.cas"));
    TEST_ASSERT_TRUE(rig.cas->mounted());
    TEST_ASSERT_TRUE(rig.player.loaded());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL_STRING("a.cas", rig.cas->name());
}

void test_mount_names_the_file_without_its_folder()
{
    Rig rig;
    TEST_ASSERT_EQUAL_STRING("mounted b.cas: 2 data, 0 fsk", rig.run("mount dir/b.cas"));
}

void test_mount_path_may_have_spaces()
{
    Rig rig;
    rig.files.files["Alien Ambush.cas"] = tapeB();
    TEST_ASSERT_EQUAL_STRING("mounted Alien Ambush.cas: 2 data, 0 fsk", rig.run("mount  Alien Ambush.cas "));
}

void test_mount_without_path_is_usage()
{
    Rig rig;
    TEST_ASSERT_EQUAL_STRING("usage: cas mount <path>", rig.run("mount"));
    TEST_ASSERT_EQUAL_STRING("usage: cas mount <path>", rig.run("mount   "));
    TEST_ASSERT_FALSE(rig.cas->mounted());
}

void test_mount_replaces_and_starts_at_the_beginning()
{
    Rig rig;
    rig.run("mount dir/b.cas");
    rig.tick(1);
    rig.tick(100);
    TEST_ASSERT_TRUE(rig.player.segment() > 0);
    TEST_ASSERT_EQUAL_STRING("mounted a.cas: 1 data, 2 fsk", rig.run("mount a.cas"));
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL_size_t(1, rig.files.closed.size());
    TEST_ASSERT_EQUAL_INT(1, rig.files.openNow);
}

void test_mount_the_same_tape_again_rewinds_it()
{
    Rig rig;
    rig.run("mount dir/b.cas");
    rig.tick(1);
    rig.tick(100);
    TEST_ASSERT_EQUAL_STRING("mounted b.cas: 2 data, 0 fsk", rig.run("mount dir/b.cas"));
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL_INT(1, rig.files.openNow);
}

// ---- AC6: a bad file leaves the mounted tape as it was ----

void expect_mount_fails_and_tape_stays(const Bytes& bad, const char* reply)
{
    Rig rig;
    rig.files.files["bad.cas"] = bad;
    rig.run("mount dir/b.cas");
    rig.tick(1);
    rig.tick(20);  // the first record (1 byte at 600 baud) is out: in the second
    const uint32_t segment = rig.player.segment();
    TEST_ASSERT_EQUAL_STRING(reply, rig.run("mount bad.cas"));
    TEST_ASSERT_EQUAL_STRING("b.cas", rig.cas->name());
    TEST_ASSERT_EQUAL_UINT32(segment, rig.player.segment());
    TEST_ASSERT_EQUAL_INT(1, rig.files.openNow);  // the bad file was closed
    TEST_ASSERT_EQUAL_UINT32(0, rig.wave.stops);
    TEST_ASSERT_EQUAL_STRING("b.cas: segment 2/2, data, motor on", rig.run("status"));
    // The old tape still plays from where it was, with its own segments.
    rig.tick(100);
    TEST_ASSERT_TRUE(rig.player.atEnd());
    TEST_ASSERT_EQUAL_size_t(2, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT8(1, rig.bytes.sent[0]);
    TEST_ASSERT_EQUAL_UINT8(2, rig.bytes.sent[1]);
}

void test_mount_without_fuji_keeps_the_tape()
{
    expect_mount_fails_and_tape_stays(chunk("data", 0, {1}),
                                      "bad.cas: not a valid .cas (FUJI header, cut block or baud 0)");
}

void test_mount_cut_block_keeps_the_tape()
{
    Bytes cut = fuji() + chunk("data", 0, {1, 2, 3, 4});
    cut.pop_back();
    expect_mount_fails_and_tape_stays(cut, "bad.cas: not a valid .cas (FUJI header, cut block or baud 0)");
}

void test_mount_baud_zero_keeps_the_tape()
{
    expect_mount_fails_and_tape_stays(fuji() + chunk("baud", 0, {}) + chunk("data", 0, {1}),
                                      "bad.cas: not a valid .cas (FUJI header, cut block or baud 0)");
}

void test_mount_pwm_keeps_the_tape()
{
    expect_mount_fails_and_tape_stays(fuji() + chunk("pwms", 0, {1, 0}), "bad.cas: pwm blocks are not supported");
}

void test_mount_too_many_blocks_keeps_the_tape()
{
    Bytes many = fuji() + chunk("baud", 600, {});
    for (int i = 0; i < 9; ++i) {
        many = many + chunk("data", 0, {1});
    }
    expect_mount_fails_and_tape_stays(many, "bad.cas: too many blocks (max 8)");
}

void test_mount_missing_file_keeps_the_tape()
{
    Rig rig;
    rig.run("mount dir/b.cas");
    TEST_ASSERT_EQUAL_STRING("cannot open nope.cas", rig.run("mount nope.cas"));
    TEST_ASSERT_EQUAL_STRING("b.cas", rig.cas->name());
    TEST_ASSERT_TRUE(rig.player.loaded());
}

// ---- status, eject, rewind ----

void test_status_without_tape()
{
    Rig rig;
    TEST_ASSERT_EQUAL_STRING("no tape, motor on", rig.run("status"));
}

void test_status_shows_segment_phase_and_motor()
{
    Rig rig;
    rig.run("mount a.cas");
    TEST_ASSERT_EQUAL_STRING("a.cas: segment 1/2, gap, motor on", rig.run("status"));
    rig.motor.motor = false;
    TEST_ASSERT_EQUAL_STRING("a.cas: segment 1/2, gap, motor off", rig.run("status"));
}

// cassette-motor-fujinet (review of PR #122, H2): when another engine plays the tape, `cas status` says
// where that engine is, not where the idle CassettePlayer was left.
namespace {
struct FakePosition final : rb::cassette::TapePosition {
    bool end = false;
    uint32_t at = 0;
    const char* name = "gap";
    bool atEnd() const noexcept override { return end; }
    uint32_t segment() const noexcept override { return at; }
    const char* phase() const noexcept override { return name; }
};
}  // namespace

void test_status_follows_the_engine_that_plays()
{
    Rig rig;
    FakePosition position;
    rig.cas->showPosition(&position);
    TEST_ASSERT_EQUAL_STRING("no tape, motor on", rig.run("status"));
    rig.run("mount a.cas");
    position.at = 1;
    position.name = "data";
    TEST_ASSERT_EQUAL_STRING("a.cas: segment 2/2, data, motor on", rig.run("status"));
    position.end = true;
    TEST_ASSERT_EQUAL_STRING("a.cas: end, motor on", rig.run("status"));
    TEST_ASSERT_FALSE(rig.player.atEnd());  // the player was never moved
    rig.cas->showPosition(nullptr);
    TEST_ASSERT_EQUAL_STRING("a.cas: segment 1/2, gap, motor on", rig.run("status"));
}

void test_status_at_the_end()
{
    Rig rig;
    rig.run("mount dir/b.cas");
    rig.tick(1);
    rig.tick(100);
    rig.tick(100);
    TEST_ASSERT_TRUE(rig.player.atEnd());
    TEST_ASSERT_EQUAL_STRING("b.cas: end, motor on", rig.run("status"));
}

void test_status_says_when_a_run_does_not_fit()
{
    Rig rig;
    rig.bank.assign(64, 0);
    rig.loader.bind(Span<uint8_t>(rig.bank.data(), rig.bank.size()));
    rig.run("mount fsk.cas");
    rig.tick(1);
    TEST_ASSERT_TRUE(rig.player.blocked());
    TEST_ASSERT_EQUAL_STRING("fsk.cas: segment 1/1, gap, motor on, run too big", rig.run("status"));
}

void test_eject()
{
    Rig rig;
    rig.run("mount a.cas");
    TEST_ASSERT_EQUAL_STRING("ejected a.cas", rig.run("eject"));
    TEST_ASSERT_FALSE(rig.cas->mounted());
    TEST_ASSERT_FALSE(rig.player.loaded());
    TEST_ASSERT_EQUAL_INT(0, rig.files.openNow);
    TEST_ASSERT_EQUAL_STRING("no tape, motor on", rig.run("status"));
    TEST_ASSERT_EQUAL_STRING("no tape", rig.run("eject"));
}

void test_rewind()
{
    Rig rig;
    TEST_ASSERT_EQUAL_STRING("no tape", rig.run("rewind"));
    rig.run("mount dir/b.cas");
    rig.tick(1);
    rig.tick(100);
    TEST_ASSERT_TRUE(rig.player.segment() > 0);
    TEST_ASSERT_EQUAL_STRING("rewound b.cas", rig.run("rewind"));
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::Gap), static_cast<int>(rig.player.phase()));
}

void test_unknown_or_empty_is_usage()
{
    Rig rig;
    const char* usage = "usage: cas mount <path> | status | eject | rewind";
    TEST_ASSERT_EQUAL_STRING(usage, rig.run(""));
    TEST_ASSERT_EQUAL_STRING(usage, rig.run(nullptr));
    TEST_ASSERT_EQUAL_STRING(usage, rig.run("play"));
    TEST_ASSERT_EQUAL_STRING(usage, rig.run("statusx"));
    TEST_ASSERT_EQUAL_STRING("no tape, motor on", rig.run("  status  "));
}

void test_reply_is_cut_to_the_buffer()
{
    Rig rig;
    char small[8];
    rig.cas->run("eject", rig.motor, rig.wave, Span<char>(small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("no tape", small);
    char tiny[4];
    rig.cas->run("eject", rig.motor, rig.wave, Span<char>(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("no ", tiny);
    rig.cas->run("status", rig.motor, rig.wave, Span<char>());  // nothing to write into: no crash
}

void test_long_name_is_cut()
{
    Rig rig;
    const std::string longName(100, 'x');
    rig.files.files[longName] = tapeB();
    rig.run(("mount " + longName).c_str());
    TEST_ASSERT_EQUAL_size_t(CassetteCommands::kNameMax - 1, std::strlen(rig.cas->name()));
}

// ---- a tape that leaves while the producer still reads it ----

void test_tape_leaving_while_loading_closes_after_the_producer_stops()
{
    Rig rig;
    rig.run("mount fsk.cas");
    rig.tick(1);  // the loader starts with the gap
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Loading), static_cast<int>(rig.loader.state()));
    const TapeSource* old = rig.files.open_.back().get();
    TEST_ASSERT_EQUAL_STRING("mounted b.cas: 2 data, 0 fsk", rig.run("mount dir/b.cas"));
    TEST_ASSERT_EQUAL_size_t(0, rig.files.closed.size());  // the producer may be reading it
    TEST_ASSERT_EQUAL_INT(2, rig.files.openNow);
    TEST_ASSERT_EQUAL_STRING("busy: the last tape is still closing", rig.run("mount a.cas"));
    TEST_ASSERT_EQUAL_STRING("b.cas", rig.cas->name());
    TEST_ASSERT_EQUAL_STRING("busy: the last tape is still closing", rig.run("eject"));
    TEST_ASSERT_TRUE(rig.cas->mounted());
    (void)rig.loader.step();  // the producer sees the stop
    TEST_ASSERT_EQUAL_STRING("b.cas: segment 1/2, gap, motor on", rig.run("status"));
    TEST_ASSERT_EQUAL_size_t(1, rig.files.closed.size());
    TEST_ASSERT_EQUAL_PTR(old, rig.files.closed[0]);
    TEST_ASSERT_EQUAL_STRING("mounted a.cas: 1 data, 2 fsk", rig.run("mount a.cas"));
    TEST_ASSERT_TRUE(rig.files.maxOpen <= 3);
}

void test_eject_while_loading_closes_after_the_producer_stops()
{
    Rig rig;
    rig.run("mount fsk.cas");
    rig.tick(1);
    TEST_ASSERT_EQUAL_STRING("ejected fsk.cas", rig.run("eject"));
    TEST_ASSERT_EQUAL_INT(1, rig.files.openNow);
    (void)rig.loader.step();
    TEST_ASSERT_EQUAL_STRING("no tape, motor on", rig.run("status"));
    TEST_ASSERT_EQUAL_INT(0, rig.files.openNow);
}

// The producer stopped on the old tape and the player already started it on the new one:
// the old tape closes even though the loader is loading again.
void test_tape_closes_once_the_loader_moved_to_the_new_one()
{
    Rig rig;
    rig.files.files["fsk2.cas"] = tapeFsk();
    rig.run("mount fsk.cas");
    rig.tick(1);
    rig.run("mount fsk2.cas");
    (void)rig.loader.step();  // stops on fsk.cas
    rig.tick(1);              // the player starts the loader on fsk2.cas
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Loading), static_cast<int>(rig.loader.state()));
    TEST_ASSERT_EQUAL_INT(2, rig.files.openNow);
    rig.run("status");
    TEST_ASSERT_EQUAL_INT(1, rig.files.openNow);
    TEST_ASSERT_EQUAL_STRING("ejected fsk2.cas", rig.run("eject"));
}

void test_failed_mount_while_closing_keeps_everything()
{
    Rig rig;
    rig.files.files["bad.cas"] = chunk("data", 0, {1});
    rig.run("mount fsk.cas");
    rig.tick(1);
    rig.run("mount dir/b.cas");
    TEST_ASSERT_EQUAL_STRING("busy: the last tape is still closing", rig.run("mount bad.cas"));
    TEST_ASSERT_EQUAL_INT(2, rig.files.openNow);
}

// ---- no allocation (ADR-011) ----

void test_commands_do_not_allocate()
{
    Rig rig;
    rig.files.files["bad.cas"] = chunk("data", 0, {1});
    rig.run("status");  // warm-up
    const unsigned long before = g_allocations;
    rig.files.open_.reserve(64);
    const unsigned long reserved = g_allocations;
    rig.cas->run("mount dir/b.cas", rig.motor, rig.wave, Span<char>(rig.out, sizeof(rig.out)));
    rig.cas->run("status", rig.motor, rig.wave, Span<char>(rig.out, sizeof(rig.out)));
    rig.cas->run("mount bad.cas", rig.motor, rig.wave, Span<char>(rig.out, sizeof(rig.out)));
    rig.cas->run("rewind", rig.motor, rig.wave, Span<char>(rig.out, sizeof(rig.out)));
    rig.cas->run("eject", rig.motor, rig.wave, Span<char>(rig.out, sizeof(rig.out)));
    // The fake opens with `new` (one per open); the commands themselves add nothing.
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(g_allocations - reserved));
    (void)before;
    // Positive control: the counter does see an allocation.
    const unsigned long control = g_allocations;
    std::unique_ptr<int> probe(new int(1));
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(g_allocations - control));
}

// ---- real tape ----

bool readFile(const std::string& path, Bytes& out)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t buffer[4096];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        out.insert(out.end(), buffer, buffer + got);
    }
    std::fclose(file);
    return true;
}

void test_mount_real_alien_ambush()
{
    const char* samples = std::getenv("RB_CAS_SAMPLES");
    if (samples == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    Bytes file;
    if (!readFile(std::string(samples) + "/PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas", file)) {
        TEST_IGNORE_MESSAGE("PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas not found");
    }
    // Count the chunks independently of CasIndex: walk the headers.
    unsigned data = 0;
    unsigned fsk = 0;
    for (std::size_t at = 0; at + 8 <= file.size();) {
        const std::size_t length = file[at + 4] | (file[at + 5] << 8);
        data += std::memcmp(&file[at], "data", 4) == 0 ? 1u : 0u;
        fsk += std::memcmp(&file[at], "fsk ", 4) == 0 ? 1u : 0u;
        at += 8 + length;
    }
    Rig rig;
    rig.files.files["MicroSD/PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas"] = file;
    rig.tableA.resize(256);
    rig.tableB.resize(256);
    rig.cas.reset(new CassetteCommands(rig.player, rig.loader, rig.files,
                                       Span<CasSegment>(rig.tableA.data(), rig.tableA.size()),
                                       Span<CasSegment>(rig.tableB.data(), rig.tableB.size())));
    char expected[128];
    std::snprintf(expected, sizeof(expected), "mounted PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas: %u data, %u fsk", data, fsk);
    TEST_ASSERT_EQUAL_STRING(expected, rig.run("mount MicroSD/PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas"));
    TEST_ASSERT_EQUAL_UINT32(51, data);
    TEST_ASSERT_EQUAL_STRING("PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas: segment 1/100, gap, motor on", rig.run("status"));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_mount_replies_name_and_blocks);
    RUN_TEST(test_mount_names_the_file_without_its_folder);
    RUN_TEST(test_mount_path_may_have_spaces);
    RUN_TEST(test_mount_without_path_is_usage);
    RUN_TEST(test_mount_replaces_and_starts_at_the_beginning);
    RUN_TEST(test_mount_the_same_tape_again_rewinds_it);
    RUN_TEST(test_mount_without_fuji_keeps_the_tape);
    RUN_TEST(test_mount_cut_block_keeps_the_tape);
    RUN_TEST(test_mount_baud_zero_keeps_the_tape);
    RUN_TEST(test_mount_pwm_keeps_the_tape);
    RUN_TEST(test_mount_too_many_blocks_keeps_the_tape);
    RUN_TEST(test_mount_missing_file_keeps_the_tape);
    RUN_TEST(test_status_without_tape);
    RUN_TEST(test_status_shows_segment_phase_and_motor);
    RUN_TEST(test_status_at_the_end);
    RUN_TEST(test_status_says_when_a_run_does_not_fit);
    RUN_TEST(test_eject);
    RUN_TEST(test_rewind);
    RUN_TEST(test_unknown_or_empty_is_usage);
    RUN_TEST(test_reply_is_cut_to_the_buffer);
    RUN_TEST(test_long_name_is_cut);
    RUN_TEST(test_tape_leaving_while_loading_closes_after_the_producer_stops);
    RUN_TEST(test_eject_while_loading_closes_after_the_producer_stops);
    RUN_TEST(test_tape_closes_once_the_loader_moved_to_the_new_one);
    RUN_TEST(test_failed_mount_while_closing_keeps_everything);
    RUN_TEST(test_commands_do_not_allocate);
    RUN_TEST(test_status_follows_the_engine_that_plays);
    RUN_TEST(test_mount_real_alien_ambush);
    return UNITY_END();
}
