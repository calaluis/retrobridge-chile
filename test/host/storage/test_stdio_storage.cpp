// StdioImageRepository / StdioByteSource against real files (ADR-015): the
// ImageByteSource and ImageRepository contract suites of H2a, path policy,
// sector reads through DiskImage and I/O errors.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "contracts/image_byte_source_contract.hpp"
#include "contracts/image_repository_contract.hpp"
#include "fakes/fake_byte_source.hpp"
#include "rb/app/image_location.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/storage/stdio_image_repository.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

#ifndef RB_TEST_DATA_DIR
#error "RB_TEST_DATA_DIR must point to a writable directory with a 'sub' folder"
#endif

using rb::app::ImageLocation;
using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
using rb::domain::DiskImage;
using rb::domain::ImageByteSource;
using rb::domain::SectorNumber;
using rb::storage::StdioImageRepository;
using rb::test::dd_data_size;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::RepositoryFixture;
using rb::test::sd_data_size;

namespace {

const std::array<uint8_t, 16> kSdHeader = make_atr_header(sd_data_size(720), 128);
const std::array<uint8_t, 16> kDdHeader = make_atr_header(dd_data_size(720), 256);

uint8_t sd_expected(uint32_t offset)
{
    return offset < kAtrHeaderSize ? kSdHeader[offset] : FakeByteSource::data_byte(offset);
}

void data_path(const char* name, std::array<char, 300>& out)
{
    const int written = std::snprintf(out.data(), out.size(), "%s/%s", RB_TEST_DATA_DIR, name);
    TEST_ASSERT_TRUE_MESSAGE(written > 0 && static_cast<std::size_t>(written) < out.size(),
                             "RB_TEST_DATA_DIR too long for the test's path buffer");
}

// Writes `header` followed by FakeByteSource's deterministic data, `size` bytes in total.
void write_image(const char* name, const std::array<uint8_t, 16>& header, uint32_t size)
{
    std::array<char, 300> path{};
    data_path(name, path);
    std::FILE* file = std::fopen(path.data(), "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path.data());
    for (uint32_t offset = 0; offset < size; ++offset) {
        const uint8_t byte = offset < kAtrHeaderSize ? header[offset] : FakeByteSource::data_byte(offset);
        std::fputc(byte, file);
    }
    std::fclose(file);
}

void write_test_files()
{
    write_image("sd.atr", kSdHeader, kAtrHeaderSize + sd_data_size(720));
    write_image("sub/dd.atr", kDdHeader, kAtrHeaderSize + dd_data_size(720));
    auto bad = kSdHeader;
    bad[0] = 0x00;
    write_image("bad.atr", bad, kAtrHeaderSize + sd_data_size(720));
}

ImageLocation location(const char* text)
{
    return ImageLocation::from(text).value();
}

template <std::size_t Slots>
void with_fixture(void (*contract)(RepositoryFixture&))
{
    StdioImageRepository<Slots> repository{RB_TEST_DATA_DIR};
    RepositoryFixture fixture{repository, location("sd.atr"), location("missing.atr"), Slots};
    contract(fixture);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, repository.open_count(), "contract left files open");
}

DiskImage open_image(StdioImageRepository<2>& repository, const char* name, ImageByteSource*& source)
{
    source = repository.open(location(name)).value();
    Result<DiskImage> image = DiskImage::open(*source);
    TEST_ASSERT_TRUE_MESSAGE(image.ok(), name);
    return std::move(image).value();
}

void check_sector(const std::array<uint8_t, 256>& buffer, uint32_t file_offset, uint16_t length)
{
    for (uint16_t i = 0; i < length; ++i) {
        TEST_ASSERT_EQUAL_HEX8(FakeByteSource::data_byte(file_offset + i), buffer[i]);
    }
}

SectorNumber sector(uint16_t n)
{
    return SectorNumber::from(n).value();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Contract suites over real files ------------------------------------------------

static void test_byte_source_meets_contract(void)
{
    StdioImageRepository<1> repository{RB_TEST_DATA_DIR};
    ImageByteSource* source = repository.open(location("sd.atr")).value();
    TEST_ASSERT_EQUAL_UINT32(kAtrHeaderSize + sd_data_size(720), source->size());
    rb::test::check_image_byte_source_contract(*source, sd_expected);
    repository.close(source);
}

static void test_repository_meets_contract(void)
{
    with_fixture<2>(rb::test::contract_missing_location_is_not_found);
    with_fixture<1>(rb::test::contract_full_repository_reports_no_free_slot);
    with_fixture<3>(rb::test::contract_full_repository_reports_no_free_slot);
    with_fixture<2>(rb::test::contract_open_returns_distinct_detached_sources);
    with_fixture<2>(rb::test::contract_attached_slot_is_never_reused);
    with_fixture<4>(rb::test::contract_attached_slot_is_never_reused);
    with_fixture<2>(rb::test::contract_close_is_idempotent);
    with_fixture<1>(rb::test::contract_closed_slot_is_reusable);
}

// --- Paths -------------------------------------------------------------------------------

static void test_invalid_paths_are_rejected_without_using_a_slot(void)
{
    StdioImageRepository<1> repository{RB_TEST_DATA_DIR};
    TEST_ASSERT_TRUE(repository.open(location("../sd.atr")).error() == ErrorCode::InvalidLocation);
    TEST_ASSERT_TRUE(repository.open(location("/sd.atr")).error() == ErrorCode::InvalidLocation);
    TEST_ASSERT_TRUE(repository.open(location("sub\\dd.atr")).error() == ErrorCode::InvalidLocation);
    TEST_ASSERT_EQUAL_size_t(0, repository.open_count());
    ImageByteSource* source = repository.open(location("sub/dd.atr")).value();  // the only slot is still free
    repository.close(source);
}

static void test_directory_is_not_an_image(void)
{
    StdioImageRepository<1> repository{RB_TEST_DATA_DIR};
    TEST_ASSERT_TRUE(repository.open(location("sub")).error() == ErrorCode::NotFound);
    TEST_ASSERT_EQUAL_size_t(0, repository.open_count());
}

// --- Base path length ------------------------------------------------------------------------

// RB_TEST_DATA_DIR padded with "/." segments up to `length` characters: the same
// folder, spelled long, as a deep build directory or images folder would be.
std::string padded_base(std::size_t length)
{
    std::string base = RB_TEST_DATA_DIR;
    TEST_ASSERT_TRUE(base.size() + 2 <= length);
    if ((length - base.size()) % 2 != 0) {
        base += '/';  // "dir//." still names dir
    }
    while (base.size() < length) {
        base += "/.";
    }
    TEST_ASSERT_EQUAL_size_t(length, base.size());
    return base;
}

static void test_long_base_path_opens_images(void)
{
    const std::string base = padded_base(200);  // longer than 127, within Windows MAX_PATH
    StdioImageRepository<1> repository{base.c_str()};
    TEST_ASSERT_TRUE(repository.baseValid());
    ImageByteSource* source = repository.open(location("sub/dd.atr")).value();
    TEST_ASSERT_EQUAL_UINT32(kAtrHeaderSize + dd_data_size(720), source->size());
    repository.close(source);
}

static void test_too_long_or_empty_base_path_is_an_error_not_an_abort(void)
{
    const std::string tooLong(StdioImageRepository<1>::kMaxBasePath + 1, 'a');
    StdioImageRepository<1> longRepository{tooLong.c_str()};
    TEST_ASSERT_FALSE(longRepository.baseValid());
    TEST_ASSERT_TRUE(longRepository.open(location("sd.atr")).error() == ErrorCode::InvalidArgument);
    TEST_ASSERT_EQUAL_size_t(0, longRepository.open_count());

    StdioImageRepository<1> emptyRepository{""};
    TEST_ASSERT_FALSE(emptyRepository.baseValid());
    TEST_ASSERT_TRUE(emptyRepository.open(location("sd.atr")).error() == ErrorCode::InvalidArgument);
}

// --- Sector reads through the domain -------------------------------------------------------

static void test_reads_128_and_256_byte_sectors_and_the_last_one(void)
{
    StdioImageRepository<2> repository{RB_TEST_DATA_DIR};
    std::array<uint8_t, 256> buffer{};
    ImageByteSource* sd_source = nullptr;
    ImageByteSource* dd_source = nullptr;
    {
        DiskImage sd = open_image(repository, "sd.atr", sd_source);
        TEST_ASSERT_EQUAL_UINT16(128, sd.readSector(sector(1), Span<uint8_t>{buffer}).value());
        check_sector(buffer, kAtrHeaderSize, 128);
        TEST_ASSERT_EQUAL_UINT16(128, sd.readSector(sector(720), Span<uint8_t>{buffer}).value());
        check_sector(buffer, kAtrHeaderSize + 719 * 128, 128);

        DiskImage dd = open_image(repository, "sub/dd.atr", dd_source);
        TEST_ASSERT_EQUAL_UINT16(128, dd.readSector(sector(3), Span<uint8_t>{buffer}).value());
        check_sector(buffer, kAtrHeaderSize + 2 * 128, 128);
        TEST_ASSERT_EQUAL_UINT16(256, dd.readSector(sector(4), Span<uint8_t>{buffer}).value());
        check_sector(buffer, kAtrHeaderSize + 3 * 128, 256);
        TEST_ASSERT_EQUAL_UINT16(256, dd.readSector(sector(720), Span<uint8_t>{buffer}).value());
        check_sector(buffer, kAtrHeaderSize + 3 * 128 + 716 * 256, 256);
        TEST_ASSERT_TRUE(dd.readSector(sector(721), Span<uint8_t>{buffer}).error() == ErrorCode::SectorOutOfRange);
    }  // images destroyed: sources detached
    repository.close(sd_source);
    repository.close(dd_source);
    TEST_ASSERT_EQUAL_size_t(0, repository.open_count());
}

static void test_invalid_atr_is_reported_by_the_domain(void)
{
    StdioImageRepository<1> repository{RB_TEST_DATA_DIR};
    ImageByteSource* source = repository.open(location("bad.atr")).value();
    TEST_ASSERT_TRUE(DiskImage::open(*source).error() == ErrorCode::InvalidAtrHeader);
    TEST_ASSERT_FALSE(source->isAttached());
    repository.close(source);
}

// --- I/O errors ---------------------------------------------------------------------------

static void test_file_truncated_after_open_reports_io_error(void)
{
    write_image("shrinking.atr", kSdHeader, kAtrHeaderSize + sd_data_size(720));
    StdioImageRepository<1> repository{RB_TEST_DATA_DIR};
    ImageByteSource* source = repository.open(location("shrinking.atr")).value();
    const uint32_t size = source->size();  // fixed at open

    write_image("shrinking.atr", kSdHeader, 64);  // another writer truncates the file
    std::array<uint8_t, 128> buffer{};
    TEST_ASSERT_EQUAL_UINT32(size, source->size());
    TEST_ASSERT_TRUE(source->readAt(16 + 700 * 128, Span<uint8_t>{buffer}).error() == ErrorCode::IoError);
    TEST_ASSERT_TRUE(source->readAt(0, Span<uint8_t>{buffer.data(), 16}).ok());  // what remains is readable
    repository.close(source);
}

int main(void)
{
    UNITY_BEGIN();
    write_test_files();
    RUN_TEST(test_byte_source_meets_contract);
    RUN_TEST(test_repository_meets_contract);
    RUN_TEST(test_invalid_paths_are_rejected_without_using_a_slot);
    RUN_TEST(test_directory_is_not_an_image);
    RUN_TEST(test_long_base_path_opens_images);
    RUN_TEST(test_too_long_or_empty_base_path_is_an_error_not_an_abort);
    RUN_TEST(test_reads_128_and_256_byte_sectors_and_the_last_one);
    RUN_TEST(test_invalid_atr_is_reported_by_the_domain);
    RUN_TEST(test_file_truncated_after_open_reports_io_error);
    return UNITY_END();
}
