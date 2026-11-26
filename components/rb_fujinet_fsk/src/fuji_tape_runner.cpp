// FujiTapeRunner — RetroBridge adapter (see the header). Written for RetroBridge.
#include "rb/fujinet/fuji_tape_runner.hpp"

namespace rb::fujinet {

FujiTapeRunner::FujiTapeRunner(rb::cassette::ByteOut& out, const rb::cassette::MotorIn& motor,
                               FskRunPlayer& player, Pause& pause) noexcept
    : out_(out), motor_(motor), pause_(pause), loop_(followed_, *this, player)
{
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
    last_ = FujiStep{};
    return true;
}

bool FujiTapeRunner::tick() noexcept
{
    if (followed_.tape == nullptr) {
        return false;
    }
    if (!motor_.on()) {
        enabled_ = false;  // between records: the tape stays where it is
        waitMotorOff_ = false;
        return false;
    }
    if (waitMotorOff_) {
        return false;
    }
    if (!enabled_) {
        loop_.enable();
        enabled_ = true;
    }
    last_ = loop_.step();
    if (last_.offset == 0) {
        enabled_ = false;  // the beta disables the cassette when a step leaves the tape at its start
        waitMotorOff_ = true;
    }
    return true;
}

void FujiTapeRunner::setBaud(uint32_t baud)
{
    while (!out_.setBaud(baud)) {
        pause_.yield();
    }
}

void FujiTapeRunner::write(const uint8_t* bytes, uint32_t count)
{
    while (!out_.send(rb::core::Span<const uint8_t>(bytes, count))) {
        pause_.yield();
    }
}

void FujiTapeRunner::flushOutput()
{
    while (!out_.drained()) {
        pause_.yield();
    }
}

}  // namespace rb::fujinet
