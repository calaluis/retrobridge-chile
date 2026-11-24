#pragma once

// The cassette task's ports over the SIO task's objects (spec menu-cinta, plan §2.4, T9a.7). rb_cassette
// cannot see rb_menu (layering), so the firmware adapts here: DataInHandover is the lender of DATA IN,
// TapeProgress the sink of "the tape played to its end". No ESP-IDF: the host tests build it too.
//
// Every atomic access of the cassette's side lives in cassette_ports.cpp, out of line, so CI can check
// that object alone with tools/ci/check_atomic_calls.py (requirement 2): menu_sio.cpp also takes
// FreeRTOS critical sections, which that check would rightly reject.

#include <cstdint>

#include "rb/cassette/cassette_tick.hpp"
#include "rb/menu/data_in_handover.hpp"
#include "rb/menu/tape_progress.hpp"

namespace rb::menusio {

class HandoverLender final : public rb::cassette::DataInLender {
public:
    explicit HandoverLender(rb::host::DataInHandover& handover) noexcept : handover_(handover) {}
    Ask pending() const noexcept override;
    void confirm(uint32_t seq, Ack ack) noexcept override;

private:
    rb::host::DataInHandover& handover_;
};

class ProgressSink final : public rb::cassette::EndSink {
public:
    explicit ProgressSink(rb::host::TapeProgress& progress) noexcept : progress_(progress) {}
    void publish(bool ended) noexcept override;

private:
    rb::host::TapeProgress& progress_;
};

// The SIO task's read of "the tape played to its end" (DataInReadings.tapeEnded), out of line too.
bool tapeEnded(const rb::host::TapeProgress& progress) noexcept;

}  // namespace rb::menusio
