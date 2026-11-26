// FujiTapeRunner — RetroBridge adapter (see the header). Written for RetroBridge.
#include "rb/fujinet/fuji_tape_runner.hpp"

namespace rb::fujinet {

using rb::cassette::DataInLender;
using rb::cassette::LiveLine;

FujiTapeRunner::FujiTapeRunner(rb::cassette::ByteOut& out, const rb::cassette::MotorIn& motor,
                               FskRunPlayer& player, Pause& pause) noexcept
    : out_(out), motor_(motor), player_(player), pause_(pause), loop_(followed_, *this, *this)
{
}

void FujiTapeRunner::lend(DataInLender& lender, LiveLine& line) noexcept
{
    lender_ = &lender;
    line_ = &line;
    live_ = false;  // live only once the SIO task lends DATA IN
}

bool FujiTapeRunner::follow(rb::cassette::TapeSource* tape, uint32_t generation) noexcept
{
    if (tape == followed_.tape && generation == generation_) {
        return false;
    }
    followed_.tape = tape;
    generation_ = generation;
    loop_.rewind();
    enabled_ = false;
    waitMotorOff_ = false;
    stalled_ = false;
    atEnd_ = false;
    records_ = 0;
    phase_ = "gap";
    last_ = FujiStep{};
    return true;
}

bool FujiTapeRunner::tick() noexcept
{
    bool played = false;
    if (followed_.tape != nullptr) {
        if (!motor_.on()) {
            enabled_ = false;  // between records: the tape stays where it is
            waitMotorOff_ = false;
        } else if (live_ && !waitMotorOff_ && !stalled_) {
            // Not live: MOTOR as the tape sees it is off (GatedMotor), and the tape stays where it is.
            stepOnce();
            played = true;
        }
    }
    if (sink_ != nullptr) {
        sink_->publish(!returning_ && atEnd_);  // never while the line may still carry the tape's bytes
    }
    if (lender_ != nullptr) {
        answer();
    }
    return played;
}

void FujiTapeRunner::stepOnce() noexcept
{
    stepping_ = true;
    interruptedStep_ = false;
    if (!enabled_) {
        if (!loop_.enable()) {
            stepping_ = false;
            return;
        }
        enabled_ = true;
        restoreRate_ = false;
    } else if (restoreRate_) {
        if (!setBaud(loop_.baud())) {  // the disk had UART2 at its own rate
            stepping_ = false;
            return;
        }
        restoreRate_ = false;
    }
    last_ = loop_.step();
    stepping_ = false;
    phase_ = "gap";
    switch (last_.kind) {
    case FujiStepKind::Data:
    case FujiStepKind::Fsk:
        ++records_;
        atEnd_ = false;
        break;
    case FujiStepKind::End:
        records_ = 0;
        atEnd_ = true;
        break;
    case FujiStepKind::Replay:
        break;
    }
    if (last_.kind == FujiStepKind::Replay && (interruptedStep_ || stalled_)) {
        return;  // stopped by RetroBridge, not by the tape or MOTOR: the beta's offset-0 rule does not apply
    }
    if (last_.offset == 0) {
        enabled_ = false;  // the beta disables the cassette when a step leaves the tape at its start
        waitMotorOff_ = true;
    }
}

void FujiTapeRunner::answer() noexcept
{
    const DataInLender::Ask ask = lender_->pending();
    if (ask.seq == 0 || ask.seq == answered_) {
        return;
    }
    if (ask.kind == DataInLender::Kind::Take) {
        if (!live_ && !line_->hardwareFree()) {
            return;  // asked again on the next turn
        }
        live_ = true;
        lender_->confirm(ask.seq, DataInLender::Ack::Tape);
        answered_ = ask.seq;
        returning_ = false;
        stalled_ = false;      // a new take: the line is tried again
        unflushed_ = 0;        // the line was free: nothing of the tape is on it
        restoreRate_ = true;   // the disk set its own rate meanwhile
        return;
    }
    if (live_) {
        returning_ = true;  // the line may still carry the tape's bytes
    }
    live_ = false;  // from now on the tape does not move
    switch (line_->giveBack()) {
    case LiveLine::GiveBack::Uart:
        lender_->confirm(ask.seq, DataInLender::Ack::Disk);
        answered_ = ask.seq;
        returning_ = false;
        break;
    case LiveLine::GiveBack::Lost:
        lender_->confirm(ask.seq, DataInLender::Ack::Released);
        answered_ = ask.seq;  // returning_ stays: the line was let go for safety
        break;
    case LiveLine::GiveBack::Busy:
        break;
    }
}

bool FujiTapeRunner::releaseAsked() const noexcept
{
    if (lender_ == nullptr) {
        return false;
    }
    const DataInLender::Ask ask = lender_->pending();
    return ask.seq != 0 && ask.seq != answered_ && ask.kind == DataInLender::Kind::Release;
}

bool FujiTapeRunner::interrupted() noexcept
{
    const bool stop = releaseAsked() || (watch_ != nullptr && watch_->stopWanted());
    if (stop && stepping_) {
        interruptedStep_ = true;
    }
    return stop;
}

uint32_t FujiTapeRunner::drainMs() const noexcept
{
    return baud_ == 0 ? 0 : unflushed_ * 10u * 1000u / baud_ + 1u;  // 10 bits per byte
}

bool FujiTapeRunner::waited(uint32_t& ms, uint32_t limitMs) noexcept
{
    if (interrupted()) {
        return false;
    }
    if (++ms > limitMs) {
        stalled_ = true;  // the line stopped taking: fail closed, nothing more plays
        return false;
    }
    pause_.waitOneMs();
    return true;
}

bool FujiTapeRunner::setBaud(uint32_t baud)
{
    uint32_t ms = 0;
    while (!out_.setBaud(baud)) {
        if (!waited(ms, kStallMs + drainMs())) {
            return false;
        }
    }
    baud_ = baud;
    return true;
}

bool FujiTapeRunner::write(const uint8_t* bytes, uint32_t count)
{
    phase_ = "data";
    while (count > 0) {
        const uint32_t n = count > kSliceBytes ? kSliceBytes : count;
        uint32_t ms = 0;
        while (!out_.send(rb::core::Span<const uint8_t>(bytes, n))) {
            if (!waited(ms, kStallMs + drainMs())) {
                return false;
            }
        }
        unflushed_ += n;
        bytes += n;
        count -= n;
    }
    return true;
}

bool FujiTapeRunner::flushOutput()
{
    uint32_t ms = 0;
    while (!out_.drained()) {
        if (!waited(ms, kStallMs + drainMs())) {
            return false;
        }
    }
    unflushed_ = 0;
    return true;
}

FskRunOutcome FujiTapeRunner::play(uint32_t headerOffset, uint16_t chunkLength)
{
    phase_ = "fsk";
    return player_.play(headerOffset, chunkLength);
}

const char* FujiTapeRunner::phase() const noexcept
{
    if (stalled_) {
        return "line stalled";
    }
    if (lender_ != nullptr && !live_) {
        return "line with the disk";
    }
    return phase_;
}

}  // namespace rb::fujinet
