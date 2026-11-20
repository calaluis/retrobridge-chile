// XexImage::wrap (spec menu-xex, X002): which .xex files are valid, which are
// incompatible with the loader, and the ATR the menu serves in D1 for them.

#include <cstdint>
#include <vector>

#include "unity.h"
#include "xex_image.hpp"
#include "xex_loader.inc"

using rb::core::Span;
using rb::host::ImageBuffer;
using rb::host::XexImage;
using rb::host::XexResult;
using rb::host::XexWrap;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes segment(uint16_t start, const Bytes& data, bool header = true)
{
    Bytes out;
    if (header) {
        out = {0xFF, 0xFF};
    }
    const uint16_t end = static_cast<uint16_t>(start + data.size() - 1);
    out.push_back(static_cast<uint8_t>(start & 0xFF));
    out.push_back(static_cast<uint8_t>(start >> 8));
    out.push_back(static_cast<uint8_t>(end & 0xFF));
    out.push_back(static_cast<uint8_t>(end >> 8));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Bytes operator+(Bytes a, const Bytes& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

XexWrap wrap(const Bytes& xex, ImageBuffer& out)
{
    return XexImage::wrap(Span<const uint8_t>(xex.data(), xex.size()), out);
}

void assert_rejected(const Bytes& xex, XexResult expected)
{
    const uint8_t before[3] = {7, 8, 9};
    ImageBuffer out = ImageBuffer::copyOf(Span<const uint8_t>(before, 3));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(wrap(xex, out).result));
    TEST_ASSERT_EQUAL_UINT32(3, out.size());  // left as it was
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, out.data(), 3);
}

void assert_ok(const Bytes& xex, uint32_t segments)
{
    ImageBuffer out;
    const XexWrap result = wrap(xex, out);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XexResult::Ok), static_cast<uint8_t>(result.result));
    TEST_ASSERT_EQUAL_UINT32(segments, result.segments);
    TEST_ASSERT_TRUE(out.valid());
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_one_segment_becomes_loader_sectors_then_the_xex(void)
{
    const Bytes xex = segment(0x3000, {1, 2, 3}) + segment(0x02E0, {0x00, 0x30});
    ImageBuffer out;
    const XexWrap result = wrap(xex, out);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XexResult::Ok), static_cast<uint8_t>(result.result));
    TEST_ASSERT_EQUAL_UINT32(2, result.segments);
    const uint32_t sectors = kXexLoaderSectors + 1;
    TEST_ASSERT_EQUAL_UINT32(16 + sectors * 128, out.size());
    const uint8_t* atr = out.data();
    const uint32_t paragraphs = sectors * 128 / 16;
    const uint8_t header[7] = {0x96, 0x02, static_cast<uint8_t>(paragraphs & 0xFF),
                               static_cast<uint8_t>(paragraphs >> 8), 0x80, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(header, atr, 7);
    for (uint32_t i = 7; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, atr[i]);
    }
    // The loader block as generated, except the 3 length bytes.
    for (uint32_t i = 0; i < sizeof kXexLoaderBlock; ++i) {
        if (i < kXexLoaderLengthOffset || i >= kXexLoaderLengthOffset + 3) {
            TEST_ASSERT_EQUAL_UINT8(kXexLoaderBlock[i], atr[16 + i]);
        }
    }
    const uint8_t* length = atr + 16 + kXexLoaderLengthOffset;
    TEST_ASSERT_EQUAL_UINT8(xex.size(), length[0]);
    TEST_ASSERT_EQUAL_UINT8(0, length[1]);
    TEST_ASSERT_EQUAL_UINT8(0, length[2]);
    const uint8_t* data = atr + 16 + kXexLoaderSectors * 128;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(xex.data(), data, xex.size());
    for (uint32_t i = static_cast<uint32_t>(xex.size()); i < 128; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, data[i]);
    }
}

void test_the_length_uses_three_bytes_and_whole_sectors(void)
{
    const Bytes xex = segment(0x1000, Bytes(0x8000, 0x55)) + segment(0x9000, Bytes(0x2000, 0x66))
                      + segment(0x1000, Bytes(0x8000, 0x77));
    ImageBuffer out;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XexResult::Ok), static_cast<uint8_t>(wrap(xex, out).result));
    const uint32_t size = static_cast<uint32_t>(xex.size());
    TEST_ASSERT_TRUE(size > 0x10000);
    const uint8_t* length = out.data() + 16 + kXexLoaderLengthOffset;
    TEST_ASSERT_EQUAL_UINT32(size, length[0] | (length[1] << 8) | (length[2] << 16));
    TEST_ASSERT_EQUAL_UINT32(16 + (kXexLoaderSectors + (size + 127) / 128) * 128, out.size());
}

void test_headers_between_segments_are_optional(void)
{
    assert_ok(segment(0x3000, {1}) + segment(0x3001, {2}, false) + segment(0x3002, {3}, true), 3);
}

void test_run_and_init_addresses_are_allowed(void)
{
    assert_ok(segment(0x02E0, {0x00, 0x30, 0x00, 0x30}) + segment(0x3000, {0x60}), 2);
}

void test_a_single_byte_segment_is_valid(void)
{
    assert_ok(segment(0xFFFF, {0x12}), 1);
}

void test_without_ffff_at_the_start_is_invalid(void)
{
    assert_rejected(segment(0x3000, {1, 2}, false), XexResult::Invalid);
    assert_rejected(Bytes{}, XexResult::Invalid);
    assert_rejected(Bytes{0xFF, 0xFF}, XexResult::Invalid);
}

void test_start_after_end_is_invalid(void)
{
    assert_rejected(Bytes{0xFF, 0xFF, 0x01, 0x30, 0x00, 0x30, 0xAA}, XexResult::Invalid);
    // A well-formed segment after it must not hide the error (end - start + 1 would wrap to 0).
    assert_rejected(Bytes{0xFF, 0xFF, 0x01, 0x30, 0x00, 0x30} + segment(0x4000, {1}, false), XexResult::Invalid);
}

void test_cut_data_or_a_cut_header_is_invalid(void)
{
    Bytes cut = segment(0x3000, {1, 2, 3});
    cut.pop_back();
    assert_rejected(cut, XexResult::Invalid);
    assert_rejected(segment(0x3000, {1}) + Bytes{0x00, 0x40, 0x00}, XexResult::Invalid);
    assert_rejected(segment(0x3000, {1}) + Bytes{0xFF, 0xFF}, XexResult::Invalid);
    assert_rejected(segment(0x3000, {1}) + Bytes{0xFF}, XexResult::Invalid);
}

// Deviation 3 (owner, 2026-11-20, rule A): an XMODEM transfer fills the last 128-byte block. After the last
// complete segment, less than 128 bytes are padding when the file is a whole number of blocks and its last byte
// is $1A (the CP/M end-of-file mark). The loader gets the length without them. BACTERION V1 ends in $20 + 29 x $1A.
Bytes padTo128(const Bytes& program, const Bytes& head = {})
{
    Bytes fill = head;
    fill.resize(128 - program.size() % 128, 0x1A);
    return program + fill;
}

void assert_padded(const Bytes& program, const Bytes& padded, uint32_t segments)
{
    TEST_ASSERT_EQUAL_UINT32(0, padded.size() % 128);
    ImageBuffer out;
    const XexWrap result = wrap(padded, out);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XexResult::Ok), static_cast<uint8_t>(result.result));
    TEST_ASSERT_EQUAL_UINT32(segments, result.segments);
    const uint8_t* length = out.data() + 16 + kXexLoaderLengthOffset;
    TEST_ASSERT_EQUAL_UINT32(program.size(), length[0] | (length[1] << 8) | (length[2] << 16));
    const uint8_t* data = out.data() + 16 + sizeof kXexLoaderBlock;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(program.data(), data, program.size());
    const std::size_t sectors = (program.size() + 127) / 128;
    TEST_ASSERT_EACH_EQUAL_UINT8(0, data + program.size(), sectors * 128 - program.size());
    TEST_ASSERT_EQUAL_UINT32(16 + (kXexLoaderSectors + sectors) * 128, out.size());
}

void test_trailing_xmodem_padding_is_ignored(void)
{
    const Bytes program = segment(0x3000, {1, 2}) + segment(0x02E0, {0x00, 0x30});
    assert_padded(program, padTo128(program), 2);
}

void test_padding_may_start_with_another_byte(void)  // the BACTERION V1 case
{
    const Bytes program = segment(0x3000, {1, 2});
    assert_padded(program, padTo128(program, {0x20}), 1);
}

void test_padding_of_1_and_127_bytes(void)
{
    const Bytes one = segment(0x3000, Bytes(121, 0x55));  // 127 bytes
    assert_padded(one, padTo128(one), 1);
    const Bytes many = segment(0x3000, Bytes(123, 0x55));  // 129 bytes
    assert_padded(many, padTo128(many), 1);
}

void test_padding_does_not_hide_an_incompatible_segment(void)
{
    assert_rejected(padTo128(segment(0x0780, {1})), XexResult::Incompatible);
}

void test_without_the_xmodem_shape_the_rest_is_invalid(void)
{
    const Bytes program = segment(0x3000, {1, 2});
    assert_rejected(program + Bytes(29, 0x1A), XexResult::Invalid);              // not a whole block
    assert_rejected(program + Bytes(118, 0x1A) + Bytes{0x00, 0x00}, XexResult::Invalid);  // last byte not $1A
    // The XMODEM shape (128 bytes, last byte $1A), but a real header with cut data after the stray bytes.
    const Bytes stray = program + Bytes{0x1A, 0x1A, 0x1A} + segment(0x4000, Bytes(111, 0x1A));
    TEST_ASSERT_EQUAL_UINT32(128, stray.size());
    assert_rejected(stray, XexResult::Invalid);
}

void test_a_file_cut_at_a_block_edge_is_still_invalid(void)
{
    Bytes cut = segment(0x3000, Bytes(300, 0x55));
    cut.resize(256);
    assert_rejected(cut, XexResult::Invalid);
    // A RUNAD cut inside its data at a block edge, last byte $1A: the shape of fill, but it is a cut segment.
    Bytes runad = segment(0x3000, Bytes(115, 0x55)) + segment(0x02E0, {0x1A, 0x30});
    runad.pop_back();
    TEST_ASSERT_EQUAL_UINT32(128, runad.size());
    assert_rejected(runad, XexResult::Invalid);
}

void test_a_segment_on_the_loader_is_incompatible(void)
{
    assert_rejected(segment(kXexLoaderLow, {0}), XexResult::Incompatible);
    assert_rejected(segment(kXexLoaderHigh, {0}), XexResult::Incompatible);
    assert_rejected(segment(kXexLoaderLow - 1, {0, 0}), XexResult::Incompatible);
    assert_rejected(segment(0x0600, Bytes(0x400, 0)), XexResult::Incompatible);  // covers the whole zone
    assert_rejected(segment(0x3000, {1}) + segment(0x0800, {1}), XexResult::Incompatible);
}

void test_a_segment_on_page_one_is_incompatible(void)
{
    assert_rejected(segment(0x0100, {0}), XexResult::Incompatible);
    assert_rejected(segment(0x01FF, {0}), XexResult::Incompatible);
    assert_rejected(segment(0x00F0, Bytes(0x20, 0)), XexResult::Incompatible);  // zero page into page 1
    assert_ok(segment(0x00FF, {0}) + segment(0x0200, {0}), 2);
}

void test_segments_next_to_the_loader_are_compatible(void)
{
    assert_ok(segment(kXexLoaderLow - 2, {0, 0}) + segment(kXexLoaderHigh + 1, {0}), 2);
}

void test_an_invalid_file_is_invalid_even_if_it_also_touches_the_loader(void)
{
    Bytes xex = segment(0x0700, {1}) + segment(0x3000, {1, 2});
    xex.pop_back();
    assert_rejected(xex, XexResult::Invalid);
}

void test_more_than_65535_sectors_is_too_large(void)
{
    const uint32_t room = (XexImage::kMaxSectors - kXexLoaderSectors) * 128;
    const Bytes big = segment(0x1000, Bytes(0x8000, 0));
    Bytes fits;
    fits.reserve(room + 16);
    uint32_t segments = 0;
    while (fits.size() + big.size() + 7 <= room) {
        fits.insert(fits.end(), big.begin(), big.end());
        ++segments;
    }
    const Bytes last = segment(0x1000, Bytes(room - fits.size() - 6, 0));
    fits.insert(fits.end(), last.begin(), last.end());
    TEST_ASSERT_EQUAL_UINT32(room, fits.size());
    assert_ok(fits, segments + 1);
    fits.push_back(0x00);  // one byte more: one sector more, and the data is cut, but size is checked first
    assert_rejected(fits, XexResult::TooLarge);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_segment_becomes_loader_sectors_then_the_xex);
    RUN_TEST(test_the_length_uses_three_bytes_and_whole_sectors);
    RUN_TEST(test_headers_between_segments_are_optional);
    RUN_TEST(test_run_and_init_addresses_are_allowed);
    RUN_TEST(test_a_single_byte_segment_is_valid);
    RUN_TEST(test_without_ffff_at_the_start_is_invalid);
    RUN_TEST(test_start_after_end_is_invalid);
    RUN_TEST(test_cut_data_or_a_cut_header_is_invalid);
    RUN_TEST(test_trailing_xmodem_padding_is_ignored);
    RUN_TEST(test_padding_may_start_with_another_byte);
    RUN_TEST(test_padding_of_1_and_127_bytes);
    RUN_TEST(test_padding_does_not_hide_an_incompatible_segment);
    RUN_TEST(test_without_the_xmodem_shape_the_rest_is_invalid);
    RUN_TEST(test_a_file_cut_at_a_block_edge_is_still_invalid);
    RUN_TEST(test_a_segment_on_the_loader_is_incompatible);
    RUN_TEST(test_a_segment_on_page_one_is_incompatible);
    RUN_TEST(test_segments_next_to_the_loader_are_compatible);
    RUN_TEST(test_an_invalid_file_is_invalid_even_if_it_also_touches_the_loader);
    RUN_TEST(test_more_than_65535_sectors_is_too_large);
    return UNITY_END();
}
