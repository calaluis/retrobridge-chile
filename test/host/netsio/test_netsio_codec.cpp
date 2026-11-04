// NetSIO codec (FEP-003, ADR-016): decoding under both peer profiles and the
// encoders used by the transport. The "hub" datagrams below are the byte
// sequences captured in Issue #32 E0 (route B, Altirra 4.31 + netsiohub).

#include <array>
#include <cstdint>
#include <initializer_list>

#include "rb/netsio/netsio_codec.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
using rb::netsio::decode;
using rb::netsio::Message;
using rb::netsio::PeerProfile;
namespace id = rb::netsio::id;

namespace {

template <std::size_t N>
Result<Message> decode_bytes(const std::array<uint8_t, N>& bytes, PeerProfile profile)
{
    return decode(Span<const uint8_t>{bytes}, profile);
}

void assert_invalid(const Result<Message>& result)
{
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_TRUE(result.error() == ErrorCode::InvalidArgument);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_strict_profile_decodes_fep003_lengths(void)
{
    const std::array<uint8_t, 2> sync{0x18, 0x07};
    const Result<Message> decoded = decode_bytes(sync, PeerProfile::Fep003Strict);
    TEST_ASSERT_TRUE(decoded.ok());
    TEST_ASSERT_EQUAL_HEX8(id::kCommandOffSync, decoded.value().id);
    TEST_ASSERT_EQUAL_size_t(1, decoded.value().payload.size());
    TEST_ASSERT_EQUAL_HEX8(0x07, decoded.value().payload[0]);
    TEST_ASSERT_FALSE(decoded.value().hasTrailer);

    const std::array<uint8_t, 6> block{0x02, 0x31, 0x53, 0x00, 0x00, 0x84};
    const Result<Message> data = decode_bytes(block, PeerProfile::Fep003Strict);
    TEST_ASSERT_TRUE(data.ok());
    TEST_ASSERT_EQUAL_size_t(5, data.value().payload.size());
    TEST_ASSERT_EQUAL_HEX8(0x84, data.value().payload[4]);
}

static void test_strict_profile_rejects_the_hub_trailing_byte(void)
{
    const std::array<uint8_t, 3> sync{0x18, 0xA9, 0x6F};  // E0, route B
    assert_invalid(decode_bytes(sync, PeerProfile::Fep003Strict));
    const std::array<uint8_t, 2> on{0x11, 0x6D};
    assert_invalid(decode_bytes(on, PeerProfile::Fep003Strict));
}

static void test_hub_profile_strips_one_trailing_byte(void)
{
    const std::array<uint8_t, 3> sync{0x18, 0xA9, 0x6F};
    const Result<Message> decoded = decode_bytes(sync, PeerProfile::NetsiohubTrailingByte);
    TEST_ASSERT_TRUE(decoded.ok());
    TEST_ASSERT_EQUAL_HEX8(0xA9, decoded.value().payload[0]);
    TEST_ASSERT_EQUAL_size_t(1, decoded.value().payload.size());
    TEST_ASSERT_TRUE(decoded.value().hasTrailer);
    TEST_ASSERT_EQUAL_HEX8(0x6F, decoded.value().trailer);

    const std::array<uint8_t, 7> block{0x02, 0x31, 0x53, 0x00, 0x00, 0x84, 0x6E};
    const Result<Message> data = decode_bytes(block, PeerProfile::NetsiohubTrailingByte);
    TEST_ASSERT_TRUE(data.ok());
    TEST_ASSERT_EQUAL_size_t(5, data.value().payload.size());
    TEST_ASSERT_EQUAL_HEX8(0x31, data.value().payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x84, data.value().payload[4]);

    const std::array<uint8_t, 6> speed{0x80, 0x60, 0x4A, 0x00, 0x00, 0x6C};
    TEST_ASSERT_TRUE(decode_bytes(speed, PeerProfile::NetsiohubTrailingByte).ok());
    const std::array<uint8_t, 2> reset{0xFF, 0x69};
    TEST_ASSERT_TRUE(decode_bytes(reset, PeerProfile::NetsiohubTrailingByte).ok());
    const std::array<uint8_t, 3> byte{0x01, 0xFF, 0x6B};
    TEST_ASSERT_TRUE(decode_bytes(byte, PeerProfile::NetsiohubTrailingByte).ok());
}

static void test_hub_profile_requires_exactly_one_trailing_byte(void)
{
    const std::array<uint8_t, 2> no_trailer{0x18, 0xA9};
    assert_invalid(decode_bytes(no_trailer, PeerProfile::NetsiohubTrailingByte));
    const std::array<uint8_t, 4> two_extra{0x18, 0xA9, 0x6F, 0x00};
    assert_invalid(decode_bytes(two_extra, PeerProfile::NetsiohubTrailingByte));
    const std::array<uint8_t, 1> bare_on{0x11};
    assert_invalid(decode_bytes(bare_on, PeerProfile::NetsiohubTrailingByte));
    const std::array<uint8_t, 2> empty_block{0x02, 0x6E};  // only the trailing byte: no data
    assert_invalid(decode_bytes(empty_block, PeerProfile::NetsiohubTrailingByte));
}

static void test_hub_generated_messages_never_carry_a_trailer(void)
{
    // C3, C5 and C7 03 as received from the hub in E0.
    const std::array<uint8_t, 1> pong{0xC3};
    const std::array<uint8_t, 1> alive{0xC5};
    const std::array<uint8_t, 2> credit{0xC7, 0x03};
    for (PeerProfile profile : {PeerProfile::Fep003Strict, PeerProfile::NetsiohubTrailingByte}) {
        TEST_ASSERT_TRUE(decode_bytes(pong, profile).ok());
        TEST_ASSERT_TRUE(decode_bytes(alive, profile).ok());
        const Result<Message> decoded = decode_bytes(credit, profile);
        TEST_ASSERT_TRUE(decoded.ok());
        TEST_ASSERT_EQUAL_HEX8(0x03, decoded.value().payload[0]);
        TEST_ASSERT_FALSE(decoded.value().hasTrailer);
    }
}

static void test_rejects_empty_unknown_and_wrong_lengths(void)
{
    assert_invalid(decode(Span<const uint8_t>{}, PeerProfile::Fep003Strict));
    const std::array<uint8_t, 1> unknown{0x77};
    assert_invalid(decode_bytes(unknown, PeerProfile::Fep003Strict));
    const std::array<uint8_t, 1> sync_without_number{0x18};
    assert_invalid(decode_bytes(sync_without_number, PeerProfile::Fep003Strict));
    const std::array<uint8_t, 4> short_speed{0x80, 0x60, 0x4A, 0x00};
    assert_invalid(decode_bytes(short_speed, PeerProfile::Fep003Strict));
    const std::array<uint8_t, 1> empty_block{0x02};
    assert_invalid(decode_bytes(empty_block, PeerProfile::Fep003Strict));
}

static void test_data_block_length_limit(void)
{
    std::array<uint8_t, 1 + 512> largest{};
    largest[0] = id::kDataBlock;
    TEST_ASSERT_TRUE(decode_bytes(largest, PeerProfile::Fep003Strict).ok());
    std::array<uint8_t, 1 + 513> too_large{};
    too_large[0] = id::kDataBlock;
    assert_invalid(decode_bytes(too_large, PeerProfile::Fep003Strict));
    std::array<uint8_t, 1 + 512 + 1> largest_with_trailer{};
    largest_with_trailer[0] = id::kDataBlock;
    TEST_ASSERT_TRUE(decode_bytes(largest_with_trailer, PeerProfile::NetsiohubTrailingByte).ok());
}

static void test_encoders_produce_fep003_bytes(void)
{
    std::array<uint8_t, rb::netsio::kMaxDatagram> out{};
    const Span<uint8_t> buffer{out};

    TEST_ASSERT_EQUAL_size_t(6, rb::netsio::encodeSyncResponse(buffer, 0x51, rb::netsio::ack::kEmpty, 0, 0));
    const uint8_t empty[] = {0x81, 0x51, 0x00, 0x00, 0x00, 0x00};  // the answer validated in E0
    TEST_ASSERT_EQUAL_HEX8_ARRAY(empty, out.data(), 6);

    TEST_ASSERT_EQUAL_size_t(6, rb::netsio::encodeSyncResponse(buffer, 0x07, rb::netsio::ack::kValid, 0x41, 0x0180));
    const uint8_t ack[] = {0x81, 0x07, 0x01, 0x41, 0x80, 0x01};  // write_size LSB first
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ack, out.data(), 6);

    const uint8_t data[] = {0x43, 0x00, 0xFF, 0xE0, 0x00, 0xE0};
    TEST_ASSERT_EQUAL_size_t(7, rb::netsio::encodeDataBlock(buffer, Span<const uint8_t>{data}));
    TEST_ASSERT_EQUAL_HEX8(0x02, out[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out.data() + 1, 6);

    TEST_ASSERT_EQUAL_size_t(2, rb::netsio::encodeCreditStatus(buffer, 0));
    TEST_ASSERT_EQUAL_HEX8(0xC6, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);

    TEST_ASSERT_EQUAL_size_t(1, rb::netsio::encodeSignal(buffer, id::kAliveRequest));
    TEST_ASSERT_EQUAL_HEX8(0xC4, out[0]);
}

static void test_encoded_messages_decode_back(void)
{
    std::array<uint8_t, rb::netsio::kMaxDatagram> out{};
    const std::size_t length = rb::netsio::encodeSyncResponse(Span<uint8_t>{out}, 0x10, 1, 0x4E, 0);
    const Result<Message> decoded = decode(Span<const uint8_t>{out.data(), length}, PeerProfile::Fep003Strict);
    TEST_ASSERT_TRUE(decoded.ok());
    TEST_ASSERT_EQUAL_HEX8(id::kSyncResponse, decoded.value().id);
    TEST_ASSERT_EQUAL_size_t(5, decoded.value().payload.size());
    TEST_ASSERT_FALSE(rb::netsio::fromAtari(id::kSyncResponse));  // device -> Atari: never a trailer
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_strict_profile_decodes_fep003_lengths);
    RUN_TEST(test_strict_profile_rejects_the_hub_trailing_byte);
    RUN_TEST(test_hub_profile_strips_one_trailing_byte);
    RUN_TEST(test_hub_profile_requires_exactly_one_trailing_byte);
    RUN_TEST(test_hub_generated_messages_never_carry_a_trailer);
    RUN_TEST(test_rejects_empty_unknown_and_wrong_lengths);
    RUN_TEST(test_data_block_length_limit);
    RUN_TEST(test_encoders_produce_fep003_bytes);
    RUN_TEST(test_encoded_messages_decode_back);
    return UNITY_END();
}
