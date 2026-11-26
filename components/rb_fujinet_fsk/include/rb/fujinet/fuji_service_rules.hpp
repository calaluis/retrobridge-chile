#pragma once

// How CassetteService uses the experimental FujiNet engine (cassette-motor-fujinet; review of PR #122, H1
// and H3). Written for RetroBridge. Pure, so the host tests check the same decisions the firmware takes.

namespace rb::fujinet {

// The engine plays the tape whenever the cassette drives DATA IN — exclusive, or shared with the disk and
// lent by the SIO task (menu-sio) — and the engine has its memory. Otherwise CassettePlayer stays.
constexpr bool fujiEngineChosen(bool variantBuilt, bool driving, bool engineReady) noexcept
{
    return variantBuilt && driving && engineReady;
}

// A request that may be answered inside a step without stopping it: exactly `status` (spaces around it
// allowed), the one command that changes nothing. Any other line stops the step and runs between steps.
inline bool answeredInsideAStep(const char* line) noexcept
{
    if (line == nullptr) {
        return false;
    }
    auto space = [](char c) { return c == ' ' || c == '\t'; };
    while (space(*line)) {
        ++line;
    }
    const char* word = "status";
    while (*word != '\0') {
        if (*line++ != *word++) {
            return false;
        }
    }
    while (space(*line)) {
        ++line;
    }
    return *line == '\0';
}

}  // namespace rb::fujinet
