#pragma once

// StdioTapeFiles (cassette-cas C106): tapes as files, through standard C stdio. On the ESP32
// the path is under the microSD mount point (FATFS through the VFS); on the host, any file.
// Three fixed slots, as TapeFiles requires (mounted, being mounted, leaving). No allocation
// of our own (ADR-011); the C library may buffer the FILE.

#include <cstdint>
#include <cstdio>
#include <mutex>

#include "rb/cassette/cassette_commands.hpp"
#include "rb/cassette/tape_source.hpp"

namespace rb::cassette {

class StdioTapeFiles final : public TapeFiles {
public:
    static constexpr int kSlots = 3;
    static constexpr std::size_t kPathMax = 256;

    // `root` is prefixed to every path ("" for none), e.g. the microSD mount point.
    explicit StdioTapeFiles(const char* root) noexcept : root_(root) {}
    ~StdioTapeFiles();
    StdioTapeFiles(const StdioTapeFiles&) = delete;
    StdioTapeFiles& operator=(const StdioTapeFiles&) = delete;

    TapeSource* open(const char* path) override;
    void close(TapeSource* source) override;

    int openCount() const noexcept;

private:
    class Slot final : public TapeSource {
    public:
        uint32_t size() const override { return size_; }
        bool read(uint32_t offset, rb::core::Span<uint8_t> dst) override;

        std::FILE* file = nullptr;
        uint32_t size_ = 0;
        // One tape is read by two tasks: the player (records) and the loader's producer (FSK
        // runs; a stop takes effect only at its next step). Seek and read stay together.
        std::mutex lock;
    };

    const char* root_;
    Slot slots_[kSlots];
};

}  // namespace rb::cassette
