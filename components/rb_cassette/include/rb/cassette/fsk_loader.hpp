#pragma once

// FskLoader (spec cassette-cas, C103, AC4): loads one FSK run from the tape a little at a
// time while it plays. A producer reads the run into a bank the caller reserved once
// (PSRAM on the ESP32, ADR-011: no allocation here) and publishes how many whole values
// are ready; the consumer plays only published values. The signal starts when 20 s of it
// are loaded, or the whole run.
//
// Adapted from the owner's own code (FujiNet PR #1773, commits 8601cef and 57ca858:
// casFSKLoader fsk_loader_step / FSKPublishedValues / fsk_run_start_gate), under ADR-012
// amendment 1:
//   - one read = at most one 512-byte block, or one chunk header;
//   - chunk payloads are packed back to back, so a value never straddles a read;
//   - the count is published with release, read with acquire; a published byte is never
//     written again;
//   - a read that fails is retried from the same place, 3 times; the 4th failure in a row
//     fails the run and what was published stays valid; a good read restores the 3 retries;
//   - start gate: 20 s of published signal and 1536 values (the encoder's first fill), or
//     the run is final.
// Changed: one contiguous bank instead of a table of allocated blocks; the run's shape comes
// from CasIndex. Pure: no RMT, no tasks, no ESP-IDF.

#include <atomic>
#include <cstdint>

#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/core/span.hpp"

namespace rb::cassette {

enum class LoaderState : uint8_t {
    Idle,     // no run started
    Loading,
    Final,    // the whole run is published
    Failed,   // a read failed 4 times in a row, or the file no longer matches the index
    Stopped,  // requestStop()
};

enum class LoaderStep : uint8_t { Progress, Done, Failed, Stopped };

enum class ValueFetch : uint8_t {
    Value,
    Underrun,  // not loaded yet: the producer is behind
    End,       // past the last value of a final run
};

enum class StartGate : uint8_t {
    Waiting,
    Ready,
    Failed,  // the producer failed or stopped before the gate opened
};

class FskLoader {
public:
    static constexpr uint32_t kBlockBytes = 512;
    static constexpr uint32_t kFaultBudget = 3;
    static constexpr uint64_t kRunwayUs = 20ULL * 1000000ULL;
    static constexpr uint32_t kPrefillValues = 1024 + 512;

    // The bank holds a whole run's payload. Set once; reused for every run.
    void bind(rb::core::Span<uint8_t> bank) noexcept;

    // Prepares the producer for `run` (an Fsk segment from CasIndex over `source`). False if
    // it is not an Fsk run or its payload does not fit the bank. Not thread-safe with step().
    bool start(TapeSource& source, const CasSegment& run, uint64_t runwayUs = kRunwayUs,
               uint32_t prefillValues = kPrefillValues) noexcept;

    // Producer: one read at most. Done once the run is final.
    LoaderStep step() noexcept;

    // Anyone: the producer stops at its next step.
    void requestStop() noexcept;

    // Consumer side (another task may call these while step() runs).
    LoaderState state() const noexcept;
    uint32_t published() const noexcept;  // whole values ready, counted over the run
    ValueFetch next(uint32_t valueIndex, uint16_t& value) const noexcept;
    // Published bytes from `valueIndex` on, for FskWave::setValues. Empty if none.
    rb::core::Span<const uint8_t> piece(uint32_t valueIndex) const noexcept;
    StartGate startGate() const noexcept;

    // Diagnostics.
    uint32_t reads() const noexcept { return reads_; }
    uint32_t faults() const noexcept { return faults_; }

private:
    void setState(LoaderState state) noexcept;
    LoaderStep fault() noexcept;

    rb::core::Span<uint8_t> bank_;
    TapeSource* source_ = nullptr;
    CasSegment run_{};
    uint64_t runwayUs_ = kRunwayUs;
    uint32_t prefillValues_ = kPrefillValues;

    // Producer cursor: advances only past bytes read completely.
    uint32_t chunksDone_ = 0;
    uint32_t chunkPos_ = 0;    // file offset of the next byte to read
    uint32_t chunkLeft_ = 0;   // payload bytes of the current chunk not read yet
    bool inPayload_ = false;   // false: the next read is a chunk header
    uint32_t loaded_ = 0;      // payload bytes in the bank
    uint32_t budget_ = kFaultBudget;
    uint32_t reads_ = 0;
    uint32_t faults_ = 0;

    std::atomic<uint32_t> published_{0};
    std::atomic<uint8_t> state_{static_cast<uint8_t>(LoaderState::Idle)};
    std::atomic<bool> stopRequested_{false};
};

}  // namespace rb::cassette
