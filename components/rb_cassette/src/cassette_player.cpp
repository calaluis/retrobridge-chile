#include "rb/cassette/cassette_player.hpp"

namespace rb::cassette {

namespace {

constexpr uint64_t kByteCost = CassettePlayer::kBitsPerByte * CassettePlayer::kUsPerSecond;  // in us * baud

}  // namespace

void CassettePlayer::load(TapeSource& source, rb::core::Span<const CasSegment> segments, WaveOut& wave) noexcept
{
    cutFsk(wave);
    source_ = &source;
    segments_ = segments;
    sentBaud_ = 0;
    fskRestarts_ = 0;
    segment_ = 0;
    enterSegment();
}

void CassettePlayer::unload(WaveOut& wave) noexcept
{
    cutFsk(wave);
    source_ = nullptr;
    segments_ = {};
    segment_ = 0;
    enterSegment();
}

void CassettePlayer::rewind(WaveOut& wave) noexcept
{
    cutFsk(wave);
    sentBaud_ = 0;
    segment_ = 0;
    enterSegment();
}

void CassettePlayer::enterSegment() noexcept
{
    phase_ = PlayerPhase::Gap;
    phaseUs_ = 0;
    budgetUs_ = 0;
    scaled_ = 0;
    sentInRecord_ = 0;
    loaderStarted_ = false;
    valueAt_ = 0;
    pieceOpen_ = false;
    havePortion_ = false;
    blocked_ = false;
}

void CassettePlayer::nextSegment() noexcept
{
    const uint64_t carry = budgetUs_;
    ++segment_;
    enterSegment();
    budgetUs_ = carry;  // leftover tape time goes on into the next segment
}

// Stops a run that is loading or playing: no more signal, DATA IN to idle mark, the
// producer stops. Nothing to do for a record.
void CassettePlayer::cutFsk(WaveOut& wave) noexcept
{
    if (loaderStarted_) {
        loader_.requestStop();
    }
    if (phase_ == PlayerPhase::FskPlay || phase_ == PlayerPhase::FskDrain) {
        wave.stop();
    }
    loaderStarted_ = false;
}

void CassettePlayer::tick(uint64_t nowUs, const MotorIn& motor, ByteOut& bytes, WaveOut& wave) noexcept
{
    const bool on = motor.on();
    // Only time with MOTOR on at both ends counts; a clock that goes back counts nothing.
    const uint64_t delta = haveNow_ && motorWasOn_ && on && nowUs > lastNowUs_ ? nowUs - lastNowUs_ : 0;
    if (!haveNow_ || nowUs > lastNowUs_) {
        lastNowUs_ = nowUs;
    }
    haveNow_ = true;
    const bool dropped = motorWasOn_ && !on;
    motorWasOn_ = on;
    if (!loaded() || atEnd()) {
        return;
    }
    if (dropped && phase_ == PlayerPhase::FskDrain && wave.idle()) {
        // The run had already played to its end: nothing to cut or replay.
        loaderStarted_ = false;
        nextSegment();
    } else if (dropped && (phase_ == PlayerPhase::FskPlay || phase_ == PlayerPhase::FskDrain)) {
        // The signal had started: stop it now and play the run again from its gap (PR #1804).
        restartRun(wave);
        return;
    }
    if (atEnd()) {
        return;
    }

    const CasSegment& first = segments_[segment_];
    if (first.kind == SegmentKind::Fsk && phase_ == PlayerPhase::Gap && !loaderStarted_) {
        // Load during the gap, even with MOTOR off (the owner preloads before playing).
        (void)startLoader(first);
    }
    if (!on || blocked_) {
        return;  // MOTOR off: the stopped time never counts
    }
    budgetUs_ += delta;

    while (!atEnd()) {
        const CasSegment& segment = segments_[segment_];
        if (segment.kind == SegmentKind::Fsk && phase_ == PlayerPhase::Gap && !loaderStarted_ &&
            !startLoader(segment)) {
            phaseUs_ += budgetUs_;  // the gap goes on while the previous producer stops
            budgetUs_ = 0;
            const uint64_t gapUs = static_cast<uint64_t>(segment.gapMs) * 1000u;
            phaseUs_ = phaseUs_ < gapUs ? phaseUs_ : gapUs;
            return;
        }
        switch (phase_) {
        case PlayerPhase::Gap: {
            const uint64_t need = static_cast<uint64_t>(segment.gapMs) * 1000u - phaseUs_;
            if (budgetUs_ < need) {
                phaseUs_ += budgetUs_;
                budgetUs_ = 0;
                return;
            }
            budgetUs_ -= need;
            phaseUs_ = 0;
            phase_ = segment.kind == SegmentKind::Data ? PlayerPhase::Bytes : PlayerPhase::FskWait;
            scaled_ = 0;
            continue;
        }
        case PlayerPhase::Bytes:
            if (!playBytes(segment, bytes)) {
                return;
            }
            nextSegment();
            continue;
        case PlayerPhase::FskWait: {
            budgetUs_ = 0;  // the signal sets its own pace
            const StartGate gate = loader_.startGate();
            if (gate == StartGate::Waiting) {
                return;
            }
            if (gate == StartGate::Failed) {
                // Fail safe, as the owner's: load the same run again.
                loaderStarted_ = false;
                (void)startLoader(segment);
                return;
            }
            wave_.reset();
            phase_ = PlayerPhase::FskPlay;
            continue;
        }
        case PlayerPhase::FskPlay: {
            budgetUs_ = 0;
            const FskProgress progress = playFsk(wave);
            if (progress == FskProgress::More) {
                return;
            }
            if (progress == FskProgress::Failed) {
                // The loader failed after the signal started: fail safe, as the owner's,
                // the same run again from its gap.
                restartRun(wave);
                return;
            }
            phase_ = PlayerPhase::FskDrain;
            continue;
        }
        case PlayerPhase::FskDrain:
            budgetUs_ = 0;
            if (!wave.idle()) {
                return;
            }
            loaderStarted_ = false;
            nextSegment();
            continue;
        case PlayerPhase::End:
            return;
        }
    }
}

void CassettePlayer::changeOutputs(WaveOut& previous, bool previousDrove) noexcept
{
    sentBaud_ = 0;  // the new ByteOut has not been given the rate
    if (!loaded() || atEnd()) {
        return;
    }
    const bool runStarted = phase_ == PlayerPhase::FskPlay || phase_ == PlayerPhase::FskDrain;
    if (!previousDrove) {
        // Nothing of the current segment reached DATA IN: one already begun is played again, whole.
        if (runStarted) {
            restartRun(previous);
        } else if (phase_ == PlayerPhase::Bytes && sentInRecord_ > 0) {
            enterSegment();
        }
        return;
    }
    if (phase_ == PlayerPhase::FskDrain && previous.idle()) {
        // The run already played to its end on the old output: nothing to cut or replay.
        loaderStarted_ = false;
        nextSegment();
    } else if (phase_ == PlayerPhase::FskPlay || phase_ == PlayerPhase::FskDrain) {
        // The signal had started on the old output: stop it there, the whole run again on the new one.
        restartRun(previous);
    }
}

// Sends the record's bytes the clock allows. True once the whole record is out.
bool CassettePlayer::playBytes(const CasSegment& segment, ByteOut& bytes) noexcept
{
    if (sentBaud_ != segment.baud) {
        if (!bytes.setBaud(segment.baud)) {
            return false;  // try again on the next tick
        }
        sentBaud_ = segment.baud;
    }
    scaled_ += budgetUs_ * segment.baud;
    budgetUs_ = 0;
    while (sentInRecord_ < segment.length) {
        const uint64_t remaining = segment.length - sentInRecord_;
        uint64_t count = scaled_ / kByteCost;
        count = count < remaining ? count : remaining;
        count = count < kMaxSend ? count : kMaxSend;
        if (count == 0) {
            return false;
        }
        const rb::core::Span<uint8_t> part(buffer_, static_cast<std::size_t>(count));
        if (!source_->read(segment.offset + sentInRecord_, part) || !bytes.send(part)) {
            // The tape does not move, and the refused time is not owed: no burst afterwards.
            scaled_ = scaled_ < kByteCost ? scaled_ : kByteCost;
            return false;
        }
        scaled_ -= count * kByteCost;
        sentInRecord_ += static_cast<uint32_t>(count);
    }
    budgetUs_ = scaled_ / segment.baud;  // leftover time carries into the next segment
    scaled_ = 0;
    return true;
}

// Starts the loader for `segment` once the previous producer is no longer inside step()
// (FskLoader::start is not thread-safe with step): while a stop is pending the state stays
// Loading, and only the producer moves it on. False while waiting, or if blocked.
bool CassettePlayer::startLoader(const CasSegment& segment) noexcept
{
    if (blocked_) {
        return false;
    }
    if (loader_.state() == LoaderState::Loading) {
        return false;  // the previous run's producer has not stopped yet
    }
    loaderStarted_ = loader_.start(*source_, segment);
    blocked_ = !loaderStarted_;
    return loaderStarted_;
}

void CassettePlayer::restartRun(WaveOut& wave) noexcept
{
    cutFsk(wave);
    ++fskRestarts_;
    enterSegment();
}

// Pushes every published portion WaveOut takes.
CassettePlayer::FskProgress CassettePlayer::playFsk(WaveOut& wave) noexcept
{
    for (;;) {
        if (havePortion_) {
            if (!wave.push(portion_)) {
                return FskProgress::More;  // queue full: the same portion first, next time
            }
            havePortion_ = false;
        }
        if (pieceOpen_) {
            if (wave_.next(portion_)) {
                havePortion_ = true;
                continue;
            }
            pieceOpen_ = false;
        }
        const rb::core::Span<const uint8_t> piece = loader_.piece(valueAt_);
        if (piece.size() == 0) {
            const LoaderState state = loader_.state();
            if (state == LoaderState::Final && valueAt_ >= loader_.published()) {
                return FskProgress::Done;
            }
            if (state == LoaderState::Failed || state == LoaderState::Stopped) {
                return FskProgress::Failed;
            }
            return FskProgress::More;  // the loader is behind
        }
        wave_.setValues(piece);
        valueAt_ += static_cast<uint32_t>(piece.size() / 2u);
        pieceOpen_ = true;
    }
}

}  // namespace rb::cassette
