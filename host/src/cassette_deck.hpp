#pragma once

// CassetteDeck of retrobridge_host (spec cassette-cas, C002): the tape mounted in
// C:. It owns a copy of the .cas bytes and their index, and the player that plays
// them. mount() refuses Pulses and Invalid tapes and then leaves C: as it was
// (AC8, AC9). rewind() goes back to the start and keeps the tape (AC12).
//
// mount() and eject() allocate: they run when a tape is chosen, outside the SIO
// path. tick() runs on the SIO loop and never allocates (ADR-011).

#include <array>
#include <cstdint>
#include <vector>

#include "cas_image.hpp"
#include "cassette_player.hpp"
#include "rb/core/span.hpp"

namespace rb::host {

class CassetteDeck {
public:
    static constexpr std::size_t kMaxName = 95;  // longer names are cut

    CasResult mount(const char* name, rb::core::Span<const uint8_t> cas);
    void eject();
    void rewind() noexcept { player_.rewind(); }
    void tick(uint64_t nowUs, TapeLink& link) { player_.tick(nowUs, link); }

    bool mounted() const noexcept { return player_.loaded(); }
    bool atEnd() const noexcept { return player_.atEnd(); }
    const char* name() const noexcept { return name_.data(); }
    const CassettePlayer& player() const noexcept { return player_; }

private:
    std::vector<uint8_t> bytes_;
    CasTape tape_;
    std::array<char, kMaxName + 1> name_{};
    CassettePlayer player_;
};

}  // namespace rb::host
