#include "rb/cassette/fsk_wave.hpp"

#include "rb/core/assert.hpp"

namespace rb::cassette {

namespace {

uint16_t le16(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

void FskWave::reset() noexcept
{
    piece_ = rb::core::Span<const uint8_t>();
    at_ = 0;
    index_ = 0;
    remaining_ = 0;
    level_ = false;
}

void FskWave::setValues(rb::core::Span<const uint8_t> le16Values) noexcept
{
    RB_ASSERT((le16Values.size() & 1u) == 0);  // whole values only (CasIndex refuses odd chunks)
    piece_ = le16Values;
    at_ = 0;
}

bool FskWave::next(FskPortion& out) noexcept
{
    while (remaining_ == 0) {
        if (at_ + 2 > piece_.size()) {
            return false;
        }
        const uint16_t value = le16(piece_.data() + at_);
        at_ += 2;
        level_ = (index_ & 1u) != 0;  // even index = space, odd = mark
        ++index_;                     // a 0 value still uses its index
        remaining_ = static_cast<uint32_t>(value) * kTicksPerUnit;
    }
    const uint32_t ticks = remaining_ > kMaxPortionTicks ? kMaxPortionTicks : remaining_;
    remaining_ -= ticks;
    out.level = level_;
    out.ticks = ticks;
    return true;
}

uint64_t FskWave::sumTicks(rb::core::Span<const uint8_t> le16Values) noexcept
{
    uint64_t total = 0;
    for (std::size_t i = 0; i + 1 < le16Values.size(); i += 2) {
        total += static_cast<uint64_t>(le16(le16Values.data() + i)) * kTicksPerUnit;
    }
    return total;
}

}  // namespace rb::cassette
