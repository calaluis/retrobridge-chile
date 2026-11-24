#include "cassette_ports.hpp"

namespace rb::menusio {

rb::cassette::DataInLender::Ask HandoverLender::pending() const noexcept
{
    const rb::host::DataInHandover::Pending p = handover_.pending();
    return Ask{p.kind == rb::host::HandoverRequest::Take ? Kind::Take : Kind::Release, p.seq};
}

void HandoverLender::confirm(uint32_t seq, Ack ack) noexcept
{
    handover_.confirm(seq, ack == Ack::Tape   ? rb::host::HandoverAck::Tape
                           : ack == Ack::Disk ? rb::host::HandoverAck::Disk
                                              : rb::host::HandoverAck::Released);
}

void ProgressSink::publish(bool ended) noexcept { progress_.publish(ended); }

bool tapeEnded(const rb::host::TapeProgress& progress) noexcept { return progress.ended(); }

}  // namespace rb::menusio
