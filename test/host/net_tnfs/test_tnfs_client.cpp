// TnfsClient against FakeTnfsServer (menu-tnfs T005). The fake records every
// request it receives, so each test also checks the read-only guarantee: only
// the commands of plan §0, every OPEN with flags 0x0001 and mode 0, and every
// MOUNT anonymous with protocol 1.2.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_tnfs_server.hpp"
#include "rb/net_tnfs/tnfs_client.hpp"
#include "unity.h"

using rb::core::Span;
using rb::test::FakeClock;
using rb::test::FakeTnfsServer;
namespace tnfs = rb::net_tnfs;
using tnfs::TnfsClient;
using tnfs::TnfsResult;

namespace {

using Bytes = std::vector<uint8_t>;

FakeClock clock_;  // only moves forward: tests measure from a starting point

struct Clock final : tnfs::TnfsClock {
    uint64_t nowMicros() const override { return clock_.nowMicros(); }
    void sleepUntil(uint64_t micros) override { clock_.waitUntil(micros); }
};

struct Names final : tnfs::DirVisitor {
    std::vector<std::string> names;
    int restarts = 0;
    std::size_t stopAfter = SIZE_MAX;    // entry() answers false once this many arrived
    std::size_t cancelAfter = SIZE_MAX;  // cancelled() turns true once this many arrived
    uint64_t cancelAtUs = UINT64_MAX;    // or once the clock reaches this time
    void restart() override
    {
        names.clear();
        ++restarts;
    }
    bool entry(Span<const uint8_t> name) override
    {
        names.emplace_back(reinterpret_cast<const char*>(name.data()), name.size());
        return names.size() < stopAfter;
    }
    bool cancelled() override { return names.size() >= cancelAfter || clock_.nowMicros() >= cancelAtUs; }
};

struct Sink final : tnfs::ReadSink {
    Bytes bytes;
    uint32_t size = 0;
    int begins = 0;
    int blocks = 0;
    int blocksSinceBegin = 0;
    bool refuse = false;
    int cancelAfterBlocks = -1;
    uint64_t cancelAtUs = UINT64_MAX;
    FakeTnfsServer* forgetOnBlock = nullptr;  // forgets the session after the first block of each pass
    int forgetTimes = 0;
    bool begin(uint32_t total) override
    {
        ++begins;
        blocksSinceBegin = 0;
        if (refuse) {
            return false;
        }
        size = total;
        bytes.assign(total, 0xEE);
        return true;
    }
    void data(uint32_t offset, Span<const uint8_t> got) override
    {
        TEST_ASSERT_TRUE(offset + got.size() <= size);
        std::memcpy(bytes.data() + offset, got.data(), got.size());
        ++blocks;
        ++blocksSinceBegin;
        if (forgetOnBlock != nullptr && forgetTimes > 0 && blocksSinceBegin == 1) {
            --forgetTimes;
            forgetOnBlock->forgetSession();
        }
    }
    bool cancelled() override
    {
        return (cancelAfterBlocks >= 0 && blocks >= cancelAfterBlocks) || clock_.nowMicros() >= cancelAtUs;
    }
};

Bytes image(std::size_t n)
{
    Bytes bytes(n);
    for (std::size_t i = 0; i < n; ++i) {
        bytes[i] = static_cast<uint8_t>((i * 13 + 5) & 0xFF);
    }
    return bytes;
}

// The read-only guarantee, on everything the server received (AC12).
void assert_read_only(const FakeTnfsServer& server)
{
    const uint8_t allowed[] = {0x00, 0x01, 0x10, 0x11, 0x12, 0x21, 0x23, 0x24, 0x29};
    for (const auto& entry : server.log()) {
        bool ok = false;
        for (uint8_t a : allowed) {
            ok = ok || entry.command == a;
        }
        TEST_ASSERT_TRUE_MESSAGE(ok, "a command outside plan section 0 was sent");
        if (entry.command == 0x29) {
            TEST_ASSERT_TRUE(entry.isOpen);
            TEST_ASSERT_EQUAL_HEX16(0x0001, entry.flags);
            TEST_ASSERT_EQUAL_HEX16(0x0000, entry.mode);
        }
        if (entry.command == 0x00) {
            TEST_ASSERT_EQUAL_HEX16(0x0102, entry.version);
            TEST_ASSERT_EQUAL_STRING("/", entry.path.c_str());
            TEST_ASSERT_EQUAL_STRING("", entry.user.c_str());
            TEST_ASSERT_EQUAL_STRING("", entry.password.c_str());
        }
    }
}

std::size_t count(const FakeTnfsServer& server, uint8_t command)
{
    std::size_t n = 0;
    for (const auto& entry : server.log()) {
        n += entry.command == command ? 1 : 0;
    }
    return n;
}

// Every request after the first of its command and sequence must be a retry
// (same sequence); every new request a new sequence. Checks the second half:
// two consecutive requests that are not retries never share a sequence number.
void assert_new_requests_get_new_sequences(const FakeTnfsServer& server)
{
    const auto& log = server.log();
    for (std::size_t i = 1; i < log.size(); ++i) {
        if (!log[i].retry && log[i].command != 0x00 && log[i].command != 0x01) {
            TEST_ASSERT_FALSE(log[i].sequence == log[i - 1].sequence && log[i].command == log[i - 1].command);
        }
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- MOUNT and first contact --------------------------------------------------

static void test_mount_is_anonymous_and_keeps_the_server_info(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(1000);
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    TEST_ASSERT_TRUE(client.mounted());
    TEST_ASSERT_EQUAL_UINT8(1, client.serverInfo().versionMajor);
    TEST_ASSERT_EQUAL_UINT16(1000, client.serverInfo().minRetryMs);
    TEST_ASSERT_EQUAL_size_t(1, server.log().size());
    assert_read_only(server);
}

static void test_a_mute_server_gives_no_response_after_six_seconds(void)
{
    FakeTnfsServer server(clock_);
    server.silence(0x00);
    server.silence(0x00);
    server.silence(0x00);
    Clock clk;
    TnfsClient client(server, clk);
    const uint64_t start = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::NoResponse);
    TEST_ASSERT_EQUAL_UINT64(start + 6000000, clock_.nowMicros());
    TEST_ASSERT_EQUAL_size_t(3, count(server, 0x00));
    TEST_ASSERT_FALSE(server.log()[0].retry);
    TEST_ASSERT_EQUAL_HEX8(server.log()[0].sequence, server.log()[2].sequence);  // retries repeat it
    TEST_ASSERT_FALSE(client.mounted());
    assert_read_only(server);
}

static void test_mount_answers_on_the_third_attempt(void)
{
    FakeTnfsServer server(clock_);
    server.silence(0x00);
    server.silence(0x00);
    Clock clk;
    TnfsClient client(server, clk);
    const uint64_t start = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_UINT64(start + 4000000, clock_.nowMicros());
    assert_read_only(server);
}

static void test_a_refused_mount_is_rejected_with_its_status(void)
{
    FakeTnfsServer server(clock_);
    server.reject(0x00, 0x09);
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Rejected);
    TEST_ASSERT_EQUAL_HEX8(0x09, client.lastStatus());
    TEST_ASSERT_FALSE(client.mounted());
    assert_read_only(server);
}

static void test_an_unreachable_server_fails_fast(void)
{
    FakeTnfsServer server(clock_);
    server.unreachable(3);
    Clock clk;
    TnfsClient client(server, clk);
    const uint64_t start = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::NoResponse);
    TEST_ASSERT_TRUE(clock_.nowMicros() - start < 6000000);  // no full waits
    assert_read_only(server);
}

// --- Waits after MOUNT ------------------------------------------------------------

static void test_attempts_wait_the_announced_minimum(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(1000);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.silence(0x24);
    server.silence(0x24);
    const uint64_t start = clock_.nowMicros();
    auto st = client.stat("/a.atr");
    TEST_ASSERT_TRUE(st.result == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_UINT64(start + 2000000, clock_.nowMicros());  // two waits of 1000 ms

    server.silence(0x24);
    server.silence(0x24);
    server.silence(0x24);
    const uint64_t again = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::NoResponse);
    TEST_ASSERT_EQUAL_UINT64(again + 3000000, clock_.nowMicros());
    assert_read_only(server);
}

static void test_without_an_announced_minimum_attempts_wait_two_seconds(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(0);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.silence(0x24);
    const uint64_t start = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_UINT64(start + 2000000, clock_.nowMicros());
    assert_read_only(server);
}

static void test_an_announced_wait_is_capped_at_five_seconds(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(65535);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_UINT16(65535, client.serverInfo().minRetryMs);  // kept as announced
    server.silence(0x29);
    server.silence(0x29);
    server.silence(0x29);
    Sink sink;
    const uint64_t start = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/a.atr", sink) == TnfsResult::NoResponse);
    TEST_ASSERT_EQUAL_UINT64(start + 3 * TnfsClient::kMaxWaitUs, clock_.nowMicros());  // STAT answered at once

    server.eagain(0x24, 60000);
    const uint64_t again = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_UINT64(again + TnfsClient::kMaxWaitUs, clock_.nowMicros());
    assert_read_only(server);
}

static void test_eagain_waits_what_it_asks_without_using_an_attempt(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(1000);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.silence(0x24);
    server.eagain(0x24, 300);
    server.silence(0x24);
    server.eagain(0x24, 200);
    const uint64_t start = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Ok);  // 2 silences, 2 EAGAIN: still answered
    TEST_ASSERT_EQUAL_UINT64(start + 1000000 + 300000 + 1000000 + 200000, clock_.nowMicros());
    assert_read_only(server);
}

static void test_endless_eagain_ends_in_no_response(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    for (unsigned i = 0; i < 50; ++i) {
        server.eagain(0x24, 10);
    }
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::NoResponse);
    TEST_ASSERT_EQUAL_size_t(TnfsClient::kMaxEagain + 1, count(server, 0x24));
    assert_read_only(server);
}

// --- Discarding what is not the reply --------------------------------------------

static void test_a_late_reply_is_discarded_and_the_retry_repeats_the_sequence(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(1000);
    const Bytes file = image(1500);
    server.addFile("/a.atr", file);
    Clock clk;
    TnfsClient client(server, clk);
    server.lateReply(0x21, 1500);  // the first READ reply arrives after the retry
    Sink sink;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/a.atr", sink) == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_size_t(file.size(), sink.bytes.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(file.data(), sink.bytes.data(), file.size());  // no block skipped or doubled
    bool sawRetry = false;
    for (const auto& entry : server.log()) {
        sawRetry = sawRetry || (entry.command == 0x21 && entry.retry);
    }
    TEST_ASSERT_TRUE(sawRetry);
    assert_new_requests_get_new_sequences(server);
    assert_read_only(server);
}

static void test_a_malformed_reply_once_is_waited_past(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    server.malformed(0x24);
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Ok);
    assert_read_only(server);
}

static void test_always_malformed_is_malformed(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.malformed(0x24);
    server.malformed(0x24);
    server.malformed(0x24);
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Malformed);
    assert_read_only(server);
}

static void test_a_short_stat_body_is_malformed(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.replyWith(0x24, {0xA4, 0x81});  // OK, but only the mode
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Malformed);
    assert_read_only(server);
}

// --- stat and listDir ------------------------------------------------------------

static void test_stat_of_file_directory_and_missing(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/Juegos/a.atr", Bytes(92176));
    Clock clk;
    TnfsClient client(server, clk);
    auto file = client.stat("/Juegos/a.atr");  // mounts on its own
    TEST_ASSERT_TRUE(file.result == TnfsResult::Ok);
    TEST_ASSERT_FALSE(file.isDirectory);
    TEST_ASSERT_EQUAL_UINT32(92176, file.size);
    auto dir = client.stat("/Juegos");
    TEST_ASSERT_TRUE(dir.result == TnfsResult::Ok);
    TEST_ASSERT_TRUE(dir.isDirectory);
    TEST_ASSERT_TRUE(client.stat("/nada").result == TnfsResult::NotOpened);
    assert_read_only(server);
}

static void test_list_dir_gives_every_entry_and_closes(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/J/b.atr", Bytes(1));
    server.addFile("/J/a.atr", Bytes(1));
    server.addDirectory("/J/Sub");
    Clock clk;
    TnfsClient client(server, clk);
    Names names;
    TEST_ASSERT_TRUE(client.listDir("/J", names) == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_size_t(5, names.names.size());
    TEST_ASSERT_EQUAL_STRING(".", names.names[0].c_str());
    TEST_ASSERT_EQUAL_STRING("..", names.names[1].c_str());
    TEST_ASSERT_EQUAL_STRING("Sub", names.names[2].c_str());
    TEST_ASSERT_EQUAL_STRING("a.atr", names.names[3].c_str());
    TEST_ASSERT_EQUAL_STRING("b.atr", names.names[4].c_str());
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x12));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    TEST_ASSERT_EQUAL_INT(0, names.restarts);
    assert_read_only(server);
    assert_new_requests_get_new_sequences(server);
}

static void test_list_dir_refused_is_not_opened(void)
{
    FakeTnfsServer server(clock_);
    server.addDirectory("/J");
    Clock clk;
    TnfsClient client(server, clk);
    server.reject(0x10, 0x09);
    Names names;
    TEST_ASSERT_TRUE(client.listDir("/J", names) == TnfsResult::NotOpened);
    Names missing;
    TEST_ASSERT_TRUE(client.listDir("/nada", missing) == TnfsResult::NotOpened);
    TEST_ASSERT_EQUAL_size_t(0, count(server, 0x12));  // nothing was opened
    assert_read_only(server);
}

static void test_list_dir_cancelled_or_stopped_still_closes(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/J/a.atr", Bytes(1));
    server.addFile("/J/b.atr", Bytes(1));
    Clock clk;
    TnfsClient client(server, clk);
    Names cancelled;
    cancelled.cancelAfter = 2;
    TEST_ASSERT_TRUE(client.listDir("/J", cancelled) == TnfsResult::Cancelled);
    TEST_ASSERT_EQUAL_size_t(2, cancelled.names.size());
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    Names stopped;
    stopped.stopAfter = 1;
    TEST_ASSERT_TRUE(client.listDir("/J", stopped) == TnfsResult::Cancelled);
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    TEST_ASSERT_EQUAL_size_t(2, count(server, 0x12));
    assert_read_only(server);
}

static void test_list_dir_failing_half_way_still_closes(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/J/a.atr", Bytes(1));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.lateReply(0x10, 1500);  // OPENDIR runs, its reply misses the 1000 ms wait: the retry must not open again
    Names names;
    server.silence(0x11);
    server.silence(0x11);
    server.silence(0x11);
    TEST_ASSERT_TRUE(client.listDir("/J", names) == TnfsResult::NoResponse);
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x12));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

// --- readWholeReadOnly -------------------------------------------------------------

static void test_read_whole_file_in_blocks_and_close(void)
{
    FakeTnfsServer server(clock_);
    const Bytes file = image(1300);
    server.addFile("/d.atr", file);
    Clock clk;
    TnfsClient client(server, clk);
    Sink sink;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_INT(1, sink.begins);
    TEST_ASSERT_EQUAL_UINT32(1300, sink.size);
    TEST_ASSERT_EQUAL_INT(3, sink.blocks);  // 512 + 512 + 276
    TEST_ASSERT_EQUAL_HEX8_ARRAY(file.data(), sink.bytes.data(), file.size());
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x29));
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x23));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
    assert_new_requests_get_new_sequences(server);
}

static void test_read_an_empty_file(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/vacio.atr", Bytes{});
    Clock clk;
    TnfsClient client(server, clk);
    Sink sink;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/vacio.atr", sink) == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_UINT32(0, sink.size);
    TEST_ASSERT_EQUAL_INT(0, sink.blocks);
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_read_refusals_are_not_opened(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", Bytes(10));
    server.addDirectory("/dir");
    Clock clk;
    TnfsClient client(server, clk);
    Sink missing;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/nada.atr", missing) == TnfsResult::NotOpened);
    Sink dir;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/dir", dir) == TnfsResult::NotOpened);
    TEST_ASSERT_EQUAL_INT(0, dir.begins);
    Sink noMemory;
    noMemory.refuse = true;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", noMemory) == TnfsResult::NotOpened);
    TEST_ASSERT_EQUAL_size_t(0, count(server, 0x29));  // refused before opening
    server.reject(0x29, 0x09);
    Sink denied;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", denied) == TnfsResult::NotOpened);
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_read_cancelled_still_closes(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", image(2000));
    Clock clk;
    TnfsClient client(server, clk);
    Sink sink;
    sink.cancelAfterBlocks = 1;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Cancelled);
    TEST_ASSERT_EQUAL_INT(1, sink.blocks);
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x23));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_read_failing_half_way_still_closes(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", image(2000));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.silence(0x21);
    server.silence(0x21);
    server.silence(0x21);
    Sink sink;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::NoResponse);
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x23));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_ok_reads_without_data_end_in_malformed(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", image(2000));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    for (unsigned i = 0; i < 20; ++i) {
        server.replyWith(0x21, {0x00, 0x00});  // OK, count 0: no progress
    }
    Sink sink;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Malformed);
    TEST_ASSERT_EQUAL_size_t(TnfsClient::kMaxEmptyReads, count(server, 0x21));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_more_data_than_stat_said_is_malformed(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", image(4));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.replyWith(0x21, {0x06, 0x00, 1, 2, 3, 4, 5, 6});  // 6 bytes of a 4-byte file
    Sink sink;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Malformed);
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_eof_before_the_stat_size_is_malformed(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", image(1000));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.reject(0x21, 0x21);  // EOF on the first READ of a 1000-byte file
    Sink sink;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Malformed);
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

// --- Bounded and cancellable waits --------------------------------------------------

static void test_a_long_eagain_waits_at_most_one_attempt(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(1000);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.eagain(0x24, 60000);  // the server asks for a minute
    const uint64_t start = clock_.nowMicros();
    TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_UINT64(start + 1000000, clock_.nowMicros());
    assert_read_only(server);
}

static void test_cancelling_cuts_a_wait_for_a_listing_short(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/J/a.atr", Bytes(1));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.silence(0x11);
    server.silence(0x11);
    server.silence(0x11);
    Names names;
    const uint64_t start = clock_.nowMicros();
    names.cancelAtUs = start + 300000;
    TEST_ASSERT_TRUE(client.listDir("/J", names) == TnfsResult::Cancelled);
    TEST_ASSERT_TRUE(clock_.nowMicros() <= start + 300000 + TnfsClient::kCancelCheckUs);
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x12));  // still closed
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_cancelling_cuts_an_eagain_wait_for_a_read_short(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(2000);
    server.addFile("/d.atr", image(2000));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    for (unsigned i = 0; i < 10; ++i) {
        server.eagain(0x21, 60000);
    }
    Sink sink;
    const uint64_t start = clock_.nowMicros();
    sink.cancelAtUs = start + 250000;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Cancelled);
    TEST_ASSERT_TRUE(clock_.nowMicros() <= start + 250000 + TnfsClient::kCancelCheckUs);
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x23));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

struct CancelAt final : tnfs::CancelCheck {
    uint64_t atUs = UINT64_MAX;
    bool cancelled() override { return clock_.nowMicros() >= atUs; }
};

static void test_cancelling_cuts_a_wait_for_a_stat_short(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.silence(0x24);
    server.silence(0x24);
    server.silence(0x24);
    CancelAt cancel;
    const uint64_t start = clock_.nowMicros();
    cancel.atUs = start + 250000;
    TEST_ASSERT_TRUE(client.stat("/a.atr", &cancel).result == TnfsResult::Cancelled);
    TEST_ASSERT_TRUE(clock_.nowMicros() <= start + 250000 + TnfsClient::kCancelCheckUs);
    assert_read_only(server);
}

static void test_cancelling_cuts_the_stat_before_a_read_short(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", image(100));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.silence(0x24);
    server.silence(0x24);
    server.silence(0x24);
    Sink sink;
    const uint64_t start = clock_.nowMicros();
    sink.cancelAtUs = start + 250000;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Cancelled);
    TEST_ASSERT_TRUE(clock_.nowMicros() <= start + 250000 + TnfsClient::kCancelCheckUs);
    TEST_ASSERT_EQUAL_size_t(0, count(server, 0x29));  // nothing was opened
    TEST_ASSERT_EQUAL_INT(0, sink.begins);
    assert_read_only(server);
}

// --- Silent reconnection ------------------------------------------------------------

static void test_a_forgotten_session_reconnects_once(void)
{
    const uint8_t codes[] = {0xFF, 0x20};
    for (uint8_t code : codes) {
        FakeTnfsServer server(clock_);
        server.addFile("/a.atr", Bytes(10));
        Clock clk;
        TnfsClient client(server, clk);
        TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
        server.forgetSession(code);
        TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Ok);
        TEST_ASSERT_EQUAL_size_t(2, count(server, 0x00));
        assert_read_only(server);
    }
}

static void test_a_read_restarts_from_zero_after_reconnecting(void)
{
    FakeTnfsServer server(clock_);
    const Bytes file = image(1500);
    server.addFile("/d.atr", file);
    Clock clk;
    TnfsClient client(server, clk);
    Sink sink;
    sink.forgetOnBlock = &server;
    sink.forgetTimes = 1;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_INT(2, sink.begins);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(file.data(), sink.bytes.data(), file.size());
    TEST_ASSERT_EQUAL_size_t(2, count(server, 0x00));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    assert_read_only(server);
}

static void test_losing_the_session_twice_is_no_response(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", image(1500));
    Clock clk;
    TnfsClient client(server, clk);
    Sink sink;
    sink.forgetOnBlock = &server;
    sink.forgetTimes = 2;
    TEST_ASSERT_TRUE(client.readWholeReadOnly("/d.atr", sink) == TnfsResult::NoResponse);
    TEST_ASSERT_EQUAL_size_t(2, count(server, 0x00));
    assert_read_only(server);
}

static void test_a_refused_reconnection_is_no_response(void)
{
    for (int op = 0; op < 3; ++op) {
        FakeTnfsServer server(clock_);
        server.addFile("/J/a.atr", Bytes(10));
        Clock clk;
        TnfsClient client(server, clk);
        TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
        server.forgetSession();
        server.reject(0x00, 0x09);  // the new MOUNT is refused
        TnfsResult result = TnfsResult::Ok;
        if (op == 0) {
            result = client.stat("/J/a.atr").result;
        } else if (op == 1) {
            Names names;
            result = client.listDir("/J", names);
        } else {
            Sink sink;
            result = client.readWholeReadOnly("/J/a.atr", sink);
        }
        TEST_ASSERT_TRUE(result == TnfsResult::NoResponse);
        TEST_ASSERT_FALSE(client.mounted());
        assert_read_only(server);
    }
}

static void test_a_listing_restarts_after_reconnecting(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/J/a.atr", Bytes(1));
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    server.forgetSession();
    Names names;
    TEST_ASSERT_TRUE(client.listDir("/J", names) == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_size_t(3, names.names.size());  // ".", "..", "a.atr", once
    TEST_ASSERT_EQUAL_INT(1, names.restarts);
    assert_read_only(server);
}

static void test_sequence_numbers_skip_zero_which_umount_uses(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/a.atr", Bytes(10));
    Clock clk;
    TnfsClient client(server, clk);
    for (unsigned i = 0; i < 300; ++i) {  // more than 256 requests: the counter wraps
        TEST_ASSERT_TRUE(client.stat("/a.atr").result == TnfsResult::Ok);
    }
    client.umount();
    for (std::size_t i = 0; i + 1 < server.log().size(); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(server.log()[i].sequence != 0, "a request other than UMOUNT used sequence 0");
    }
    TEST_ASSERT_EQUAL_HEX8(0x01, server.log().back().command);
    assert_read_only(server);
}

// --- UMOUNT -----------------------------------------------------------------------

static void test_umount_ends_the_session(void)
{
    FakeTnfsServer server(clock_);
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    client.umount();
    TEST_ASSERT_FALSE(client.mounted());
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x01));
    TEST_ASSERT_EQUAL_HEX8(0x00, server.log().back().sequence);
    client.umount();  // not mounted: nothing sent
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x01));
    assert_read_only(server);
}

static void test_mounting_again_ends_the_first_session(void)
{
    FakeTnfsServer server(clock_);
    Clock clk;
    TnfsClient client(server, clk);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    TEST_ASSERT_TRUE(client.mount() == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x01));
    TEST_ASSERT_TRUE(client.mounted());
    assert_read_only(server);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mount_is_anonymous_and_keeps_the_server_info);
    RUN_TEST(test_a_mute_server_gives_no_response_after_six_seconds);
    RUN_TEST(test_mount_answers_on_the_third_attempt);
    RUN_TEST(test_a_refused_mount_is_rejected_with_its_status);
    RUN_TEST(test_an_unreachable_server_fails_fast);
    RUN_TEST(test_attempts_wait_the_announced_minimum);
    RUN_TEST(test_without_an_announced_minimum_attempts_wait_two_seconds);
    RUN_TEST(test_an_announced_wait_is_capped_at_five_seconds);
    RUN_TEST(test_eagain_waits_what_it_asks_without_using_an_attempt);
    RUN_TEST(test_endless_eagain_ends_in_no_response);
    RUN_TEST(test_a_late_reply_is_discarded_and_the_retry_repeats_the_sequence);
    RUN_TEST(test_a_malformed_reply_once_is_waited_past);
    RUN_TEST(test_always_malformed_is_malformed);
    RUN_TEST(test_a_short_stat_body_is_malformed);
    RUN_TEST(test_stat_of_file_directory_and_missing);
    RUN_TEST(test_list_dir_gives_every_entry_and_closes);
    RUN_TEST(test_list_dir_refused_is_not_opened);
    RUN_TEST(test_list_dir_cancelled_or_stopped_still_closes);
    RUN_TEST(test_list_dir_failing_half_way_still_closes);
    RUN_TEST(test_read_whole_file_in_blocks_and_close);
    RUN_TEST(test_read_an_empty_file);
    RUN_TEST(test_read_refusals_are_not_opened);
    RUN_TEST(test_read_cancelled_still_closes);
    RUN_TEST(test_read_failing_half_way_still_closes);
    RUN_TEST(test_ok_reads_without_data_end_in_malformed);
    RUN_TEST(test_more_data_than_stat_said_is_malformed);
    RUN_TEST(test_eof_before_the_stat_size_is_malformed);
    RUN_TEST(test_a_long_eagain_waits_at_most_one_attempt);
    RUN_TEST(test_cancelling_cuts_a_wait_for_a_listing_short);
    RUN_TEST(test_cancelling_cuts_an_eagain_wait_for_a_read_short);
    RUN_TEST(test_cancelling_cuts_a_wait_for_a_stat_short);
    RUN_TEST(test_cancelling_cuts_the_stat_before_a_read_short);
    RUN_TEST(test_a_forgotten_session_reconnects_once);
    RUN_TEST(test_a_read_restarts_from_zero_after_reconnecting);
    RUN_TEST(test_losing_the_session_twice_is_no_response);
    RUN_TEST(test_a_refused_reconnection_is_no_response);
    RUN_TEST(test_a_listing_restarts_after_reconnecting);
    RUN_TEST(test_sequence_numbers_skip_zero_which_umount_uses);
    RUN_TEST(test_umount_ends_the_session);
    RUN_TEST(test_mounting_again_ends_the_first_session);
    return UNITY_END();
}
