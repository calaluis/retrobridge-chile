// EspFskEngine — RetroBridge adapter (see the header). Written for RetroBridge.
#include "rb/fujinet/esp_fsk_engine.hpp"

#include <new>

#include "bus.h"
#include "cassetteFSK.h"
#include "cassetteFSKLoader.h"
#include "fnio.h"
#include "../lib/include/pinmap.h"

namespace rb::fujinet {

void setDataInPin(int pin) noexcept { setUart2TxPin(pin); }

void setFlushOutput(void (*flush)()) noexcept { SYSTEM_BUS.flush = flush; }

struct EspFskEngine::Impl {
    CassetteFSKLoader loader;
    fnFile file;
    MotorDroppedFn motorDropped = nullptr;
    void* motorCtx = nullptr;
};

namespace {
bool neverDropped(void*) { return false; }
}  // namespace

EspFskEngine::EspFskEngine() noexcept : impl_(new (std::nothrow) Impl()) {}

EspFskEngine::~EspFskEngine()
{
    if (impl_ != nullptr) {
        impl_->loader.stop();
        delete impl_;
    }
}

void EspFskEngine::attach(rb::cassette::TapeSource* source, MotorDroppedFn motorDropped, void* ctx) noexcept
{
    if (impl_ == nullptr) {
        return;
    }
    impl_->loader.stop();
    impl_->file.source = source;
    impl_->file.position = 0;
    impl_->motorDropped = motorDropped != nullptr ? motorDropped : &neverDropped;
    impl_->motorCtx = ctx;
}

void EspFskEngine::stop() noexcept
{
    if (impl_ != nullptr) {
        impl_->loader.stop();
    }
}

FskRunOutcome EspFskEngine::play(uint32_t headerOffset, uint16_t chunkLength)
{
    FskRunOutcome outcome;
    if (impl_ == nullptr || impl_->file.source == nullptr) {
        lastStatus_ = static_cast<uint8_t>(CassetteFSKStatus::allocation_failed);
        return outcome;
    }
    const CassetteFSKPlayResult result =
        cassette_fsk_play_run(&impl_->file, impl_->file.source->size(), headerOffset, chunkLength,
                              impl_->motorDropped, impl_->motorCtx, impl_->loader);
    lastStatus_ = static_cast<uint8_t>(result.status);
    outcome.ok = result.status == CassetteFSKStatus::ok;
    outcome.nextOffset = static_cast<uint32_t>(result.next_offset);
    outcome.waveformMs = result.waveform_ms;
    return outcome;
}

}  // namespace rb::fujinet
