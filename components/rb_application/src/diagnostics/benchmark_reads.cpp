#include "rb/app/diagnostics/benchmark_reads.hpp"

#include <array>

#include "rb/app/diagnostics/scoped_image.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::app {

using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;
using rb::domain::SectorNumber;

namespace {

constexpr uint32_t kRandomSeed = 0x52425231u;  // fixed: runs are reproducible

uint32_t elapsed(uint64_t from, uint64_t to) noexcept
{
    const uint64_t delta = to - from;
    return delta > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(delta);
}

}  // namespace

Result<BenchmarkReport> BenchmarkReads::execute(const ImageLocation& location, BenchmarkMode mode, uint32_t count,
                                                LatencyStats& stats)
{
    ScopedImage scoped{repository_};
    const uint64_t openStart = clock_.nowMicros();
    const Result<void> opened = scoped.open(location);
    if (!opened.ok()) {
        return Failure{opened.error()};
    }
    BenchmarkReport report{elapsed(openStart, clock_.nowMicros()), 0, 0, 0};

    const uint32_t sectors = scoped.image().geometry().sectorCount();
    const uint32_t total = (mode == BenchmarkMode::Sequential && (count == 0 || count > sectors)) ? sectors : count;
    uint32_t random = kRandomSeed;
    std::array<uint8_t, 256> buffer{};

    for (uint32_t i = 0; i < total; ++i) {
        uint32_t sector = i + 1;
        if (mode == BenchmarkMode::Random) {
            random = random * 1664525u + 1013904223u;  // 32-bit linear congruential generator
            sector = 1 + (random >> 8) % sectors;
        }
        const uint64_t start = clock_.nowMicros();
        const Result<uint16_t> read =
            scoped.image().readSector(SectorNumber::from(static_cast<uint16_t>(sector)).value(), Span<uint8_t>{buffer});
        const uint32_t micros = elapsed(start, clock_.nowMicros());
        if (i == 0) {
            report.firstReadMicros = micros;
        }
        stats.add(micros);
        ++report.reads;
        if (!read.ok()) {
            ++report.readErrors;
        }
    }
    return report;
}

}  // namespace rb::app
