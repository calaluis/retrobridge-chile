#include "rb/cassette/cassette_tick.hpp"

namespace rb::cassette {

void CassetteTick::tick(uint64_t nowUs) noexcept
{
    player_.tick(nowUs, motor_, outputs_.bytes(), outputs_.wave());
    if (line_ != nullptr) {
        line_->pump();
    }
    if (sink_ != nullptr) {
        // The outputs of this tick; never the discard outputs while the live line still holds tape bytes.
        sink_->publish(!returning_ && player_.ended(outputs_.bytes(), outputs_.wave()));
    }
    if (lender_ != nullptr) {
        answer();
    }
}

void CassetteTick::answer() noexcept
{
    const DataInLender::Ask ask = lender_->pending();
    if (ask.seq == 0 || ask.seq == answered_) {
        return;
    }
    if (ask.kind == DataInLender::Kind::Take) {
        if (!outputs_.live() && !line_->hardwareFree()) {
            return;  // step 3: asked again on the next tick
        }
        if (!outputs_.select(true, player_)) {
            return;  // no live outputs: never confirmed (the SIO task's take expires)
        }
        lender_->confirm(ask.seq, DataInLender::Ack::Tape);
        answered_ = ask.seq;
        return;
    }
    if (outputs_.live()) {
        returning_ = true;  // the live line may still hold the tape's bytes
    }
    (void)outputs_.select(false, player_);  // step 6: the player discards from the next tick on
    switch (line_->giveBack()) {
    case LiveLine::GiveBack::Uart:
        lender_->confirm(ask.seq, DataInLender::Ack::Disk);
        answered_ = ask.seq;
        returning_ = false;
        break;
    case LiveLine::GiveBack::Lost:
        lender_->confirm(ask.seq, DataInLender::Ack::Released);
        answered_ = ask.seq;
        returning_ = false;
        break;
    case LiveLine::GiveBack::Busy:
        break;
    }
}

}  // namespace rb::cassette
