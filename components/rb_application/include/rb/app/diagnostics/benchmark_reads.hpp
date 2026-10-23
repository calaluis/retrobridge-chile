#pragma once

#include <cstdint>

#include "rb/app/diagnostics/latency_stats.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/core/result.hpp"

namespace rb::app {

enum class BenchmarkMode : uint8_t {
    Sequential,  // sectors 1..count in order (count 0: the whole image)
    Random,      // `count` sectors from a fixed-seed generator: reproducible runs
};

struct BenchmarkReport {
    uint32_t openMicros;       // repository open + DiskImage::open (header read)
    uint32_t firstReadMicros;  // first sector read after opening ("cold")
    uint32_t reads;
    uint32_t readErrors;
};

// Storage-context diagnostic for the H3-12 measurements: times every sector
// read with the Clock port and records it in `stats` (fixed storage). Cache
// effects are classified by the caller from the mode and the image geometry.
class BenchmarkReads {
public:
    BenchmarkReads(ImageRepository& repository, Clock& clock) noexcept : repository_(repository), clock_(clock) {}

    rb::core::Result<BenchmarkReport> execute(const ImageLocation& location, BenchmarkMode mode, uint32_t count,
                                              LatencyStats& stats);

private:
    ImageRepository& repository_;
    Clock& clock_;
};

}  // namespace rb::app
