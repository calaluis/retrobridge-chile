#include "rb/menu/data_in_handover.hpp"

namespace rb::host {

uint32_t DataInHandover::request(HandoverRequest kind) noexcept
{
    lastSeq_ = (lastSeq_ + 1u) & kSeqMask;
    if (lastSeq_ == 0) {
        lastSeq_ = 1;  // 0 is the boot state, already confirmed Disk
    }
    request_.store(pack(lastSeq_, static_cast<uint32_t>(kind)), std::memory_order_release);
    return lastSeq_;
}

bool DataInHandover::ackFor(uint32_t seq, HandoverAck& ack) const noexcept
{
    const uint32_t word = ack_.load(std::memory_order_acquire);
    if ((word >> 2) != (seq & kSeqMask)) {
        return false;
    }
    ack = static_cast<HandoverAck>(word & 3u);
    return true;
}

DataInHandover::Pending DataInHandover::pending() const noexcept
{
    const uint32_t word = request_.load(std::memory_order_acquire);
    return {static_cast<HandoverRequest>(word & 3u), word >> 2};
}

void DataInHandover::confirm(uint32_t seq, HandoverAck ack) noexcept
{
    ack_.store(pack(seq, static_cast<uint32_t>(ack)), std::memory_order_release);
}

LineResult HandoverTapeLine::drive(HandoverRequest kind) noexcept
{
    if (!waiting_ || kind_ != kind) {
        seq_ = handover_.request(kind);  // a new sequence: any ack for an older request is ignored
        kind_ = kind;
        waiting_ = true;
    }
    HandoverAck ack;
    if (!handover_.ackFor(seq_, ack)) {
        return LineResult::Pending;
    }
    waiting_ = false;
    if (kind == HandoverRequest::Take) {
        return ack == HandoverAck::Tape ? LineResult::Done : LineResult::Failed;
    }
    switch (ack) {
    case HandoverAck::Disk:
        return LineResult::Done;
    case HandoverAck::Released:
        return LineResult::Released;
    default:
        return LineResult::Failed;  // Refused, or Tape for a release: the line may still be held
    }
}

}  // namespace rb::host
