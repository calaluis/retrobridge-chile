#pragma once

// NetSioTapeLink of retrobridge_host (spec cassette-cas, C002): the cassette's
// TapeLink over the NetSIO transport. MOTOR is the state of the last 20/21 from
// the Atari; the rate goes out as SPEED_CHANGE; tape bytes go out as DATA BLOCKs
// outside any command frame, never waiting for credit. While a disk command is
// in progress (frame, sync or write data), tape bytes are refused; the cassette
// tries again on its next tick.

#include "cassette_player.hpp"
#include "rb/netsio/netsio_sio_transport.hpp"

namespace rb::host {

class NetSioTapeLink final : public TapeLink {
public:
    explicit NetSioTapeLink(rb::netsio::NetSioSioTransport& transport) noexcept : transport_(transport) {}

    bool motorOn() const noexcept override { return transport_.motorOn(); }
    rb::core::Result<void> setBaud(uint32_t baud) override { return transport_.sendSpeed(baud); }
    rb::core::Result<void> sendTapeBytes(rb::core::Span<const uint8_t> bytes) override
    {
        return transport_.sendOutsideFrame(bytes);  // fails fast while a disk command owns the line
    }

private:
    rb::netsio::NetSioSioTransport& transport_;
};

}  // namespace rb::host
