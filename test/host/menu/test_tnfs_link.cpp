// What main.cpp wires for TNFS (menu-tnfs T013; AC7, AC12, M41-6): the trace
// of every TNFS request in the host log, the link of a source whose name does
// not resolve, and a JSONL log that two threads can write without mixing lines.

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_tnfs_server.hpp"
#include "host_image_repository.hpp"
#include "jsonl_log.hpp"
#include "memory_image_store.hpp"
#include "menu_catalog.hpp"
#include "menu_controller.hpp"
#include "menu_image.hpp"
#include "menu_sources.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/net_tnfs/tnfs_codec.hpp"
#include "tnfs_link.hpp"
#include "tnfs_loader.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif

namespace tnfs = rb::net_tnfs;
using rb::core::ErrorCode;
using rb::core::Span;
using rb::host::LoaderStatus;
using rb::host::TnfsFolder;
using rb::host::TnfsLoader;
using rb::test::FakeClock;
using rb::test::FakeTnfsServer;
using Bytes = std::vector<uint8_t>;

namespace {

FakeClock fakeClock;  // used by the loader thread only, while a loader runs

struct Clock final : tnfs::TnfsClock {
    uint64_t nowMicros() const override { return fakeClock.nowMicros(); }
    void sleepUntil(uint64_t micros) override { fakeClock.waitUntil(micros); }
};

struct Events final : rb::host::MenuEvents {
    void menuEvent(const char* name, const char* detail) override
    {
        log.push_back(std::string(name) + " " + detail);
    }
    std::vector<std::string> named(const char* name) const
    {
        std::vector<std::string> out;
        const std::string prefix = std::string(name) + " ";
        for (const std::string& line : log) {
            if (line.compare(0, prefix.size(), prefix) == 0) {
                out.push_back(line.substr(prefix.size()));
            }
        }
        return out;
    }
    std::vector<std::string> log;
};

struct Link final : rb::host::TnfsLink {
    explicit Link(rb::netsio::DatagramPort& p) : port_(p) {}
    rb::core::Result<void> open() override { return {}; }
    rb::netsio::DatagramPort& port() override { return port_; }
    rb::netsio::DatagramPort& port_;
};

struct NoNames final : rb::host::NameResolver {
    rb::core::Result<uint32_t> resolveIpv4(const char*) override
    {
        ++asked;
        return rb::core::Failure{ErrorCode::NotFound};
    }
    int asked = 0;
};

std::string describe(Span<const uint8_t> request)
{
    char text[400];
    rb::host::describeTnfsRequest(request, text, sizeof text);
    return text;
}

LoaderStatus awaitResult(TnfsLoader& loader, uint32_t job, TnfsFolder& folder)
{
    for (int i = 0; i < 10000; ++i) {
        const LoaderStatus status = loader.poll(job, folder);
        if (status.kind != LoaderStatus::Kind::InProgress) {
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_FAIL_MESSAGE("the job did not end");
    return LoaderStatus{};
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_a_request_is_described_by_command_and_open_by_its_flags_and_mode()
{
    uint8_t buffer[tnfs::kMaxRequest];
    const tnfs::ConnectionId connection{{0x12, 0x34}};
    const Span<uint8_t> out{buffer, sizeof buffer};

    std::size_t n = tnfs::encodeOpenReadOnly(connection, 7, "/a b/\"x\".atr", out).value();
    TEST_ASSERT_EQUAL_STRING("cmd=OPEN seq=7 flags=0x0001 mode=0x0000 path=/a b/\"x\".atr",
                             describe(Span<const uint8_t>{buffer, n}).c_str());
    n = tnfs::encodeMount(1, "/", out).value();
    TEST_ASSERT_EQUAL_STRING("cmd=MOUNT seq=1 path=/", describe(Span<const uint8_t>{buffer, n}).c_str());
    n = tnfs::encodeRead(connection, 9, 3, 512, out).value();
    TEST_ASSERT_EQUAL_STRING("cmd=READ seq=9", describe(Span<const uint8_t>{buffer, n}).c_str());
    n = tnfs::encodeStat(connection, 4, "/Juegos", out).value();
    TEST_ASSERT_EQUAL_STRING("cmd=STAT seq=4 path=/Juegos", describe(Span<const uint8_t>{buffer, n}).c_str());
    n = tnfs::encodeOpenDir(connection, 5, "/", out).value();
    TEST_ASSERT_EQUAL_STRING("cmd=OPENDIR seq=5 path=/", describe(Span<const uint8_t>{buffer, n}).c_str());
    n = tnfs::encodeUmount(connection, out).value();
    TEST_ASSERT_EQUAL_STRING("cmd=UMOUNT seq=0", describe(Span<const uint8_t>{buffer, n}).c_str());

    // Odd datagrams never make it read outside them.
    const uint8_t shortOne[] = {0x12, 0x34};
    TEST_ASSERT_EQUAL_STRING("short request (2 bytes)", describe(Span<const uint8_t>{shortOne, 2}).c_str());
    const uint8_t other[] = {0x12, 0x34, 0x02, 0x42};
    TEST_ASSERT_EQUAL_STRING("cmd=0x42 seq=2", describe(Span<const uint8_t>{other, 4}).c_str());
    const uint8_t cutOpen[] = {0x12, 0x34, 0x03, 0x29, 0x01};
    TEST_ASSERT_EQUAL_STRING("cmd=OPEN seq=3 (cut short)", describe(Span<const uint8_t>{cutOpen, 5}).c_str());
    const uint8_t noNul[] = {0x12, 0x34, 0x03, 0x24, '/', 'a'};
    TEST_ASSERT_EQUAL_STRING("cmd=STAT seq=3 path=/a", describe(Span<const uint8_t>{noNul, 6}).c_str());
    // A small buffer is cut, never overrun.
    char tiny[8];
    n = tnfs::encodeOpenReadOnly(connection, 7, "/a.atr", out).value();
    rb::host::describeTnfsRequest(Span<const uint8_t>{buffer, n}, tiny, sizeof tiny);
    TEST_ASSERT_EQUAL_STRING("cmd=OPE", tiny);
}

// AC12: every request that leaves is in the log, only read-only commands, and
// every OPEN carries O_RDONLY and mode 0.
void test_the_trace_logs_every_request_of_a_listing_and_a_fetch()
{
    FakeTnfsServer server{fakeClock};
    server.addDirectory("/Juegos");
    server.addFile("/a.atr", Bytes(3000, 7));
    Events events;
    rb::host::TnfsTracePort trace{server, 2, events};
    Link link{trace};
    rb::host::TnfsLink* links[TnfsLoader::kMaxSource + 1] = {};
    links[2] = &link;
    rb::host::MemoryImageStore store;
    rb::host::HeapImageMemory memory;
    Clock clock;
    std::size_t requests = 0;
    {
        TnfsLoader loader(links, store, clock, memory);
        TnfsFolder folder;
        TEST_ASSERT_TRUE(awaitResult(loader, loader.startList(2, "/", 1), folder).kind == LoaderStatus::Kind::Folder);
        const LoaderStatus image = awaitResult(loader, loader.startFetch(2, "/a.atr", 6), folder);
        TEST_ASSERT_TRUE(image.kind == LoaderStatus::Kind::Image);
        TEST_ASSERT_TRUE(store.release(image.slot));
    }
    for (const auto& entry : server.log()) {
        (void)entry;
        ++requests;
    }
    const std::vector<std::string> lines = events.named("tnfs_request");
    TEST_ASSERT_EQUAL_size_t(requests, lines.size());
    std::size_t opens = 0;
    for (const std::string& line : lines) {
        TEST_ASSERT_EQUAL_STRING_LEN("source=2 cmd=", line.c_str(), 13);
        const std::string command = line.substr(13, line.find(' ', 13) - 13);
        const bool allowed = command == "MOUNT" || command == "UMOUNT" || command == "OPENDIR" ||
                             command == "READDIR" || command == "CLOSEDIR" || command == "READ" ||
                             command == "CLOSE" || command == "STAT" || command == "OPEN";
        TEST_ASSERT_TRUE_MESSAGE(allowed, line.c_str());
        if (command == "OPEN") {
            ++opens;
            TEST_ASSERT_TRUE(line.find("flags=0x0001 mode=0x0000 path=/a.atr") != std::string::npos);
        }
    }
    TEST_ASSERT_EQUAL_size_t(1, opens);
}

// AC7: a source whose name does not resolve says so on screen and in the log.
void test_a_name_that_does_not_resolve_is_logged_and_shown()
{
    rb::host::MenuCatalog catalog;
    TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
    rb::host::MenuSources sources;
    std::FILE* file = std::tmpfile();
    TEST_ASSERT_NOT_NULL(file);
    std::fputs("no-such-host.invalid\n", file);
    std::rewind(file);
    char error[96];
    TEST_ASSERT_TRUE(sources.read(file, error, sizeof error));
    std::fclose(file);
    rb::host::MenuImage menu;
    rb::host::HostImageRepository repository{RB_TEST_MENU_DIR, menu};
    rb::app::InlineDriveMailbox mailbox;
    Events events;
    NoNames resolver;
    rb::host::UdpTnfsLink link{sources.name(2), 2, resolver, events};
    rb::host::TnfsLink* links[TnfsLoader::kMaxSource + 1] = {};
    links[2] = &link;
    rb::host::HeapImageMemory memory;
    Clock clock;
    TnfsLoader loader(links, repository.memory(), clock, memory);
    rb::host::MenuController controller{catalog, sources, repository, mailbox, &events, &loader};
    rb::host::MenuReply reply = controller.key(0x1E);  // 2
    for (int i = 0; i < 100 && reply.action() == rb::host::kMenuActionWait; ++i) {
        reply = controller.key(rb::host::kMenuPollKey);
    }
    std::array<uint8_t, 40> expected{};
    rb::host::screenLine(" ERROR: SERVIDOR NO ENCONTRADO", Span<uint8_t>{expected});
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.screen() + rb::host::kMainMessage, 40);
    TEST_ASSERT_EQUAL(1, resolver.asked);
    const std::vector<std::string> errors = events.named("menu_tnfs");
    TEST_ASSERT_EQUAL_size_t(1, errors.size());
    TEST_ASSERT_TRUE(errors[0].find("not found") != std::string::npos);
    TEST_ASSERT_TRUE(errors[0].find("no se resuelve") != std::string::npos);
    TEST_ASSERT_TRUE(events.named("tnfs_request").empty());  // nothing was sent
    // Pressing 2 again resolves the name again (the link was never opened).
    reply = controller.key(0x1E);
    for (int i = 0; i < 100 && reply.action() == rb::host::kMenuActionWait; ++i) {
        reply = controller.key(rb::host::kMenuPollKey);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.screen() + rb::host::kMainMessage, 40);
    TEST_ASSERT_EQUAL(2, resolver.asked);
    TEST_ASSERT_EQUAL_size_t(2, events.named("menu_tnfs").size());
}

// The loader thread and the SIO thread share the log: lines never mix.
void test_two_threads_write_whole_lines_to_the_log()
{
    std::FILE* file = std::tmpfile();
    TEST_ASSERT_NOT_NULL(file);
    FakeClock clock;
    rb::host::JsonlLog log{file, clock};
    const std::string detail(300, 'x');
    auto writer = [&log, &detail](const char* name) {
        for (int i = 0; i < 300; ++i) {
            log.event(name, detail.c_str());
        }
    };
    std::thread a(writer, "a");
    std::thread b(writer, "b");
    a.join();
    b.join();
    std::rewind(file);
    char line[1024];
    int count = 0;
    while (std::fgets(line, sizeof line, file) != nullptr) {
        ++count;
        const std::size_t length = std::strlen(line);
        TEST_ASSERT_TRUE(length > 2);
        TEST_ASSERT_EQUAL_CHAR('{', line[0]);
        TEST_ASSERT_EQUAL_STRING("}\n", line + length - 2);
        TEST_ASSERT_TRUE(std::strstr(line, "\"detail\":\"xxxx") != nullptr);
    }
    std::fclose(file);
    TEST_ASSERT_EQUAL(600, count);
}

// Decodes the "detail" string of one JSONL line: \" \ and \uXXXX (as UTF-8).
std::string decodedDetail(const char* line)
{
    const char* p = std::strstr(line, "\"detail\":\"");
    TEST_ASSERT_NOT_NULL(p);
    p += 10;
    std::string out;
    while (*p != '"') {
        TEST_ASSERT_NOT_EQUAL('\0', *p);
        if (*p != '\\') {
            out += *p++;
            continue;
        }
        ++p;
        if (*p != 'u') {
            out += *p++;
            continue;
        }
        const unsigned code = static_cast<unsigned>(std::strtoul(std::string(p + 1, 4).c_str(), nullptr, 16));
        p += 5;
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

std::string loggedDetail(const char* detail)
{
    std::FILE* file = std::tmpfile();
    TEST_ASSERT_NOT_NULL(file);
    FakeClock clock;
    rb::host::JsonlLog log{file, clock};
    log.event("e", detail);
    std::rewind(file);
    char line[1024];
    TEST_ASSERT_NOT_NULL(std::fgets(line, sizeof line, file));
    std::fclose(file);
    return decodedDetail(line);
}

// Review of PR #44: TNFS names are UTF-8 and must read back unchanged.
void test_utf8_names_read_back_unchanged_from_the_log()
{
    const char* path = "source=2 path=/Informaci\xC3\xB3n/Ni\xC3\xB1o \xE2\x82\xAC \xF0\x9F\x8E\xAE.atr \"q\" \\";
    TEST_ASSERT_EQUAL_STRING(path, loggedDetail(path).c_str());
}

// Bytes that are not UTF-8 (an ANSI file name, a cut or overlong sequence, a
// surrogate) still go out as Latin-1 escapes, so the line stays valid UTF-8.
void test_bytes_that_are_not_utf8_are_escaped_as_latin1()
{
    TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", loggedDetail("caf\xE9").c_str());
    TEST_ASSERT_EQUAL_STRING("a\xC3\x83", loggedDetail("a\xC3").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\x80\xC2\xAF", loggedDetail("\xC0\xAF").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xAD\xC2\xA0\xC2\x80", loggedDetail("\xED\xA0\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xA2\xC2\x82x", loggedDetail("\xE2\x82x").c_str());
    // Edges of the table: C1, E0 9F, F0 8F, F4 90 and a bad fourth byte.
    TEST_ASSERT_EQUAL_STRING("\xC3\x81\xC2\x81", loggedDetail("\xC1\x81").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xA0\xC2\x9F\xC2\x80", loggedDetail("\xE0\x9F\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xB0\xC2\x8F\xC2\x80\xC2\x80", loggedDetail("\xF0\x8F\x80\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xB4\xC2\x90\xC2\x80\xC2\x80", loggedDetail("\xF4\x90\x80\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\xC3\xB0\xC2\x9F\xC2\x8E" "x", loggedDetail("\xF0\x9F\x8E" "x").c_str());
}

// The other side of those edges is well formed and goes out as it is.
void test_utf8_at_the_table_edges_reads_back_unchanged()
{
    const char* edges = "\xC2\x80 \xDF\xBF \xE0\xA0\x80 \xED\x9F\xBF \xEE\x80\x80 \xF0\x90\x80\x80 \xF4\x8F\xBF\xBF";
    TEST_ASSERT_EQUAL_STRING(edges, loggedDetail(edges).c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_a_request_is_described_by_command_and_open_by_its_flags_and_mode);
    RUN_TEST(test_the_trace_logs_every_request_of_a_listing_and_a_fetch);
    RUN_TEST(test_a_name_that_does_not_resolve_is_logged_and_shown);
    RUN_TEST(test_two_threads_write_whole_lines_to_the_log);
    RUN_TEST(test_utf8_names_read_back_unchanged_from_the_log);
    RUN_TEST(test_bytes_that_are_not_utf8_are_escaped_as_latin1);
    RUN_TEST(test_utf8_at_the_table_edges_reads_back_unchanged);
    return UNITY_END();
}
