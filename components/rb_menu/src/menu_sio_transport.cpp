#include "rb/menu/menu_sio_transport.hpp"

#include "rb/menu/menu_image.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_codes.hpp"
#include "rb/sio/sio_session.hpp"

namespace rb::host {

using rb::app::CommandBytes;
using rb::core::ErrorCode;

namespace {
// The SIO task is the only writer (review of PR #124, H1): a relaxed load and store, never a lock.
void bump(std::atomic<uint32_t>& counter) noexcept
{
    counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}
}  // namespace
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
    trackWrite(command);
    return received;
}

void MenuSioTransport::trackWrite(const CommandBytes& command) noexcept
{
    writeStage_ = WriteStage::None;
    if (writes_ == nullptr || command.count != command.bytes.size()) {
        return;
    }
    const uint8_t device = command.bytes[0];
    const uint8_t code = command.bytes[1];
    if (device < rb::sio::device::kFirstDisk || device > rb::sio::device::kLastDisk ||
        (code != rb::sio::command::kWriteSector && code != rb::sio::command::kPutSector && !isFormat(code)) ||
        rb::sio::sio_checksum(Span<const uint8_t>{command.bytes.data(), 4}) != command.bytes[4]) {
        return;
    }
    write_ = SioWriteRecord{};
    write_.drive = static_cast<uint8_t>(device - 0x30);
    write_.command = code;
    write_.sector = static_cast<uint16_t>(command.bytes[2] | (command.bytes[3] << 8));
    writeStage_ = WriteStage::Command;
}

void MenuSioTransport::reportWrite(char result) noexcept
{
    write_.result = result;
    writeStage_ = WriteStage::None;
    writes_->onSioWrite(write_);
}

Result<void> MenuSioTransport::receiveAfterAck(Span<uint8_t> frame)
{
    const Result<void> received = inner_.receiveAfterAck(frame);
    if (writeStage_ == WriteStage::Command) {
        if (received.ok() && !frame.empty()) {
            write_.haveData = true;
            write_.crc32 = crc32(frame.data(), frame.size() - 1);  // the sector, without its checksum
            writeStage_ = WriteStage::Data;
        } else {
            writeStage_ = WriteStage::None;  // no data frame: nothing was written, nothing to report
        }
    }
    return received;
}

Result<void> MenuSioTransport::send(Span<const uint8_t> bytes)
{
    const Result<void> sent = inner_.send(bytes);
    if (writeStage_ == WriteStage::None || bytes.empty()) {
        return sent;
    }
    if (!sent.ok()) {
        if (writeStage_ == WriteStage::Completion) {
            // The sector was already handled: report it, with the reply lost.
            write_.replyLost = true;
            reportWrite(bytes[0] == rb::sio::code::kComplete ? 'C' : 'E');
        } else {
            writeStage_ = WriteStage::None;  // nothing was written
        }
        return sent;
    }
    const uint8_t first = bytes[0];
    switch (writeStage_) {
    case WriteStage::Command:  // the command itself was NAKed (invalid sector)
        if (first == rb::sio::code::kNak) {
            reportWrite('N');
        } else if (isFormat(write_.command)) {
            writeStage_ = WriteStage::Completion;  // a format has no data frame: it runs after the ACK
        }
        break;
    case WriteStage::Data:
        if (first == rb::sio::code::kNak) {
            reportWrite('N');  // bad data checksum: nothing written
        } else {
            writeStage_ = WriteStage::Completion;
        }
        break;
    case WriteStage::Completion:
        reportWrite(first == rb::sio::code::kComplete ? 'C' : 'E');
        break;
    case WriteStage::None:
    default:
        break;
    }
    return sent;
}

void MenuSioTransport::serveMenu(const CommandBytes& command)
{
    const Span<const uint8_t> body{command.bytes.data(), 4};
    if (rb::sio::sio_checksum(body) != command.bytes[4]) {
        bump(stats_.menuBadChecksums);
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
        bump(stats_.menuAckFailures);  // review of PR #124 (H2): counted apart from a reply block
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
    bool complete = true;
    for (std::size_t sent = 0; sent < response_.size(); sent += kSendBlock) {
        const std::size_t left = response_.size() - sent;
        const std::size_t length = left < kSendBlock ? left : kSendBlock;
        if (!inner_.send(Span<const uint8_t>{response_.data() + sent, length}).ok()) {
            bump(stats_.menuSendFailures);
            complete = false;
            break;  // the Atari's SIOV times out and shows that the host does not answer
        }
    }
    const uint64_t replyUs = clock_.nowMicros() - ackEnd;
    const uint32_t reply = replyUs > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(replyUs);
    // Review of PR #124 (M2): only a whole reply sets the reply times; a cut-off one is kept apart.
    if (!complete) {
        stats_.lastInterruptedUs.store(reply, std::memory_order_release);
    } else {
        stats_.lastReplyUs.store(reply, std::memory_order_release);
        if (reply > stats_.maxReplyUs.load(std::memory_order_relaxed)) {
            stats_.maxReplyUs.store(reply, std::memory_order_release);
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
