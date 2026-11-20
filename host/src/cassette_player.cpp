#include "cassette_player.hpp"

namespace rb::host {

namespace {

constexpr uint64_t kByteCost = CassettePlayer::kBitsPerByte * CassettePlayer::kUsPerSecond;  // in us * baud

}  // namespace

void CassettePlayer::load(const CasTape* tape, rb::core::Span<const uint8_t> bytes) noexcept
{
    tape_ = tape;
    bytes_ = bytes;
    rewind();
}

void CassettePlayer::unload() noexcept
{
    tape_ = nullptr;
    bytes_ = {};
    rewind();
}

void CassettePlayer::rewind() noexcept
{
    block_ = 0;
    phase_ = Phase::Gap;
    phaseUs_ = 0;
    budgetUs_ = 0;
    scaled_ = 0;
    sentInBlock_ = 0;
    sentBaud_ = 0;
}

bool CassettePlayer::atEnd() const noexcept
{
    return tape_ == nullptr || block_ >= tape_->blocks.size();
}

void CassettePlayer::nextBlock() noexcept
{
    ++block_;
    phase_ = Phase::Gap;
    phaseUs_ = 0;
    sentInBlock_ = 0;
}

void CassettePlayer::tick(uint64_t nowUs, TapeLink& link)
{
    const bool motor = link.motorOn();
    // Only time with MOTOR on at both ends counts; a clock that goes back counts nothing.
    const uint64_t delta = haveNow_ && motorWasOn_ && motor && nowUs > lastNowUs_ ? nowUs - lastNowUs_ : 0;
    if (!haveNow_ || nowUs > lastNowUs_) {
        lastNowUs_ = nowUs;
    }
    haveNow_ = true;
    motorWasOn_ = motor;
    if (atEnd() || !motor) {
        return;  // MOTOR off: the stopped time never counts
    }
    budgetUs_ += delta;

    while (!atEnd()) {
        const CasBlock& block = tape_->blocks[block_];
        if (phase_ == Phase::Gap || phase_ == Phase::Silence) {
            const uint64_t length = phase_ == Phase::Gap ? static_cast<uint64_t>(block.gapMs) * 1000 : block.silenceUs;
            const uint64_t need = length - phaseUs_;
            if (budgetUs_ < need) {
                phaseUs_ += budgetUs_;
                budgetUs_ = 0;
                return;
            }
            budgetUs_ -= need;
            phaseUs_ = 0;
            if (phase_ == Phase::Silence) {
                nextBlock();
            } else {
                phase_ = block.isData ? Phase::Bytes : Phase::Silence;
                scaled_ = 0;
            }
            continue;
        }

        // Bytes: the rate first, then as many bytes as the time allows.
        if (sentBaud_ != block.baud) {
            if (!link.setBaud(block.baud).ok()) {
                return;  // try again on the next tick
            }
            sentBaud_ = block.baud;
        }
        scaled_ += budgetUs_ * block.baud;
        budgetUs_ = 0;
        const uint64_t remaining = block.length - sentInBlock_;
        uint64_t count = scaled_ / kByteCost;
        count = count < remaining ? count : remaining;
        count = count < kMaxSend ? count : kMaxSend;
        if (count > 0) {
            const rb::core::Span<const uint8_t> part(bytes_.data() + block.offset + sentInBlock_, static_cast<std::size_t>(count));
            if (!link.sendTapeBytes(part).ok()) {
                // The tape does not move, and the refused time is not owed: no burst afterwards.
                scaled_ = scaled_ < kByteCost ? scaled_ : kByteCost;
                return;
            }
            scaled_ -= count * kByteCost;
            sentInBlock_ += static_cast<uint32_t>(count);
        }
        if (sentInBlock_ < block.length) {
            if (scaled_ >= kByteCost) {
                continue;  // more than one data block's worth is due
            }
            return;
        }
        budgetUs_ = scaled_ / block.baud;  // leftover time carries into the next block
        scaled_ = 0;
        nextBlock();
    }
}

}  // namespace rb::host
