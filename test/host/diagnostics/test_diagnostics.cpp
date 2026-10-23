// Storage-context diagnostics (H3-6): InspectImage, VerifySectors and
// BenchmarkReads. Each opens its own image through the ImageRepository and
// always closes it, success or failure; none touches the DriveBay.

#include <array>
#include <cstdint>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_remote_byte_source.hpp"
#include "fakes/memory_image_repository.hpp"
#include "rb/app/diagnostics/benchmark_reads.hpp"
#include "rb/app/diagnostics/inspect_image.hpp"
#include "rb/app/diagnostics/verify_sectors.hpp"
#include "rb/app/image_location.hpp"
#include "support/atr_header.hpp"
#include "support/crc32.hpp"
#include "unity.h"

using rb::app::BenchmarkMode;
using rb::app::BenchmarkReads;
using rb::app::BenchmarkReport;
using rb::app::ImageInfo;
using rb::app::ImageLocation;
using rb::app::InspectImage;
using rb::app::LatencyStats;
using rb::app::SectorCrcSink;
using rb::app::VerifyReport;
using rb::app::VerifySectors;
using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::Density;
using rb::test::dd_data_size;
using rb::test::FakeClock;
using rb::test::FakeRemoteByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::MemoryImage;
using rb::test::MemoryImageRepository;
using rb::test::sd_data_size;
using rb::test::SlotByteSource;

namespace {

std::array<uint8_t, 16> bad_header()
{
    auto header = make_atr_header(sd_data_size(720), 128);
    header[1] = 0x00;
    return header;
}

const MemoryImage kSd{"sd.atr", make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
const MemoryImage kDd{"dd.atr", make_atr_header(dd_data_size(720), 256), kAtrHeaderSize + dd_data_size(720)};
const MemoryImage kTrailing{"trailing.atr", make_atr_header(sd_data_size(720), 128),
                            kAtrHeaderSize + sd_data_size(720) + 50};
const MemoryImage kBad{"bad.atr", bad_header(), kAtrHeaderSize + sd_data_size(720)};

ImageLocation location(const char* text)
{
    return ImageLocation::from(text).value();
}

void add_catalog(MemoryImageRepository<1>& repository)
{
    repository.add(kSd);
    repository.add(kDd);
    repository.add(kTrailing);
    repository.add(kBad);
}

// Records what VerifySectors reports, in fixed storage.
class RecordingSink final : public SectorCrcSink {
public:
    void onSector(uint16_t sector, uint16_t length, uint32_t crc) override
    {
        TEST_ASSERT_EQUAL_UINT16(++sectors, sector);  // in order, starting at 1
        lengths[sector] = length;
        crcs[sector] = crc;
    }
    void onReadError(uint16_t sector, ErrorCode error) override
    {
        ++errors;
        last_error_sector = sector;
        last_error = error;
    }

    unsigned sectors = 0;
    unsigned errors = 0;
    uint16_t last_error_sector = 0;
    ErrorCode last_error{};
    std::array<uint16_t, 721> lengths{};
    std::array<uint32_t, 721> crcs{};
};

uint32_t expected_crc(uint32_t file_offset, uint16_t length)
{
    std::array<uint8_t, 256> bytes{};
    for (uint16_t i = 0; i < length; ++i) {
        bytes[i] = SlotByteSource::data_byte(file_offset + i);
    }
    return rb::test::crc32(bytes.data(), length);
}

// Serves one externally owned source (e.g. a FakeRemoteByteSource with latency).
class SingleSourceRepository final : public rb::app::ImageRepository {
public:
    explicit SingleSourceRepository(rb::domain::ImageByteSource& source) : source_(source) {}
    bool open() const { return open_; }

protected:
    rb::core::Result<rb::domain::ImageByteSource*> doOpen(const ImageLocation&) override
    {
        if (open_) {
            return rb::core::Failure{ErrorCode::NoFreeSlot};
        }
        open_ = true;
        return &source_;
    }
    void doClose(rb::domain::ImageByteSource&) override { open_ = false; }

private:
    rb::domain::ImageByteSource& source_;
    bool open_ = false;
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- InspectImage ------------------------------------------------------------------------

static void test_inspect_reports_geometry_and_d8_findings(void)
{
    MemoryImageRepository<1> repository;
    add_catalog(repository);
    InspectImage inspect{repository};

    const ImageInfo dd = inspect.execute(location("dd.atr")).value();
    TEST_ASSERT_EQUAL_UINT32(kDd.size, dd.fileSize);
    TEST_ASSERT_EQUAL_UINT32(720, dd.geometry.sectorCount());
    TEST_ASSERT_TRUE(dd.geometry.density() == Density::Double);
    TEST_ASSERT_FALSE(dd.headerSizeMismatch);

    const ImageInfo trailing = inspect.execute(location("trailing.atr")).value();
    TEST_ASSERT_TRUE(trailing.headerSizeMismatch);
    TEST_ASSERT_EQUAL_UINT32(50, trailing.ignoredTrailingBytes);
    TEST_ASSERT_EQUAL_UINT(0, repository.open_count());
}

static void test_inspect_errors_close_the_image(void)
{
    MemoryImageRepository<1> repository;
    add_catalog(repository);
    InspectImage inspect{repository};
    TEST_ASSERT_TRUE(inspect.execute(location("bad.atr")).error() == ErrorCode::InvalidAtrHeader);
    TEST_ASSERT_TRUE(inspect.execute(location("none.atr")).error() == ErrorCode::NotFound);
    TEST_ASSERT_EQUAL_UINT(0, repository.open_count());
    TEST_ASSERT_EQUAL_UINT(repository.opens(), repository.closes());
}

// --- VerifySectors -------------------------------------------------------------------------

static void test_verify_reports_every_sector_crc(void)
{
    MemoryImageRepository<1> repository;
    add_catalog(repository);
    VerifySectors verify{repository, rb::test::crc32};
    RecordingSink sink;

    const VerifyReport report = verify.execute(location("sd.atr"), sink).value();
    TEST_ASSERT_EQUAL_UINT32(720, report.sectors);
    TEST_ASSERT_EQUAL_UINT32(0, report.readErrors);
    TEST_ASSERT_EQUAL_UINT(720, sink.sectors);
    TEST_ASSERT_EQUAL_UINT16(128, sink.lengths[720]);
    TEST_ASSERT_EQUAL_HEX32(expected_crc(kAtrHeaderSize, 128), sink.crcs[1]);
    TEST_ASSERT_EQUAL_HEX32(expected_crc(kAtrHeaderSize + 719 * 128, 128), sink.crcs[720]);
    TEST_ASSERT_EQUAL_UINT(0, repository.open_count());
}

static void test_verify_uses_the_real_sector_sizes(void)
{
    MemoryImageRepository<1> repository;
    add_catalog(repository);
    VerifySectors verify{repository, rb::test::crc32};
    RecordingSink sink;
    (void)verify.execute(location("dd.atr"), sink).value();
    TEST_ASSERT_EQUAL_UINT16(128, sink.lengths[3]);
    TEST_ASSERT_EQUAL_UINT16(256, sink.lengths[4]);
    TEST_ASSERT_EQUAL_HEX32(expected_crc(kAtrHeaderSize + 3 * 128, 256), sink.crcs[4]);
}

static void test_verify_reports_read_errors_and_continues(void)
{
    FakeClock clock;
    FakeRemoteByteSource source{kSd.header, kSd.size, clock, 0};
    source.fail_reads_after(1 + 10);  // the header and sectors 1-10 succeed, the rest fail
    SingleSourceRepository repository{source};
    VerifySectors verify{repository, rb::test::crc32};
    RecordingSink sink;

    const VerifyReport report = verify.execute(location("x.atr"), sink).value();
    TEST_ASSERT_EQUAL_UINT32(720, report.sectors);
    TEST_ASSERT_EQUAL_UINT32(710, report.readErrors);
    TEST_ASSERT_EQUAL_UINT(10, sink.sectors);
    TEST_ASSERT_EQUAL_UINT(710, sink.errors);
    TEST_ASSERT_EQUAL_UINT16(720, sink.last_error_sector);
    TEST_ASSERT_TRUE(sink.last_error == ErrorCode::IoError);
    TEST_ASSERT_FALSE(repository.open());
}

// --- BenchmarkReads ------------------------------------------------------------------------

static void test_sequential_benchmark_measures_every_read(void)
{
    FakeClock clock;
    FakeRemoteByteSource source{kSd.header, kSd.size, clock, 2000};  // 2 ms per read
    SingleSourceRepository repository{source};
    BenchmarkReads bench{repository, clock};
    std::array<uint32_t, 64> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};

    const BenchmarkReport report = bench.execute(location("x.atr"), BenchmarkMode::Sequential, 10, stats).value();
    TEST_ASSERT_EQUAL_UINT32(2000, report.openMicros);       // the header read
    TEST_ASSERT_EQUAL_UINT32(2000, report.firstReadMicros);
    TEST_ASSERT_EQUAL_UINT32(10, report.reads);
    TEST_ASSERT_EQUAL_UINT32(0, report.readErrors);
    const auto summary = stats.summarize();
    TEST_ASSERT_EQUAL_UINT32(10, summary.count);
    TEST_ASSERT_EQUAL_UINT32(2000, summary.min);
    TEST_ASSERT_EQUAL_UINT32(2000, summary.max);
    TEST_ASSERT_FALSE(repository.open());  // closed afterwards
}

static void test_sequential_benchmark_with_zero_count_reads_the_whole_image(void)
{
    FakeClock clock;
    FakeRemoteByteSource source{kSd.header, kSd.size, clock, 1};
    SingleSourceRepository repository{source};
    BenchmarkReads bench{repository, clock};
    std::array<uint32_t, 1024> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    TEST_ASSERT_EQUAL_UINT32(720, bench.execute(location("x.atr"), BenchmarkMode::Sequential, 0, stats).value().reads);
}

static void test_random_benchmark_is_deterministic_and_in_range(void)
{
    FakeClock clock;
    FakeRemoteByteSource source{kSd.header, kSd.size, clock, 100};
    SingleSourceRepository repository{source};
    BenchmarkReads bench{repository, clock};
    std::array<uint32_t, 256> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};

    const BenchmarkReport first = bench.execute(location("x.atr"), BenchmarkMode::Random, 200, stats).value();
    TEST_ASSERT_EQUAL_UINT32(200, first.reads);
    TEST_ASSERT_EQUAL_UINT32(0, first.readErrors);  // every random sector existed
    TEST_ASSERT_EQUAL_UINT32(200, stats.summarize().count);
}

static void test_benchmark_open_failure_is_reported(void)
{
    MemoryImageRepository<1> repository;
    add_catalog(repository);
    FakeClock clock;
    BenchmarkReads bench{repository, clock};
    std::array<uint32_t, 4> storage{};
    LatencyStats stats{Span<uint32_t>{storage}};
    TEST_ASSERT_TRUE(bench.execute(location("bad.atr"), BenchmarkMode::Sequential, 1, stats).error() ==
                     ErrorCode::InvalidAtrHeader);
    TEST_ASSERT_EQUAL_UINT(0, repository.open_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_inspect_reports_geometry_and_d8_findings);
    RUN_TEST(test_inspect_errors_close_the_image);
    RUN_TEST(test_verify_reports_every_sector_crc);
    RUN_TEST(test_verify_uses_the_real_sector_sizes);
    RUN_TEST(test_verify_reports_read_errors_and_continues);
    RUN_TEST(test_sequential_benchmark_measures_every_read);
    RUN_TEST(test_sequential_benchmark_with_zero_count_reads_the_whole_image);
    RUN_TEST(test_random_benchmark_is_deterministic_and_in_range);
    RUN_TEST(test_benchmark_open_failure_is_reported);
    return UNITY_END();
}
