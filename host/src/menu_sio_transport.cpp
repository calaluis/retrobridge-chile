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
        haveLast_ = false;  // the menu starts again: its first key is never a retry
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
    if (command.bytes[1] == kMenuTextCommand) {
        serveText(command);
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
    const uint8_t key = command.bytes[2];
    // AUX2: the sequence number in bits 7-5, the row of the local bar in bits 4-0.
    const uint8_t sequence = command.bytes[3];
    // SIOV retries a command whose reply did not arrive with the same AUX1 and
    // AUX2. Acting on it again would apply the key twice (a second mount, or a
    // digit read on the next screen): resend the reply already built instead.
    // Every start of the menu draws first (AUX1 = kMenuNoKey), which forgets it.
    if (key != kMenuNoKey && haveLast_ && key == lastKey_ && sequence == lastSequence_) {
        ++stats_.menuRetries;
    } else {
        const MenuReply reply = controller_.key(key, static_cast<uint8_t>(sequence & kMenuRowMask));
        response_[0] = rb::sio::code::kComplete;
        for (std::size_t i = 0; i < MenuReply::kSize; ++i) {
            response_[1 + i] = reply.bytes[i];
        }
        response_[1 + MenuReply::kSize] =
            rb::sio::sio_checksum(Span<const uint8_t>{reply.bytes.data(), MenuReply::kSize});
        haveLast_ = key != kMenuNoKey;
        lastKey_ = key;
        lastSequence_ = sequence;
    }
    clock_.waitUntil(ackEnd + rb::sio::SioSession::kCompleteDelayUs);
    // COMPLETE, the screen and the checksum are one byte stream for the Atari;
    // the transport takes it in blocks of at most kSendBlock bytes.
    for (std::size_t sent = 0; sent < response_.size(); sent += kSendBlock) {
        const std::size_t left = response_.size() - sent;
        const std::size_t length = left < kSendBlock ? left : kSendBlock;
        if (!inner_.send(Span<const uint8_t>{response_.data() + sent, length}).ok()) {
            break;  // the Atari's SIOV times out and shows that the host does not answer
        }
    }
    inner_.releaseDataLine();
}

// 'T' (menu-editar-servidor): AUX1 is a position of the text being edited, AUX2 its
// ASCII character. No data either way: ACK, then COMPLETE after the same delay as
// SioSession. A retry writes the same character again, so nothing is remembered.
void MenuSioTransport::serveText(const CommandBytes& command)
{
    ++stats_.textCommands;
    const uint8_t ack = rb::sio::code::kAck;
    if (!inner_.send(Span<const uint8_t>{&ack, 1}).ok()) {
        inner_.releaseDataLine();
        return;
    }
    const uint64_t ackEnd = clock_.nowMicros();
    controller_.text(command.bytes[2], command.bytes[3]);
    clock_.waitUntil(ackEnd + rb::sio::SioSession::kCompleteDelayUs);
    const uint8_t complete = rb::sio::code::kComplete;
    (void)inner_.send(Span<const uint8_t>{&complete, 1});
    inner_.releaseDataLine();
}

}  // namespace rb::host
