#pragma once

// PlayerOutputs (spec menu-cinta, plan §2.4, T9a.3): which outputs the player plays into, chosen while
// the tape runs. "Discard" (DiscardByteOut/DiscardWaveOut) until the cassette holds DATA IN; the live
// outputs (EspDataIn on the ESP32) only once it confirms Tape; discard again on release.
//
// The cassette task owns it and calls select() between two ticks, as steps 3 and 6 of the handover
// say, so a switch never falls inside a send() or a push(). Each tick then passes bytes() and wave()
// to CassettePlayer::tick(). Never allocates.

#include "rb/cassette/cassette_player.hpp"

namespace rb::cassette {

class PlayerOutputs {
public:
    PlayerOutputs(ByteOut& discardBytes, WaveOut& discardWave) noexcept
        : discardBytes_(discardBytes), discardWave_(discardWave)
    {
    }

    // The outputs that drive DATA IN. Set once, before the first select(true).
    void setLive(ByteOut& bytes, WaveOut& wave) noexcept
    {
        liveBytes_ = &bytes;
        liveWave_ = &wave;
    }
    bool hasLive() const noexcept { return liveBytes_ != nullptr; }
    bool live() const noexcept { return live_; }

    ByteOut& bytes() noexcept { return live_ ? *liveBytes_ : discardBytes_; }
    WaveOut& wave() noexcept { return live_ ? *liveWave_ : discardWave_; }

    // Between two ticks. The player moves onto the chosen outputs (CassettePlayer::changeOutputs).
    // False, and still on discard, if live outputs were asked for and none are set.
    bool select(bool live, CassettePlayer& player) noexcept
    {
        if (live && !hasLive()) {
            return false;
        }
        if (live == live_) {
            return true;
        }
        WaveOut& previous = wave();
        live_ = live;
        player.changeOutputs(previous);
        return true;
    }

private:
    ByteOut& discardBytes_;
    WaveOut& discardWave_;
    ByteOut* liveBytes_ = nullptr;
    WaveOut* liveWave_ = nullptr;
    bool live_ = false;
};

}  // namespace rb::cassette
