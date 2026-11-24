#pragma once

// PlayerOutputs (spec menu-cinta, plan §2.4, T9a.3): which outputs the player plays into, chosen while
// the tape runs. "Discard" (DiscardByteOut/DiscardWaveOut) until the cassette holds DATA IN; the live
// outputs (EspDataIn on the ESP32) only once it confirms Tape; discard again on release.
//
// The cassette task owns it and calls select() between two ticks, as steps 3 and 6 of the handover
// say, so a switch never falls inside a send() or a push(). Each tick then passes bytes(), wave() and
// a GatedMotor to CassettePlayer::tick(). Never allocates.
//
// Nothing is lost while DATA IN is not the cassette's (review of PR #102): with live outputs set, the
// tape moves only while they are selected. During TakePending, after a release, or while a take keeps
// failing, the player sees MOTOR off: the tape stays where it is (an FSK run still preloads in its
// gap). Without live outputs (kTapeOutput = false) the tape moves with MOTOR into discard, as before,
// so `cas status` still follows a load attempt.

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
        const bool drove = live_;
        live_ = live;
        player.changeOutputs(previous, drove);
        return true;
    }

    // Whether the tape may move now: always without live outputs; with them, only while they are
    // selected.
    bool tapeMayMove() const noexcept { return !hasLive() || live_; }

private:
    ByteOut& discardBytes_;
    WaveOut& discardWave_;
    ByteOut* liveBytes_ = nullptr;
    WaveOut* liveWave_ = nullptr;
    bool live_ = false;
};

// MOTOR as the player must see it: the Atari's MOTOR, and the tape allowed to move (PlayerOutputs).
class GatedMotor final : public MotorIn {
public:
    GatedMotor(const MotorIn& motor, const PlayerOutputs& outputs) noexcept : motor_(motor), outputs_(outputs) {}
    bool on() const override { return outputs_.tapeMayMove() && motor_.on(); }

private:
    const MotorIn& motor_;
    const PlayerOutputs& outputs_;
};

}  // namespace rb::cassette
