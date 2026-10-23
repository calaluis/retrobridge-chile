#pragma once

// ImageByteSource with simulated latency, standing in for a future remote source
// (TNFS) without any network code. Each read advances the FakeClock by the
// latency and records when it started, so tests can prove the ACK was sent
// before the read began. An optional hook runs at the start of every read, e.g.
// to simulate the storage context submitting a request mid-transaction.

#include <array>
#include <cstdint>

#include "fakes/fake_byte_source.hpp"
#include "fakes/fake_clock.hpp"
#include "rb/domain/image_byte_source.hpp"

namespace rb::test {

class FakeRemoteByteSource final : public rb::domain::ImageByteSource {
public:
    using Hook = void (*)(void* context);

    FakeRemoteByteSource(const std::array<uint8_t, 16>& header, uint32_t size, FakeClock& clock, uint64_t latencyUs)
        : header_(header), size_(size), clock_(clock), latency_us_(latencyUs)
    {
    }

    uint32_t size() const override { return size_; }

    rb::core::Result<void> readAt(uint32_t offset, rb::core::Span<uint8_t> out) override
    {
        ++reads_;
        last_read_started_at_ = clock_.nowMicros();
        if (hook_ != nullptr) {
            hook_(hook_context_);
        }
        clock_.advance(latency_us_);
        if (offset > size_ || out.size() > size_ - offset) {
            return rb::core::Failure{rb::core::ErrorCode::OutOfBounds};
        }
        for (uint32_t i = 0; i < out.size(); ++i) {
            const uint32_t at = offset + i;
            out[i] = at < header_.size() ? header_[at] : FakeByteSource::data_byte(at);
        }
        return {};
    }

    void set_latency(uint64_t latencyUs) { latency_us_ = latencyUs; }
    void set_hook(Hook hook, void* context)
    {
        hook_ = hook;
        hook_context_ = context;
    }
    unsigned reads() const { return reads_; }
    uint64_t last_read_started_at() const { return last_read_started_at_; }

private:
    std::array<uint8_t, 16> header_;
    uint32_t size_;
    FakeClock& clock_;
    uint64_t latency_us_;
    Hook hook_ = nullptr;
    void* hook_context_ = nullptr;
    unsigned reads_ = 0;
    uint64_t last_read_started_at_ = 0;
};

}  // namespace rb::test
