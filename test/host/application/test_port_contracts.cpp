// Contract suites for the ImageByteSource and ImageRepository ports, run against
// the host test doubles, plus the death test for closing an attached source.

#include <cstring>

#include "contracts/image_byte_source_contract.hpp"
#include "contracts/image_repository_contract.hpp"
#include "fakes/fake_byte_source.hpp"
#include "fakes/memory_image_repository.hpp"
#include "rb/domain/disk_image.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::app::ImageLocation;
using rb::domain::DiskImage;
using rb::domain::ImageByteSource;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::MemoryImage;
using rb::test::MemoryImageRepository;
using rb::test::RepositoryFixture;
using rb::test::sd_data_size;
using rb::test::SlotByteSource;

namespace {

const std::array<uint8_t, 16> kSdHeader = make_atr_header(sd_data_size(720), 128);
const MemoryImage kSdImage{"sd.atr", kSdHeader, kAtrHeaderSize + sd_data_size(720)};

uint8_t fake_expected(uint32_t offset)
{
    return offset < kAtrHeaderSize ? kSdHeader[offset] : FakeByteSource::data_byte(offset);
}

uint8_t slot_expected(uint32_t offset)
{
    return offset < kAtrHeaderSize ? kSdHeader[offset] : SlotByteSource::data_byte(offset);
}

template <std::size_t Slots>
void with_fixture(void (*contract)(RepositoryFixture&))
{
    MemoryImageRepository<Slots> repository;
    repository.add(kSdImage);
    RepositoryFixture fixture{repository, ImageLocation::from("sd.atr").value(),
                              ImageLocation::from("missing.atr").value(), Slots};
    contract(fixture);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, repository.open_count(), "contract left sources open");
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- ImageByteSource ---------------------------------------------------------

static void test_fake_byte_source_meets_contract(void)
{
    FakeByteSource source{kSdHeader, kAtrHeaderSize + sd_data_size(720)};
    rb::test::check_image_byte_source_contract(source, fake_expected);
}

static void test_repository_slot_source_meets_contract(void)
{
    MemoryImageRepository<1> repository;
    repository.add(kSdImage);
    ImageByteSource* source = repository.open(ImageLocation::from("sd.atr").value()).value();
    rb::test::check_image_byte_source_contract(*source, slot_expected);
    repository.close(source);
}

// --- ImageRepository -----------------------------------------------------------

static void test_missing_location_is_not_found(void)
{
    with_fixture<2>(rb::test::contract_missing_location_is_not_found);
}

static void test_full_repository_reports_no_free_slot(void)
{
    with_fixture<1>(rb::test::contract_full_repository_reports_no_free_slot);
    with_fixture<3>(rb::test::contract_full_repository_reports_no_free_slot);
}

static void test_open_returns_distinct_detached_sources(void)
{
    with_fixture<2>(rb::test::contract_open_returns_distinct_detached_sources);
}

static void test_attached_slot_is_never_reused(void)
{
    with_fixture<2>(rb::test::contract_attached_slot_is_never_reused);
    with_fixture<4>(rb::test::contract_attached_slot_is_never_reused);
}

static void test_close_is_idempotent(void)
{
    with_fixture<2>(rb::test::contract_close_is_idempotent);
}

static void test_closed_slot_is_reusable(void)
{
    with_fixture<1>(rb::test::contract_closed_slot_is_reusable);
}

// Death test: ImageRepository::close() on an attached source must abort
// (RB_ASSERT in the non-virtual base, H2-11). Run by expect_abort.cmake.
static int close_attached_source(void)
{
    MemoryImageRepository<1> repository;
    repository.add(kSdImage);
    ImageByteSource* source = repository.open(ImageLocation::from("sd.atr").value()).value();
    DiskImage image = DiskImage::open(*source).value();
    repository.close(source);  // must not return
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--close-attached-source") == 0) {
        return close_attached_source();
    }

    UNITY_BEGIN();
    RUN_TEST(test_fake_byte_source_meets_contract);
    RUN_TEST(test_repository_slot_source_meets_contract);
    RUN_TEST(test_missing_location_is_not_found);
    RUN_TEST(test_full_repository_reports_no_free_slot);
    RUN_TEST(test_open_returns_distinct_detached_sources);
    RUN_TEST(test_attached_slot_is_never_reused);
    RUN_TEST(test_close_is_idempotent);
    RUN_TEST(test_closed_slot_is_reusable);
    return UNITY_END();
}
