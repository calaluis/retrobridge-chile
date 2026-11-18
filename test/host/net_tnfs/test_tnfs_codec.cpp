// TNFS codec, read-only subset (menu-tnfs T002, ADR-012). Expected bytes come
// from the request and reply layouts of "The TNFS Protocol" (docs/fuentes.md).

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <string>
#include <vector>

#include "rb/net_tnfs/tnfs_codec.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
namespace tnfs = rb::net_tnfs;
using tnfs::ConnectionId;
using tnfs::Reply;
using tnfs::ReplyKind;

namespace {

const ConnectionId kConn{{0xEF, 0xBE}};

std::array<uint8_t, tnfs::kMaxRequest> out{};

void assert_bytes(const Result<std::size_t>& result, std::initializer_list<uint8_t> expected)
{
    TEST_ASSERT_TRUE_MESSAGE(result.ok(), "encoder failed");
    TEST_ASSERT_EQUAL_size_t(expected.size(), result.value());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.begin(), out.data(), expected.size());
}

void assert_error(const Result<std::size_t>& result, ErrorCode code)
{
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_TRUE(result.error() == code);
}

std::deque<std::vector<uint8_t>> parsed;  // deque: push_back never moves existing buffers

Reply parse_bytes(std::initializer_list<uint8_t> bytes, uint8_t command, uint8_t sequence)
{
    // Each datagram keeps its own buffer until the next test, so every Reply body
    // a test holds stays valid however many datagrams it parses after it.
    parsed.emplace_back(bytes.begin(), bytes.end());
    const std::vector<uint8_t>& storage = parsed.back();
    return tnfs::parse(Span<const uint8_t>{storage.data(), storage.size()}, command, sequence);
}

std::string long_path(std::size_t length)
{
    return "/" + std::string(length - 1, 'a');
}

}  // namespace

void setUp(void)
{
    out.fill(0xAA);
    parsed.clear();
}
void tearDown(void) {}

// --- Encoders: the nine read-only requests, byte by byte --------------------

static void test_mount_is_anonymous_version_1_2_with_connection_zero(void)
{
    assert_bytes(tnfs::encodeMount(0x05, "/", Span<uint8_t>{out}),
                 {0x00, 0x00, 0x05, 0x00, 0x02, 0x01, '/', 0x00, 0x00, 0x00});
}

static void test_umount_uses_sequence_zero(void)
{
    assert_bytes(tnfs::encodeUmount(kConn, Span<uint8_t>{out}), {0xEF, 0xBE, 0x00, 0x01});
}

static void test_directory_requests(void)
{
    assert_bytes(tnfs::encodeOpenDir(kConn, 0x11, "/a b", Span<uint8_t>{out}),
                 {0xEF, 0xBE, 0x11, 0x10, '/', 'a', ' ', 'b', 0x00});
    assert_bytes(tnfs::encodeReadDir(kConn, 0x12, 0x04, Span<uint8_t>{out}), {0xEF, 0xBE, 0x12, 0x11, 0x04});
    assert_bytes(tnfs::encodeCloseDir(kConn, 0x13, 0x04, Span<uint8_t>{out}), {0xEF, 0xBE, 0x13, 0x12, 0x04});
}

static void test_stat_keeps_utf8_bytes(void)
{
    // "/ñ" in UTF-8: C3 B1. The path bytes go out exactly as given.
    assert_bytes(tnfs::encodeStat(kConn, 0x20, "/\xC3\xB1", Span<uint8_t>{out}),
                 {0xEF, 0xBE, 0x20, 0x24, '/', 0xC3, 0xB1, 0x00});
}

static void test_open_read_only_has_fixed_flags_and_mode(void)
{
    // Flags O_RDONLY = 0x0001 and mode 0, little endian, then the path.
    assert_bytes(tnfs::encodeOpenReadOnly(kConn, 0x30, "/x.atr", Span<uint8_t>{out}),
                 {0xEF, 0xBE, 0x30, 0x29, 0x01, 0x00, 0x00, 0x00, '/', 'x', '.', 'a', 't', 'r', 0x00});
    TEST_ASSERT_EQUAL_HEX16(0x0001, tnfs::kOpenReadOnlyFlags);
}

static void test_read_and_close(void)
{
    assert_bytes(tnfs::encodeRead(kConn, 0x40, 0x04, 512, Span<uint8_t>{out}),
                 {0xEF, 0xBE, 0x40, 0x21, 0x04, 0x00, 0x02});
    assert_bytes(tnfs::encodeClose(kConn, 0x41, 0x04, Span<uint8_t>{out}), {0xEF, 0xBE, 0x41, 0x23, 0x04});
}

static void test_encoders_reject_bad_paths_and_sizes(void)
{
    assert_error(tnfs::encodeOpenDir(kConn, 1, nullptr, Span<uint8_t>{out}), ErrorCode::InvalidArgument);
    assert_error(tnfs::encodeStat(kConn, 1, "", Span<uint8_t>{out}), ErrorCode::InvalidArgument);
    const std::string longest = long_path(tnfs::kMaxPath);
    const std::string too_long = long_path(tnfs::kMaxPath + 1);
    TEST_ASSERT_TRUE(tnfs::encodeOpenReadOnly(kConn, 1, longest.c_str(), Span<uint8_t>{out}).ok());
    assert_error(tnfs::encodeOpenReadOnly(kConn, 1, too_long.c_str(), Span<uint8_t>{out}), ErrorCode::InvalidArgument);
    assert_error(tnfs::encodeMount(1, too_long.c_str(), Span<uint8_t>{out}), ErrorCode::InvalidArgument);
    assert_error(tnfs::encodeRead(kConn, 1, 4, 0, Span<uint8_t>{out}), ErrorCode::InvalidArgument);
    assert_error(tnfs::encodeRead(kConn, 1, 4, tnfs::kMaxReadSize + 1, Span<uint8_t>{out}),
                 ErrorCode::InvalidArgument);
}

static void test_encoders_report_a_small_buffer(void)
{
    std::array<uint8_t, 4> small{};
    assert_error(tnfs::encodeReadDir(kConn, 1, 4, Span<uint8_t>{small}), ErrorCode::BufferTooSmall);
    assert_error(tnfs::encodeOpenReadOnly(kConn, 1, "/x", Span<uint8_t>{small}), ErrorCode::BufferTooSmall);
    std::array<uint8_t, 9> mount_short{};  // "/" needs 10 bytes
    assert_error(tnfs::encodeMount(1, "/", Span<uint8_t>{mount_short}), ErrorCode::BufferTooSmall);
}

// --- parse: classify what arrived -------------------------------------------

static void test_parse_classifies_reply_foreign_and_malformed(void)
{
    Reply ok = parse_bytes({0xEF, 0xBE, 0x07, 0x10, 0x00, 0x04}, tnfs::cmd::kOpenDir, 0x07);
    TEST_ASSERT_TRUE(ok.kind == ReplyKind::Reply);
    TEST_ASSERT_EQUAL_HEX8(0xEF, ok.connection.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, ok.connection.bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, ok.status);
    TEST_ASSERT_EQUAL_size_t(1, ok.body.size());

    TEST_ASSERT_TRUE(parse_bytes({0xEF, 0xBE, 0x06, 0x10, 0x00, 0x04}, tnfs::cmd::kOpenDir, 0x07).kind ==
                     ReplyKind::Foreign);  // older sequence number: a late reply
    TEST_ASSERT_TRUE(parse_bytes({0xEF, 0xBE, 0x07, 0x11, 0x00}, tnfs::cmd::kOpenDir, 0x07).kind ==
                     ReplyKind::Foreign);  // another command
    TEST_ASSERT_TRUE(parse_bytes({0xEF, 0xBE, 0x07}, tnfs::cmd::kOpenDir, 0x07).kind == ReplyKind::Malformed);
    TEST_ASSERT_TRUE(parse_bytes({0xEF, 0xBE, 0x07, 0x10}, tnfs::cmd::kOpenDir, 0x07).kind ==
                     ReplyKind::Malformed);  // no status byte
    TEST_ASSERT_TRUE(parse_bytes({}, tnfs::cmd::kOpenDir, 0x07).kind == ReplyKind::Malformed);

    std::vector<uint8_t> largest(tnfs::kMaxDatagram, 0);  // exactly the limit is still a reply
    largest[2] = 0x07;
    largest[3] = tnfs::cmd::kRead;
    TEST_ASSERT_TRUE(tnfs::parse(Span<const uint8_t>{largest.data(), largest.size()}, tnfs::cmd::kRead, 0x07).kind ==
                     ReplyKind::Reply);

    std::vector<uint8_t> huge(tnfs::kMaxDatagram + 1, 0);
    huge[2] = 0x07;
    huge[3] = tnfs::cmd::kRead;
    TEST_ASSERT_TRUE(tnfs::parse(Span<const uint8_t>{huge.data(), huge.size()}, tnfs::cmd::kRead, 0x07).kind ==
                     ReplyKind::Malformed);
}

// --- Body decoders -----------------------------------------------------------

static void test_decode_mount(void)
{
    // The document's example: version 2.6, minimum retry 5000 ms (0x1388).
    auto info = tnfs::decodeMount(parse_bytes({0xEF, 0xBE, 0x00, 0x00, 0x00, 0x06, 0x02, 0x88, 0x13}, 0x00, 0x00));
    TEST_ASSERT_TRUE(info.ok());
    TEST_ASSERT_EQUAL_UINT8(2, info.value().versionMajor);
    TEST_ASSERT_EQUAL_UINT8(6, info.value().versionMinor);
    TEST_ASSERT_TRUE(info.value().hasMinRetry);
    TEST_ASSERT_EQUAL_UINT16(5000, info.value().minRetryMs);

    auto no_retry = tnfs::decodeMount(parse_bytes({0xEF, 0xBE, 0x00, 0x00, 0x00, 0x03, 0x01}, 0x00, 0x00));
    TEST_ASSERT_TRUE(no_retry.ok());
    TEST_ASSERT_FALSE(no_retry.value().hasMinRetry);

    // An error status (the document's example 0x1F, version 3.5) is not a mount.
    TEST_ASSERT_FALSE(tnfs::decodeMount(parse_bytes({0x00, 0x00, 0x00, 0x00, 0x1F, 0x05, 0x03}, 0x00, 0x00)).ok());
    TEST_ASSERT_FALSE(tnfs::decodeMount(parse_bytes({0xEF, 0xBE, 0x00, 0x00, 0x00, 0x06}, 0x00, 0x00)).ok());
}

static void test_decode_handle_and_dir_entry(void)
{
    auto handle = tnfs::decodeHandle(parse_bytes({0xEF, 0xBE, 0x01, 0x10, 0x00, 0x04}, 0x10, 0x01));
    TEST_ASSERT_TRUE(handle.ok());
    TEST_ASSERT_EQUAL_UINT8(4, handle.value());
    TEST_ASSERT_FALSE(tnfs::decodeHandle(parse_bytes({0xEF, 0xBE, 0x01, 0x10, 0x00}, 0x10, 0x01)).ok());
    TEST_ASSERT_FALSE(tnfs::decodeHandle(parse_bytes({0xEF, 0xBE, 0x01, 0x10, 0x02, 0x04}, 0x10, 0x01)).ok());

    auto entry = tnfs::decodeDirEntry(parse_bytes({0xEF, 0xBE, 0x19, 0x11, 0x00, 'f', 'o', 'o', 0x00}, 0x11, 0x19));
    TEST_ASSERT_TRUE(entry.ok());
    TEST_ASSERT_EQUAL_size_t(3, entry.value().size());
    TEST_ASSERT_EQUAL_MEMORY("foo", entry.value().data(), 3);
    // No NUL inside the datagram, or an empty name: malformed.
    TEST_ASSERT_FALSE(tnfs::decodeDirEntry(parse_bytes({0xEF, 0xBE, 0x19, 0x11, 0x00, 'f', 'o'}, 0x11, 0x19)).ok());
    TEST_ASSERT_FALSE(tnfs::decodeDirEntry(parse_bytes({0xEF, 0xBE, 0x19, 0x11, 0x00, 0x00}, 0x11, 0x19)).ok());
    // End of directory is a status, not an entry.
    TEST_ASSERT_FALSE(tnfs::decodeDirEntry(parse_bytes({0xEF, 0xBE, 0x1A, 0x11, 0x21}, 0x11, 0x1A)).ok());
}

static void test_decode_read_data(void)
{
    auto data = tnfs::decodeReadData(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x00, 0x03, 0x00, 'a', 'b', 'c'}, 0x21, 0x40),
                                     512);
    TEST_ASSERT_TRUE(data.ok());
    TEST_ASSERT_EQUAL_size_t(3, data.value().size());
    TEST_ASSERT_EQUAL_MEMORY("abc", data.value().data(), 3);
    // OK with count 0 is an empty read, not an error: the document does not forbid it.
    // Telling it apart from progress is the client's job (T005), not the codec's.
    auto empty = tnfs::decodeReadData(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x00, 0x00, 0x00}, 0x21, 0x40), 512);
    TEST_ASSERT_TRUE(empty.ok());
    TEST_ASSERT_EQUAL_size_t(0, empty.value().size());
    // A count equal to what was asked for is accepted.
    auto exact = tnfs::decodeReadData(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x00, 0x03, 0x00, 'a', 'b', 'c'}, 0x21, 0x40),
                                      3);
    TEST_ASSERT_TRUE(exact.ok());
    TEST_ASSERT_EQUAL_size_t(3, exact.value().size());
    // Count larger than the bytes present, or than what was asked for.
    TEST_ASSERT_FALSE(
        tnfs::decodeReadData(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x00, 0x04, 0x00, 'a', 'b', 'c'}, 0x21, 0x40), 512)
            .ok());
    TEST_ASSERT_FALSE(
        tnfs::decodeReadData(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x00, 0x03, 0x00, 'a', 'b', 'c'}, 0x21, 0x40), 2)
            .ok());
    TEST_ASSERT_FALSE(tnfs::decodeReadData(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x00, 0x03}, 0x21, 0x40), 512).ok());
    TEST_ASSERT_FALSE(tnfs::decodeReadData(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x21}, 0x21, 0x40), 512).ok());
}

static void test_decode_stat(void)
{
    // mode 040755 (directory), uid, gid, size 92176 (0x00016810), then times.
    auto dir = tnfs::decodeStat(parse_bytes({0xEF, 0xBE, 0x24, 0x24, 0x00, 0xED, 0x41, 0, 0, 0, 0, 0x10, 0x68, 0x01,
                                             0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                            0x24, 0x24));
    TEST_ASSERT_TRUE(dir.ok());
    TEST_ASSERT_TRUE(dir.value().isDirectory());
    TEST_ASSERT_EQUAL_UINT32(92176, dir.value().size);

    // Only mode and size are required; a regular file (0100644).
    auto file = tnfs::decodeStat(
        parse_bytes({0xEF, 0xBE, 0x24, 0x24, 0x00, 0xA4, 0x81, 0, 0, 0, 0, 0x01, 0x00, 0x00, 0x00}, 0x24, 0x24));
    TEST_ASSERT_TRUE(file.ok());
    TEST_ASSERT_FALSE(file.value().isDirectory());
    TEST_ASSERT_EQUAL_UINT32(1, file.value().size);

    // All four size bytes count, the highest included.
    auto big = tnfs::decodeStat(
        parse_bytes({0xEF, 0xBE, 0x24, 0x24, 0x00, 0xA4, 0x81, 0, 0, 0, 0, 0x10, 0xFF, 0xFF, 0x80}, 0x24, 0x24));
    TEST_ASSERT_TRUE(big.ok());
    TEST_ASSERT_EQUAL_HEX32(0x80FFFF10u, big.value().size);

    TEST_ASSERT_FALSE(
        tnfs::decodeStat(parse_bytes({0xEF, 0xBE, 0x24, 0x24, 0x00, 0xA4, 0x81, 0, 0, 0, 0, 0x01, 0x00}, 0x24, 0x24))
            .ok());
    TEST_ASSERT_FALSE(tnfs::decodeStat(parse_bytes({0xEF, 0xBE, 0x24, 0x24, 0x02}, 0x24, 0x24)).ok());
}

static void test_decode_eagain_delay(void)
{
    auto delay = tnfs::decodeEagainDelay(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x07, 0xF4, 0x01}, 0x21, 0x40));
    TEST_ASSERT_TRUE(delay.ok());
    TEST_ASSERT_EQUAL_UINT16(500, delay.value());
    TEST_ASSERT_FALSE(tnfs::decodeEagainDelay(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x07, 0xF4}, 0x21, 0x40)).ok());
    TEST_ASSERT_FALSE(tnfs::decodeEagainDelay(parse_bytes({0xEF, 0xBE, 0x40, 0x21, 0x00, 0xF4, 0x01}, 0x21, 0x40)).ok());
}

static void test_decoders_refuse_foreign_and_malformed(void)
{
    Reply foreign = parse_bytes({0xEF, 0xBE, 0x01, 0x10, 0x00, 0x04}, 0x10, 0x02);
    TEST_ASSERT_FALSE(tnfs::decodeHandle(foreign).ok());
    Reply malformed = parse_bytes({0xEF}, 0x10, 0x01);
    TEST_ASSERT_FALSE(tnfs::decodeHandle(malformed).ok());
    TEST_ASSERT_FALSE(tnfs::decodeStat(malformed).ok());
    TEST_ASSERT_FALSE(tnfs::decodeMount(malformed).ok());
}

// --- Random datagrams never make a decoder read outside them -----------------
// Each datagram lives in its own exact-size heap block, so an out-of-bounds read
// is caught by AddressSanitizer in the CI build (-DRB_SANITIZE=ON).

static void test_random_datagrams_are_handled_without_reading_outside(void)
{
    uint32_t state = 0x12345678u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<uint8_t>(state >> 24);
    };
    const uint8_t commands[] = {tnfs::cmd::kMount,   tnfs::cmd::kOpenDir, tnfs::cmd::kReadDir,
                                tnfs::cmd::kCloseDir, tnfs::cmd::kRead,    tnfs::cmd::kClose,
                                tnfs::cmd::kStat,    tnfs::cmd::kOpen,    tnfs::cmd::kUmount};
    unsigned replies = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        const std::size_t length = next() % 40;
        std::vector<uint8_t> datagram(length);
        for (auto& byte : datagram) {
            byte = next();
        }
        const uint8_t command = commands[i % 9];
        if (length >= 4 && (i % 3) != 0) {  // most of them match the request
            datagram[2] = 0x55;
            datagram[3] = command;
        }
        if (length >= 5 && (i % 2) == 0) {
            datagram[4] = (i % 4) == 0 ? tnfs::status::kOk : tnfs::status::kEagain;
        }
        const Reply reply = tnfs::parse(Span<const uint8_t>{datagram.data(), datagram.size()}, command, 0x55);
        if (reply.kind == ReplyKind::Reply) {
            ++replies;
            TEST_ASSERT_TRUE(reply.body.size() + 5 == datagram.size());
        }
        (void)tnfs::decodeMount(reply);
        (void)tnfs::decodeHandle(reply);
        auto entry = tnfs::decodeDirEntry(reply);
        if (entry.ok()) {
            TEST_ASSERT_TRUE(entry.value().size() > 0 && entry.value().size() < reply.body.size());
        }
        auto data = tnfs::decodeReadData(reply, 16);
        if (data.ok()) {
            TEST_ASSERT_TRUE(data.value().size() <= 16 && data.value().size() + 2 <= reply.body.size());
        }
        (void)tnfs::decodeStat(reply);
        (void)tnfs::decodeEagainDelay(reply);
    }
    TEST_ASSERT_TRUE(replies > 1000);  // the generator really exercised the decoders
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mount_is_anonymous_version_1_2_with_connection_zero);
    RUN_TEST(test_umount_uses_sequence_zero);
    RUN_TEST(test_directory_requests);
    RUN_TEST(test_stat_keeps_utf8_bytes);
    RUN_TEST(test_open_read_only_has_fixed_flags_and_mode);
    RUN_TEST(test_read_and_close);
    RUN_TEST(test_encoders_reject_bad_paths_and_sizes);
    RUN_TEST(test_encoders_report_a_small_buffer);
    RUN_TEST(test_parse_classifies_reply_foreign_and_malformed);
    RUN_TEST(test_decode_mount);
    RUN_TEST(test_decode_handle_and_dir_entry);
    RUN_TEST(test_decode_read_data);
    RUN_TEST(test_decode_stat);
    RUN_TEST(test_decode_eagain_delay);
    RUN_TEST(test_decoders_refuse_foreign_and_malformed);
    RUN_TEST(test_random_datagrams_are_handled_without_reading_outside);
    return UNITY_END();
}
