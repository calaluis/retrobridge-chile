#include "rb/app/diagnostics/verify_sectors.hpp"

#include <array>

#include "rb/app/diagnostics/scoped_image.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::app {

using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;
using rb::domain::SectorNumber;

Result<VerifyReport> VerifySectors::execute(const ImageLocation& location, SectorCrcSink& sink)
{
    ScopedImage scoped{repository_};
    const Result<void> opened = scoped.open(location);
    if (!opened.ok()) {
        return Failure{opened.error()};
    }

    std::array<uint8_t, 256> buffer{};
    const uint16_t sectors = scoped.image().geometry().sectorCount();
    VerifyReport report{sectors, 0};
    for (uint32_t n = 1; n <= sectors; ++n) {
        const uint16_t number = static_cast<uint16_t>(n);
        const Result<uint16_t> read =
            scoped.image().readSector(SectorNumber::from(number).value(), Span<uint8_t>{buffer});
        if (read.ok()) {
            sink.onSector(number, read.value(), crc32_(buffer.data(), read.value()));
        } else {
            ++report.readErrors;
            sink.onReadError(number, read.error());
        }
    }
    return report;
}

}  // namespace rb::app
