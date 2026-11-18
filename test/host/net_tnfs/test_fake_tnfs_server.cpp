// FakeTnfsServer (menu-tnfs T003, M41-5). The requests and expected replies are
// written here byte by byte from "The TNFS Protocol" (docs/fuentes.md), without
// TnfsCodec, so a mistake in the codec cannot hide in both ends.

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_tnfs_server.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Span;
using rb::test::FakeClock;
using rb::test::FakeTnfsServer;

namespace {

constexpr uint32_t kWaitUs = 2000000;  // the client's wait per attempt in these tests

using Bytes = std::vector<uint8_t>;

Bytes request(uint8_t c0, uint8_t c1, uint8_t seq, uint8_t cmd, std::initializer_list<uint8_t> body = {},
              const char* path = nullptr)
{
    Bytes bytes{c0, c1, seq, cmd};
    bytes.insert(bytes.end(), body.begin(), body.end());
    if (path != nullptr) {
        for (const char* p = path; *p != '\0'; ++p) {
            bytes.push_back(static_cast<uint8_t>(*p));
        }
        bytes.push_back(0x00);
    }
    return bytes;
}

void send(FakeTnfsServer& server, const Bytes& bytes)
{
    TEST_ASSERT_TRUE(server.send(Span<const uint8_t>{bytes.data(), bytes.size()}).ok());
}

// Receives one datagram; fails the test on Timeout.
Bytes receive(FakeTnfsServer& server, uint32_t waitUs = kWaitUs)
{
    std::array<uint8_t, 1024> buffer{};
    auto got = server.receive(Span<uint8_t>{buffer.data(), buffer.size()}, waitUs);
    TEST_ASSERT_TRUE_MESSAGE(got.ok(), "expected a datagram");
    return Bytes(buffer.begin(), buffer.begin() + got.value());
}

bool times_out(FakeTnfsServer& server, uint32_t waitUs = kWaitUs)
{
    std::array<uint8_t, 1024> buffer{};
    auto got = server.receive(Span<uint8_t>{buffer.data(), buffer.size()}, waitUs);
    return !got.ok() && got.error() == ErrorCode::Timeout;
}

void assert_reply(const Bytes& got, std::initializer_list<uint8_t> expected)
{
    TEST_ASSERT_EQUAL_size_t(expected.size(), got.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.begin(), got.data(), expected.size());
}

Bytes mount_request(uint8_t seq)
{
    Bytes bytes = request(0x00, 0x00, seq, 0x00, {0x02, 0x01}, "/");
    bytes.push_back(0x00);  // user id
    bytes.push_back(0x00);  // password
    return bytes;
}

// MOUNT "/" anonymously, protocol 1.2; returns the session the server gave.
std::array<uint8_t, 2> mount(FakeTnfsServer& server, uint8_t seq = 0x00)
{
    send(server, mount_request(seq));
    Bytes reply = receive(server);
    TEST_ASSERT_TRUE(reply.size() >= 5);
    TEST_ASSERT_EQUAL_HEX8(0x00, reply[4]);
    return {reply[0], reply[1]};
}

std::string dir_entry(const Bytes& reply)
{
    TEST_ASSERT_TRUE(reply.size() >= 6);
    TEST_ASSERT_EQUAL_HEX8(0x00, reply[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, reply.back());
    return std::string(reply.begin() + 5, reply.end() - 1);
}

FakeClock clock_;  // only ever moves forward; tests measure from a starting point

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Session ------------------------------------------------------------------

static void test_mount_replies_session_version_and_min_retry(void)
{
    FakeTnfsServer server(clock_);
    server.setMinRetryMs(1000);
    send(server, mount_request(0x00));
    Bytes reply = receive(server);
    TEST_ASSERT_EQUAL_size_t(9, reply.size());
    TEST_ASSERT_FALSE(reply[0] == 0x00 && reply[1] == 0x00);  // a real session id
    TEST_ASSERT_EQUAL_HEX8(0x00, reply[2]);                     // sequence echoed
    TEST_ASSERT_EQUAL_HEX8(0x00, reply[3]);                     // MOUNT
    TEST_ASSERT_EQUAL_HEX8(0x00, reply[4]);                     // OK
    TEST_ASSERT_EQUAL_HEX8(0x02, reply[5]);                     // version 1.2: minor
    TEST_ASSERT_EQUAL_HEX8(0x01, reply[6]);                     // major
    TEST_ASSERT_EQUAL_HEX8(0xE8, reply[7]);                     // 1000 ms, little endian
    TEST_ASSERT_EQUAL_HEX8(0x03, reply[8]);
}

static void test_unknown_session_and_umount(void)
{
    FakeTnfsServer server(clock_);
    send(server, request(0x12, 0x34, 0x01, 0x10, {}, "/"));
    assert_reply(receive(server), {0x12, 0x34, 0x01, 0x10, 0xFF});  // invalid handle

    auto s = mount(server);
    send(server, request(s[0], s[1], 0x00, 0x01));
    assert_reply(receive(server), {s[0], s[1], 0x00, 0x01, 0x00});
    send(server, request(s[0], s[1], 0x02, 0x10, {}, "/"));
    assert_reply(receive(server), {s[0], s[1], 0x02, 0x10, 0xFF});  // that session is gone
}

// --- The tree -----------------------------------------------------------------

static void test_directory_listing_with_dot_entries_then_eof(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/Juegos/b.atr", Bytes(16, 0x11));
    server.addFile("/Juegos/a.atr", Bytes(16, 0x22));
    server.addDirectory("/Juegos/Sub");
    auto s = mount(server);

    send(server, request(s[0], s[1], 0x01, 0x10, {}, "/Juegos"));
    Bytes opened = receive(server);
    TEST_ASSERT_EQUAL_size_t(6, opened.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, opened[4]);
    const uint8_t handle = opened[5];

    std::vector<std::string> names;
    uint8_t seq = 0x02;
    for (;;) {
        send(server, request(s[0], s[1], seq, 0x11, {handle}));
        Bytes entry = receive(server);
        TEST_ASSERT_EQUAL_HEX8(seq, entry[2]);
        if (entry[4] == 0x21) {
            assert_reply(entry, {s[0], s[1], seq, 0x11, 0x21});  // EOF
            break;
        }
        names.push_back(dir_entry(entry));
        ++seq;
        TEST_ASSERT_TRUE(names.size() < 10);
    }
    TEST_ASSERT_EQUAL_size_t(5, names.size());
    TEST_ASSERT_EQUAL_STRING(".", names[0].c_str());
    TEST_ASSERT_EQUAL_STRING("..", names[1].c_str());
    TEST_ASSERT_EQUAL_STRING("Sub", names[2].c_str());
    TEST_ASSERT_EQUAL_STRING("a.atr", names[3].c_str());
    TEST_ASSERT_EQUAL_STRING("b.atr", names[4].c_str());

    send(server, request(s[0], s[1], 0x09, 0x12, {handle}));
    assert_reply(receive(server), {s[0], s[1], 0x09, 0x12, 0x00});
    send(server, request(s[0], s[1], 0x0A, 0x11, {handle}));
    assert_reply(receive(server), {s[0], s[1], 0x0A, 0x11, 0xFF});  // closed handle

    send(server, request(s[0], s[1], 0x0B, 0x10, {}, "/Nada"));
    assert_reply(receive(server), {s[0], s[1], 0x0B, 0x10, 0x02});  // ENOENT
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
}

static void test_stat_of_file_directory_and_missing(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/x/\xC3\xB1.atr", Bytes(92176, 0x00));  // non-ASCII name, kept as bytes
    auto s = mount(server);

    send(server, request(s[0], s[1], 0x01, 0x24, {}, "/x/\xC3\xB1.atr"));
    Bytes file = receive(server);
    // header, status, mode 2, uid 2, gid 2, size 4, atime 4, mtime 4, ctime 4, two empty strings
    TEST_ASSERT_EQUAL_size_t(5 + 22 + 2, file.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, file[4]);
    TEST_ASSERT_EQUAL_HEX16(0100444, file[5] | (file[6] << 8));  // regular file, read-only
    TEST_ASSERT_EQUAL_UINT32(92176, file[11] | (file[12] << 8) | (file[13] << 16) |
                                        (static_cast<uint32_t>(file[14]) << 24));

    send(server, request(s[0], s[1], 0x02, 0x24, {}, "/x"));
    Bytes dir = receive(server);
    TEST_ASSERT_EQUAL_HEX8(0x00, dir[4]);
    TEST_ASSERT_EQUAL_HEX16(0040555, dir[5] | (dir[6] << 8));

    send(server, request(s[0], s[1], 0x03, 0x24, {}, "/x/no.atr"));
    assert_reply(receive(server), {s[0], s[1], 0x03, 0x24, 0x02});
}

static void test_read_only_open_read_until_eof_and_close(void)
{
    FakeTnfsServer server(clock_);
    Bytes image(700);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<uint8_t>(i * 7);
    }
    server.addFile("/d.atr", image);
    auto s = mount(server);

    send(server, request(s[0], s[1], 0x01, 0x29, {0x01, 0x00, 0x00, 0x00}, "/d.atr"));
    Bytes opened = receive(server);
    TEST_ASSERT_EQUAL_size_t(6, opened.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, opened[4]);
    const uint8_t fd = opened[5];

    send(server, request(s[0], s[1], 0x02, 0x21, {fd, 0x00, 0x02}));  // 512 bytes
    Bytes first = receive(server);
    TEST_ASSERT_EQUAL_size_t(7 + 512, first.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, first[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, first[5]);
    TEST_ASSERT_EQUAL_HEX8(0x02, first[6]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image.data(), first.data() + 7, 512);

    send(server, request(s[0], s[1], 0x03, 0x21, {fd, 0x00, 0x02}));  // the remainder only
    Bytes rest = receive(server);
    TEST_ASSERT_EQUAL_size_t(7 + 188, rest.size());
    TEST_ASSERT_EQUAL_HEX8(188, rest[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, rest[6]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image.data() + 512, rest.data() + 7, 188);

    send(server, request(s[0], s[1], 0x04, 0x21, {fd, 0x00, 0x02}));
    assert_reply(receive(server), {s[0], s[1], 0x04, 0x21, 0x21});  // EOF

    TEST_ASSERT_EQUAL_size_t(1, server.openHandles());
    send(server, request(s[0], s[1], 0x05, 0x23, {fd}));
    assert_reply(receive(server), {s[0], s[1], 0x05, 0x23, 0x00});
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
}

static void test_read_size_is_capped_and_short_reads_are_exact(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", Bytes(2000, 0x5A));
    auto s = mount(server);
    send(server, request(s[0], s[1], 0x01, 0x29, {0x01, 0x00, 0x00, 0x00}, "/d.atr"));
    const uint8_t fd = receive(server)[5];

    send(server, request(s[0], s[1], 0x02, 0x21, {fd, 0xE8, 0x03}));  // asks 1000
    Bytes capped = receive(server);
    TEST_ASSERT_EQUAL_size_t(7 + 512, capped.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, capped[5]);
    TEST_ASSERT_EQUAL_HEX8(0x02, capped[6]);

    send(server, request(s[0], s[1], 0x03, 0x21, {fd, 100, 0x00}));  // asks 100
    Bytes small = receive(server);
    TEST_ASSERT_EQUAL_size_t(7 + 100, small.size());
    TEST_ASSERT_EQUAL_HEX8(100, small[5]);

    // Asking 0 bytes gets OK with count 0: no progress, which the client must handle.
    send(server, request(s[0], s[1], 0x04, 0x21, {fd, 0x00, 0x00}));
    assert_reply(receive(server), {s[0], s[1], 0x04, 0x21, 0x00, 0x00, 0x00});
}

static void test_nothing_can_change_the_tree(void)
{
    FakeTnfsServer server(clock_);
    const Bytes original{1, 2, 3, 4};
    server.addFile("/d.atr", original);
    auto s = mount(server);
    uint8_t seq = 0x01;

    // Well-formed requests that would change the tree, each refused as a read-only
    // file system: MKDIR, RMDIR, WRITE (handle, length, data), UNLINK, CHMOD (mode,
    // path), RENAME (from, to).
    Bytes rename = request(s[0], s[1], 0, 0x28, {}, "/d.atr");
    rename.insert(rename.end(), {'/', 'e', '.', 'a', 't', 'r', 0x00});
    const Bytes mutating[] = {
        request(s[0], s[1], 0, 0x13, {}, "/nuevo"),
        request(s[0], s[1], 0, 0x14, {}, "/"),
        request(s[0], s[1], 0, 0x22, {0x01, 0x02, 0x00, 0x41, 0x42}),
        request(s[0], s[1], 0, 0x26, {}, "/d.atr"),
        request(s[0], s[1], 0, 0x27, {0xFF, 0x01}, "/d.atr"),
        rename,
    };
    for (Bytes bytes : mutating) {
        bytes[2] = seq;
        send(server, bytes);
        assert_reply(receive(server), {s[0], s[1], seq, bytes[3], 0x14});
        ++seq;
    }
    // Any OPEN that is not exactly O_RDONLY: on a new name (write, read/write,
    // create, append, none) and on the existing file with O_TRUNC.
    const uint16_t flags[] = {0x0002, 0x0003, 0x0101, 0x0009, 0x0000};
    for (uint16_t f : flags) {
        send(server, request(s[0], s[1], seq, 0x29,
                             {static_cast<uint8_t>(f & 0xFF), static_cast<uint8_t>(f >> 8), 0xA4, 0x01},
                             "/nuevo.atr"));
        assert_reply(receive(server), {s[0], s[1], seq, 0x29, 0x14});
        ++seq;
    }
    send(server, request(s[0], s[1], seq, 0x29, {0x01, 0x02, 0x00, 0x00}, "/d.atr"));  // O_RDONLY|O_TRUNC
    assert_reply(receive(server), {s[0], s[1], seq, 0x29, 0x14});
    ++seq;

    TEST_ASSERT_FALSE(server.exists("/nuevo.atr"));
    TEST_ASSERT_FALSE(server.exists("/nuevo"));
    TEST_ASSERT_FALSE(server.exists("/e.atr"));
    TEST_ASSERT_TRUE(server.exists("/"));
    Bytes now = server.fileBytes("/d.atr");
    TEST_ASSERT_EQUAL_size_t(original.size(), now.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(original.data(), now.data(), original.size());
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());

    send(server, request(s[0], s[1], seq, 0x7F, {}));  // a code the document does not assign
    assert_reply(receive(server), {s[0], s[1], seq, 0x7F, 0x16});  // ENOSYS
}

// --- The log: every command, and the flags and mode of every OPEN ------------

static void test_log_records_every_command_and_open_flags(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", Bytes(4, 0));
    auto s = mount(server);
    send(server, request(s[0], s[1], 0x01, 0x29, {0x01, 0x00, 0x00, 0x00}, "/d.atr"));
    (void)receive(server);
    // A write open is logged as received and refused: the tree is read-only.
    send(server, request(s[0], s[1], 0x02, 0x29, {0x02, 0x01, 0xA4, 0x01}, "/d.atr"));
    assert_reply(receive(server), {s[0], s[1], 0x02, 0x29, 0x14});  // EROFS
    send(server, request(s[0], s[1], 0x03, 0x22, {0x00, 0x01, 0x00, 0x41}));  // WRITE
    assert_reply(receive(server), {s[0], s[1], 0x03, 0x22, 0x14});

    const auto& log = server.log();
    TEST_ASSERT_EQUAL_size_t(4, log.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, log[0].command);
    TEST_ASSERT_EQUAL_HEX16(0x0102, log[0].version);  // as sent: 1.2
    TEST_ASSERT_EQUAL_STRING("/", log[0].path.c_str());
    TEST_ASSERT_EQUAL_STRING("", log[0].user.c_str());  // anonymous
    TEST_ASSERT_EQUAL_STRING("", log[0].password.c_str());
    TEST_ASSERT_EQUAL_HEX8(s[0], log[1].connection[0]);
    TEST_ASSERT_EQUAL_HEX8(s[1], log[1].connection[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, log[1].sequence);
    TEST_ASSERT_FALSE(log[1].retry);
    TEST_ASSERT_EQUAL_HEX8(0x29, log[1].command);
    TEST_ASSERT_TRUE(log[1].isOpen);
    TEST_ASSERT_EQUAL_HEX16(0x0001, log[1].flags);
    TEST_ASSERT_EQUAL_HEX16(0x0000, log[1].mode);
    TEST_ASSERT_EQUAL_STRING("/d.atr", log[1].path.c_str());
    TEST_ASSERT_EQUAL_HEX16(0x0102, log[2].flags);
    TEST_ASSERT_EQUAL_HEX16(0x01A4, log[2].mode);
    TEST_ASSERT_EQUAL_HEX8(0x22, log[3].command);
    TEST_ASSERT_FALSE(log[3].isOpen);
    TEST_ASSERT_EQUAL_size_t(4, server.fileBytes("/d.atr").size());  // the file is unchanged
}

// --- Programming each command ------------------------------------------------

static void test_delay_applies_to_every_reply_of_a_command(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", Bytes(1024, 0));
    auto s = mount(server);
    server.delay(0x21, 300);  // every READ answers 300 ms later
    send(server, request(s[0], s[1], 0x01, 0x29, {0x01, 0x00, 0x00, 0x00}, "/d.atr"));
    const uint8_t fd = receive(server)[5];

    for (uint8_t i = 0; i < 2; ++i) {
        const uint64_t before = clock_.nowMicros();
        send(server, request(s[0], s[1], static_cast<uint8_t>(0x02 + i), 0x21, {fd, 0x00, 0x02}));
        TEST_ASSERT_TRUE(times_out(server, 200000));  // not yet after 200 ms
        (void)receive(server);
        TEST_ASSERT_EQUAL_UINT64(before + 300000, clock_.nowMicros());
    }
}

static void test_silence_drops_one_reply(void)
{
    FakeTnfsServer server(clock_);
    auto s = mount(server);
    server.silence(0x10);
    send(server, request(s[0], s[1], 0x01, 0x10, {}, "/"));
    const uint64_t before = clock_.nowMicros();
    TEST_ASSERT_TRUE(times_out(server));
    TEST_ASSERT_EQUAL_UINT64(before + kWaitUs, clock_.nowMicros());  // the whole wait elapsed
    send(server, request(s[0], s[1], 0x01, 0x10, {}, "/"));  // the retry is answered
    TEST_ASSERT_EQUAL_HEX8(0x00, receive(server)[4]);
}

static void test_reject_and_malformed_and_eagain(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", Bytes(4, 0));
    auto s = mount(server);

    server.reject(0x29, 0x09);  // EACCES once
    send(server, request(s[0], s[1], 0x01, 0x29, {0x01, 0x00, 0x00, 0x00}, "/d.atr"));
    assert_reply(receive(server), {s[0], s[1], 0x01, 0x29, 0x09});
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());

    server.malformed(0x24);  // ours (same sequence and command), but no status byte
    send(server, request(s[0], s[1], 0x02, 0x24, {}, "/d.atr"));
    assert_reply(receive(server), {s[0], s[1], 0x02, 0x24});

    server.eagain(0x24, 500);  // try again in 500 ms
    send(server, request(s[0], s[1], 0x03, 0x24, {}, "/d.atr"));
    assert_reply(receive(server), {s[0], s[1], 0x03, 0x24, 0x07, 0xF4, 0x01});
    send(server, request(s[0], s[1], 0x03, 0x24, {}, "/d.atr"));
    TEST_ASSERT_EQUAL_HEX8(0x00, receive(server)[4]);  // each programme is used once
}

static void test_forget_session_then_mount_again(void)
{
    FakeTnfsServer server(clock_);
    server.addFile("/d.atr", Bytes(4, 0));
    auto s = mount(server);
    send(server, request(s[0], s[1], 0x01, 0x29, {0x01, 0x00, 0x00, 0x00}, "/d.atr"));
    (void)receive(server);
    TEST_ASSERT_EQUAL_size_t(1, server.openHandles());

    server.forgetSession();  // as if the server had restarted
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    send(server, request(s[0], s[1], 0x02, 0x24, {}, "/d.atr"));
    assert_reply(receive(server), {s[0], s[1], 0x02, 0x24, 0xFF});

    auto again = mount(server, 0x03);
    send(server, request(again[0], again[1], 0x04, 0x24, {}, "/d.atr"));
    TEST_ASSERT_EQUAL_HEX8(0x00, receive(server)[4]);

    server.forgetSession(0x20);  // a server that answers ESTALE instead
    send(server, request(again[0], again[1], 0x05, 0x24, {}, "/d.atr"));
    assert_reply(receive(server), {again[0], again[1], 0x05, 0x24, 0x20});
}

static void test_late_reply_arrives_after_the_retry_is_sent(void)
{
    FakeTnfsServer server(clock_);
    auto s = mount(server);
    server.lateReply(0x10, 2500);  // the first OPENDIR reply comes 2.5 s late, once
    send(server, request(s[0], s[1], 0x01, 0x10, {}, "/"));
    TEST_ASSERT_TRUE(times_out(server));  // 2 s: nothing yet

    send(server, request(s[0], s[1], 0x01, 0x10, {}, "/"));  // the retry: same sequence
    Bytes fresh = receive(server);
    TEST_ASSERT_EQUAL_HEX8(0x01, fresh[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, fresh[4]);
    TEST_ASSERT_TRUE(server.log().back().retry);
    TEST_ASSERT_EQUAL_size_t(1, server.openHandles());  // recognised, not opened twice

    // The client moves on; the stale reply to the first attempt still arrives,
    // half a second later, while it waits for a slower STAT.
    server.delay(0x24, 1000);
    send(server, request(s[0], s[1], 0x02, 0x24, {}, "/"));
    Bytes late = receive(server);
    TEST_ASSERT_EQUAL_HEX8(0x01, late[2]);
    TEST_ASSERT_EQUAL_HEX8(0x10, late[3]);
    TEST_ASSERT_EQUAL_HEX8(0x02, receive(server)[2]);
}

static void test_a_read_retry_returns_the_same_block(void)
{
    FakeTnfsServer server(clock_);
    Bytes image(1024);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<uint8_t>(i / 4);
    }
    server.addFile("/d.atr", image);
    auto s = mount(server);
    send(server, request(s[0], s[1], 0x01, 0x29, {0x01, 0x00, 0x00, 0x00}, "/d.atr"));
    const uint8_t fd = receive(server)[5];

    server.lateReply(0x21, 2500);  // the reply to the first block is late
    send(server, request(s[0], s[1], 0x02, 0x21, {fd, 0x00, 0x02}));
    TEST_ASSERT_TRUE(times_out(server));
    send(server, request(s[0], s[1], 0x02, 0x21, {fd, 0x00, 0x02}));  // retry, same sequence
    Bytes block = receive(server);
    TEST_ASSERT_EQUAL_size_t(7 + 512, block.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image.data(), block.data() + 7, 512);  // block 0, not block 1

    send(server, request(s[0], s[1], 0x03, 0x21, {fd, 0x00, 0x02}));
    Bytes next = receive(server);
    TEST_ASSERT_EQUAL_HEX8(0x03, next[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image.data() + 512, next.data() + 7, 512);  // block 1
    Bytes late = receive(server);  // the late copy of block 0 still arrives, half a second on
    TEST_ASSERT_EQUAL_HEX8(0x02, late[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image.data(), late.data() + 7, 512);
}

static void test_umount_retry_gets_the_same_ok(void)
{
    FakeTnfsServer server(clock_);
    auto s = mount(server);
    server.lateReply(0x01, 2500);
    send(server, request(s[0], s[1], 0x00, 0x01));
    TEST_ASSERT_TRUE(times_out(server));
    send(server, request(s[0], s[1], 0x00, 0x01));  // the session is gone, the retry is still OK
    assert_reply(receive(server), {s[0], s[1], 0x00, 0x01, 0x00});
    TEST_ASSERT_TRUE(server.log().back().retry);
}

static void test_a_retry_after_a_reject_is_marked_and_executed(void)
{
    FakeTnfsServer server(clock_);
    auto s = mount(server);
    server.reject(0x10, 0x09);
    send(server, request(s[0], s[1], 0x01, 0x10, {}, "/"));
    assert_reply(receive(server), {s[0], s[1], 0x01, 0x10, 0x09});
    TEST_ASSERT_FALSE(server.log().back().retry);
    send(server, request(s[0], s[1], 0x01, 0x10, {}, "/"));  // same sequence
    TEST_ASSERT_EQUAL_HEX8(0x00, receive(server)[4]);
    TEST_ASSERT_TRUE(server.log().back().retry);
    TEST_ASSERT_EQUAL_size_t(1, server.openHandles());  // the rejected attempt opened nothing
    send(server, request(s[0], s[1], 0x02, 0x24, {}, "/"));
    (void)receive(server);
    TEST_ASSERT_FALSE(server.log().back().retry);
}

static void test_mount_credentials_are_logged(void)
{
    FakeTnfsServer server(clock_);
    Bytes bytes = request(0x00, 0x00, 0x00, 0x00, {0x02, 0x01}, "/home");
    bytes.insert(bytes.end(), {'u', 0x00, 'p', 0x00});
    send(server, bytes);
    TEST_ASSERT_EQUAL_HEX8(0x00, receive(server)[4]);
    TEST_ASSERT_EQUAL_STRING("/home", server.log()[0].path.c_str());
    TEST_ASSERT_EQUAL_STRING("u", server.log()[0].user.c_str());
    TEST_ASSERT_EQUAL_STRING("p", server.log()[0].password.c_str());
}

static void test_handles_are_never_zero_or_reused_while_open(void)
{
    FakeTnfsServer server(clock_);
    auto s = mount(server);
    std::array<bool, 256> used{};
    uint8_t seq = 0x01;
    for (int i = 0; i < 255; ++i) {
        send(server, request(s[0], s[1], seq++, 0x10, {}, "/"));
        Bytes opened = receive(server);
        TEST_ASSERT_EQUAL_HEX8(0x00, opened[4]);
        TEST_ASSERT_NOT_EQUAL(0, opened[5]);
        TEST_ASSERT_FALSE(used[opened[5]]);
        used[opened[5]] = true;
    }
    send(server, request(s[0], s[1], seq++, 0x10, {}, "/"));
    assert_reply(receive(server), {s[0], s[1], static_cast<uint8_t>(seq - 1), 0x10, 0x0F});  // ENFILE

    send(server, request(s[0], s[1], seq++, 0x12, {0x07}));
    TEST_ASSERT_EQUAL_HEX8(0x00, receive(server)[4]);
    send(server, request(s[0], s[1], seq++, 0x10, {}, "/"));
    Bytes reopened = receive(server);
    TEST_ASSERT_EQUAL_HEX8(0x00, reopened[4]);
    TEST_ASSERT_EQUAL_HEX8(0x07, reopened[5]);  // the only free number
}

static void test_unreachable_fails_one_receive(void)
{
    FakeTnfsServer server(clock_);
    server.unreachable();
    send(server, mount_request(0x00));
    std::array<uint8_t, 1024> buffer{};
    auto got = server.receive(Span<uint8_t>{buffer.data(), buffer.size()}, kWaitUs);
    TEST_ASSERT_FALSE(got.ok());
    TEST_ASSERT_TRUE(got.error() == ErrorCode::TransportError);
    TEST_ASSERT_EQUAL_HEX8(0x00, receive(server)[4]);  // then the server answers again
}

static void test_respond_serves_a_real_socket_without_the_clock(void)
{
    // T008 puts the server behind a UDP socket: respond() answers one request
    // directly and ignores the delays, which the socket side does not simulate.
    FakeTnfsServer server(clock_);
    const uint64_t start = clock_.nowMicros();
    Bytes mountBytes = mount_request(0x07);
    auto replies = server.respond(Span<const uint8_t>{mountBytes.data(), mountBytes.size()});
    TEST_ASSERT_EQUAL_size_t(1, replies.size());
    TEST_ASSERT_EQUAL_HEX8(0x07, replies[0][2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, replies[0][4]);
    TEST_ASSERT_EQUAL_UINT64(start, clock_.nowMicros());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mount_replies_session_version_and_min_retry);
    RUN_TEST(test_unknown_session_and_umount);
    RUN_TEST(test_directory_listing_with_dot_entries_then_eof);
    RUN_TEST(test_stat_of_file_directory_and_missing);
    RUN_TEST(test_read_only_open_read_until_eof_and_close);
    RUN_TEST(test_log_records_every_command_and_open_flags);
    RUN_TEST(test_delay_applies_to_every_reply_of_a_command);
    RUN_TEST(test_silence_drops_one_reply);
    RUN_TEST(test_reject_and_malformed_and_eagain);
    RUN_TEST(test_forget_session_then_mount_again);
    RUN_TEST(test_late_reply_arrives_after_the_retry_is_sent);
    RUN_TEST(test_a_read_retry_returns_the_same_block);
    RUN_TEST(test_umount_retry_gets_the_same_ok);
    RUN_TEST(test_a_retry_after_a_reject_is_marked_and_executed);
    RUN_TEST(test_mount_credentials_are_logged);
    RUN_TEST(test_handles_are_never_zero_or_reused_while_open);
    RUN_TEST(test_unreachable_fails_one_receive);
    RUN_TEST(test_read_size_is_capped_and_short_reads_are_exact);
    RUN_TEST(test_nothing_can_change_the_tree);
    RUN_TEST(test_respond_serves_a_real_socket_without_the_clock);
    return UNITY_END();
}
