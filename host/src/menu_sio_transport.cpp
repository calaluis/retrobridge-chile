#include "menu_sio_transport.hpp"

#include "menu_image.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_codes.hpp"
#include "rb/sio/sio_session.hpp"

namespace rb::host {

using rb::app::CommandBytes;
using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

Result<CommandBytes> MenuSioTransport::waitCommandFrame(uint32_t timeoutUs)
{
    Result<CommandBytes> received = inner_.waitCommandFrame(timeoutUs);
    if (!received.ok()) {
        return received;
    }
    if (controller_.coldResetPending()) {
        inner_.releaseDataLine();
        ++stats_.droppedAfterReset;
        return Failure{ErrorCode::Timeout};
    }
    const CommandBytes& command = received.value();
    if (command.count == command.bytes.size() && command.bytes[0] == kMenuDevice) {
        serveMenu(command);
        return Failure{ErrorCode::Timeout};
    }
    return received;
}

void MenuSioTransport::serveMenu(const CommandBytes& command)
{
    const Span<const uint8_t> body{command.bytes.data(), 4};
    if (rb::sio::sio_checksum(body) != command.bytes[4]) {
        inner_.releaseDataLine();
        return;
    }
    if (command.bytes[1] != kMenuCommand) {
        ++stats_.menuNaks;
        const uint8_t nak = rb::sio::code::kNak;
        (void)inner_.send(Span<const uint8_t>{&nak, 1});
        inner_.releaseDataLine();
        return;
    }
    ++stats_.menuCommands;
    const uint8_t ack = rb::sio::code::kAck;
    if (!inner_.send(Span<const uint8_t>{&ack, 1}).ok()) {
        inner_.releaseDataLine();  // without an ACK the Atari retries; nothing was selected
        return;
    }
    const uint64_t ackEnd = clock_.nowMicros();
    const MenuReply reply = controller_.select(command.bytes[2]);
    response_[0] = rb::sio::code::kComplete;
    for (std::size_t i = 0; i < MenuReply::kSize; ++i) {
        response_[1 + i] = reply.bytes[i];
    }
    response_[1 + MenuReply::kSize] = rb::sio::sio_checksum(Span<const uint8_t>{reply.bytes.data(), MenuReply::kSize});
    clock_.waitUntil(ackEnd + rb::sio::SioSession::kCompleteDelayUs);
    (void)inner_.send(Span<const uint8_t>{response_.data(), response_.size()});
    inner_.releaseDataLine();
}

}  // namespace rb::host
