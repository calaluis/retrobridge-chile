#pragma once

// FskSymbolFeed (spec cassette-cas, C107): the FSK portions on their way to the RMT.
//
// - push() runs on the cassette loop (one producer); fill() runs in the RMT refill interrupt (one
//   consumer). Lock-free: two atomic indexes over caller-owned storage. No allocation (ADR-011),
//   no I/O, no logging, no virtual calls in fill().
// - fill() packs two portions per symbol, as the owner's own simple-encoder callback
//   (FujiNet PR #1756, cassette_fsk_encode_cb, under ADR-012 amendment 1): an odd last portion
//   ends its symbol with duration1 = 0 and the same level, which also ends the transmission.
//   An empty feed ends it too. Both only when nothing else was written in that call: a call that
//   wrote whole symbols leaves the end for the next one, to give the producer time. The platform
//   starts a new transmission when portions come again.
// - clear() only while the consumer is stopped (the RMT channel disabled).
// - FskSymbol has the RMT symbol's fields; the platform copies them into rmt_symbol_word_t.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "rb/cassette/fsk_wave.hpp"
#include "rb/core/span.hpp"

namespace rb::cassette {

struct FskSymbol {
    uint16_t duration0 = 0;
    uint8_t level0 = 0;
    uint16_t duration1 = 0;
    uint8_t level1 = 0;
};

class FskSymbolFeed {
public:
    // `slots`: one uint32_t per portion; capacity = slots.size().
    explicit FskSymbolFeed(rb::core::Span<uint32_t> slots) noexcept : slots_(slots) {}

    // Producer. False if full or the portion cannot be a symbol half (0 or over 32767 ticks).
    bool push(const FskPortion& portion) noexcept
    {
        if (portion.ticks == 0 || portion.ticks > FskWave::kMaxPortionTicks) {
            return false;
        }
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (tail - head >= capacity()) {
            return false;
        }
        slots_[tail % capacity()] = (portion.level ? kLevelBit : 0u) | portion.ticks;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer. Fills up to out.size() symbols; `done` is set when the feed ran out.
    std::size_t fill(rb::core::Span<FskSymbol> out, bool& done) noexcept
    {
        done = false;
        std::size_t written = 0;
        uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        while (written < out.size()) {
            const uint32_t left = tail - head;
            if (left == 0 || (left == 1 && written > 0)) {
                // Whole symbols written: leave the end for the next call, so a producer that is
                // only a little behind does not cut the transmission.
                done = written == 0;
                break;
            }
            const uint32_t first = slots_[head % capacity()];
            ++head;
            FskSymbol& symbol = out[written++];
            symbol.duration0 = static_cast<uint16_t>(first & kTicksMask);
            symbol.level0 = (first & kLevelBit) != 0 ? 1 : 0;
            if (head == tail) {
                symbol.duration1 = 0;  // odd tail, alone: ends here, level held
                symbol.level1 = symbol.level0;
                done = true;
                break;
            }
            const uint32_t second = slots_[head % capacity()];
            ++head;
            symbol.duration1 = static_cast<uint16_t>(second & kTicksMask);
            symbol.level1 = (second & kLevelBit) != 0 ? 1 : 0;
        }
        head_.store(head, std::memory_order_release);
        return written;
    }

    void clear() noexcept { head_.store(tail_.load(std::memory_order_acquire), std::memory_order_release); }

    uint32_t capacity() const noexcept { return static_cast<uint32_t>(slots_.size()); }
    uint32_t size() const noexcept
    {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }
    bool empty() const noexcept { return size() == 0; }

private:
    static constexpr uint32_t kLevelBit = 0x80000000u;
    static constexpr uint32_t kTicksMask = 0x7FFFu;

    rb::core::Span<uint32_t> slots_;
    std::atomic<uint32_t> head_{0};  // next to read (consumer)
    std::atomic<uint32_t> tail_{0};  // next to write (producer)
};

}  // namespace rb::cassette
