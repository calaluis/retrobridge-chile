#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/hwdiag/diag_types.hpp"
#include "rb/hwdiag/diagnostics_recorder.hpp"

namespace rb::hwdiag {

// Where report lines go: the console prints them (UART0); the host tests keep them.
class LineSink {
public:
    virtual void line(const char* text) = 0;

protected:
    LineSink() = default;
    LineSink(const LineSink&) = default;
    LineSink& operator=(const LineSink&) = default;
    ~LineSink() = default;
};

// Incremental CRC-32 (IEEE 802.3, reflected 0xEDB88320, init and final XOR
// 0xFFFFFFFF), bitwise: small and portable, only for report integrity. The
// same value as zlib.crc32, so tools/check_diag_report.py can recompute it.
class Crc32Accumulator {
public:
    void add(const uint8_t* data, std::size_t length) noexcept;
    uint32_t value() const noexcept { return crc_ ^ 0xFFFFFFFFu; }

private:
    uint32_t crc_ = 0xFFFFFFFFu;
};

// Maps an rb::core::ErrorCode value to its name (the console's table).
using ErrorNameFunction = const char* (*)(uint8_t code);

// Formats diagnostics as "DIAG <section> key=value ..." lines with stable keys
// (format 1), so a UART0 capture is reproducible and machine-checkable. Each
// line is built in a fixed buffer: no allocation.
//
// writeReport() frames everything between "DIAG BEGIN" and
// "DIAG END lines=<n> crc32=<hex>", where the CRC covers every line from BEGIN
// up to the line before END, each followed by '\n'.
class ReportWriter {
public:
    static constexpr std::size_t kLineCapacity = 256;  // the longest line (latency) fits with 10-digit values
    static constexpr int kFormatVersion = 1;

    ReportWriter(LineSink& sink, ErrorNameFunction errorName) noexcept : sink_(sink), errorName_(errorName) {}

    void writeStatus(const SystemSnapshot& system, const DiagnosticsRecorder& recorder);
    void writeMemory(const SystemSnapshot& system);
    void writeStorage(const DiagnosticsRecorder& recorder);
    void writeEvents(const DiagnosticsRecorder& recorder);
    void writeReport(const SystemSnapshot& system, const DiagnosticsRecorder& recorder);

private:
    void emit(const char* format, ...) __attribute__((format(printf, 2, 3)));
    const char* errorName(uint8_t code) const;

    LineSink& sink_;
    ErrorNameFunction errorName_;
    std::array<char, kLineCapacity> buffer_{};
    bool framing_ = false;
    uint32_t framedLines_ = 0;
    Crc32Accumulator crc_{};
};

}  // namespace rb::hwdiag
