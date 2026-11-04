#include "rb/netsio/netsio_sio_transport.hpp"

#include <limits>

namespace rb::netsio {

using rb::app::CommandBytes;
using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

uint32_t clampTimeout(uint64_t micros) noexcept
{
    constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(micros < kMax ? micros : kMax);
}

uint32_t littleEndian32(Span<const uint8_t> bytes) noexcept
{
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

}  // namespace

Result<void> NetSioSioTransport::connect()
{
    for (uint8_t attempt = 0; attempt < config_.pingAttempts; ++attempt) {
        pingAnswered_ = false;
        const Result<void> pinged = transmitSignal(id::kPingRequest);
        if (!pinged.ok()) {
            return pinged;
        }
        const uint64_t deadline = clock_.nowMicros() + config_.pingTimeoutUs;
        while (!pingAnswered_ && clock_.nowMicros() < deadline) {
            const Result<bool> received = receiveOne(deadline);
            if (!received.ok()) {
                if (received.error() != ErrorCode::Timeout) {
                    clock_.waitUntil(deadline);  // peer not listening yet: do not spin
                }
                break;
            }
        }
        if (pingAnswered_) {
            const Result<void> registered = transmitSignal(id::kDeviceConnected);
            if (!registered.ok()) {
                return registered;
            }
            connected_ = true;
            lastTrailer_.reset();
            nextAliveUs_ = clock_.nowMicros() + config_.aliveIntervalUs;
            return {};
        }
    }
    return Failure{ErrorCode::Timeout};
}

void NetSioSioTransport::disconnect()
{
    answerEmpty();
    if (connected_) {
        (void)transmitSignal(id::kDeviceDisconnected);  // best effort: the hub also expires silent devices
        connected_ = false;
    }
}

Result<CommandBytes> NetSioSioTransport::waitCommandFrame(uint32_t timeoutUs)
{
    answerEmpty();  // a sync left pending must never stall the emulator
    const uint64_t deadline = clock_.nowMicros() + timeoutUs;
    for (;;) {
        if (clock_.nowMicros() >= deadline) {
            return Failure{ErrorCode::Timeout};
        }
        const Result<bool> received = receiveOne(deadline);
        if (!received.ok()) {
            if (received.error() == ErrorCode::Timeout) {
                continue;
            }
            return Failure{ErrorCode::TransportError};
        }
        if (received.value()) {
            return frame_;
        }
    }
}

Result<void> NetSioSioTransport::send(Span<const uint8_t> bytes)
{
    if (pendingSync_.has_value()) {
        if (bytes.size() != 1) {
            return Failure{ErrorCode::TransportError};  // only ACK/NAK can answer a sync
        }
        const std::size_t length = encodeSyncResponse(Span<uint8_t>{tx_}, *pendingSync_, ack::kValid, bytes[0], 0);
        const Result<void> sent = transmit(length);
        if (!sent.ok()) {
            return sent;  // still pending: releaseDataLine() answers it empty
        }
        pendingSync_.reset();
        ++stats_.validSyncResponses;
        return {};
    }

    if (bytes.empty() || bytes.size() > kMaxDataBlock) {
        return Failure{ErrorCode::TransportError};
    }
    const Result<void> credited = acquireCredit();
    if (!credited.ok()) {
        return credited;
    }
    const Result<void> sent = transmit(encodeDataBlock(Span<uint8_t>{tx_}, bytes));
    if (sent.ok()) {
        --credit_;
    }
    return sent;
}

void NetSioSioTransport::releaseDataLine()
{
    answerEmpty();
}

Result<void> NetSioSioTransport::transmit(std::size_t length)
{
    const Span<const uint8_t> datagram{tx_.data(), length};
    if (observer_ != nullptr) {
        observer_->onDatagram(true, datagram);
    }
    const Result<void> sent = port_.send(datagram);
    if (!sent.ok()) {
        return Failure{ErrorCode::TransportError};
    }
    ++stats_.datagramsOut;
    return {};
}

Result<void> NetSioSioTransport::transmitSignal(uint8_t messageId)
{
    return transmit(encodeSignal(Span<uint8_t>{tx_}, messageId));
}

void NetSioSioTransport::answerEmpty()
{
    if (!pendingSync_.has_value()) {
        return;
    }
    const uint8_t sync = *pendingSync_;
    pendingSync_.reset();
    if (transmit(encodeSyncResponse(Span<uint8_t>{tx_}, sync, ack::kEmpty, 0, 0)).ok()) {
        ++stats_.emptySyncResponses;
    }
}

void NetSioSioTransport::serviceAlive()
{
    if (!connected_ || clock_.nowMicros() < nextAliveUs_) {
        return;
    }
    if (transmitSignal(id::kAliveRequest).ok()) {
        ++stats_.aliveRequests;
    }
    nextAliveUs_ = clock_.nowMicros() + config_.aliveIntervalUs;
}

Result<void> NetSioSioTransport::acquireCredit()
{
    if (credit_ > 0) {
        return {};
    }
    const Result<void> asked = transmit(encodeCreditStatus(Span<uint8_t>{tx_}, 0));
    if (!asked.ok()) {
        return asked;
    }
    ++stats_.creditRequests;
    const uint64_t deadline = clock_.nowMicros() + config_.creditTimeoutUs;
    while (credit_ == 0) {
        if (clock_.nowMicros() >= deadline) {
            return Failure{ErrorCode::TransportError};
        }
        const Result<bool> received = receiveOne(deadline);
        if (!received.ok() && received.error() != ErrorCode::Timeout) {
            return Failure{ErrorCode::TransportError};
        }
        if (received.ok() && received.value()) {
            // A new command frame in the middle of a transaction: the computer gave
            // up on this one. Drop the frame and release the emulator.
            ++stats_.framesDiscarded;
            answerEmpty();
        }
    }
    return {};
}

Result<bool> NetSioSioTransport::receiveOne(uint64_t deadlineUs)
{
    serviceAlive();
    const uint64_t now = clock_.nowMicros();
    if (now >= deadlineUs) {
        return Failure{ErrorCode::Timeout};
    }
    uint64_t until = deadlineUs;
    if (connected_ && nextAliveUs_ < until) {
        until = nextAliveUs_;  // serviceAlive() guarantees nextAliveUs_ > now
    }
    const Result<std::size_t> received = port_.receive(Span<uint8_t>{rx_}, clampTimeout(until - now));
    if (!received.ok()) {
        return Failure{received.error() == ErrorCode::Timeout ? ErrorCode::Timeout : ErrorCode::TransportError};
    }
    return process(received.value());
}

bool NetSioSioTransport::process(std::size_t length)
{
    const Span<const uint8_t> datagram{rx_.data(), length};
    ++stats_.datagramsIn;
    if (observer_ != nullptr) {
        observer_->onDatagram(false, datagram);
    }
    const Result<Message> decoded = decode(datagram, config_.profile);
    if (!decoded.ok()) {
        ++stats_.malformed;
        return false;
    }
    const Message& message = decoded.value();
    if (message.hasTrailer) {
        if (lastTrailer_.has_value() && message.trailer != static_cast<uint8_t>(*lastTrailer_ + 1)) {
            ++stats_.trailerGaps;
        }
        lastTrailer_ = message.trailer;
    }

    switch (message.id) {
    case id::kCommandOn:
        if (inFrame_) {
            ++stats_.framesDiscarded;
        }
        frame_ = CommandBytes{};
        frame_.commandAssertedAtUs = clock_.nowMicros();
        inFrame_ = true;
        return false;
    case id::kDataByte:
    case id::kDataBlock:
        append(message.payload);
        return false;
    case id::kCommandOffSync:
        pendingSync_ = message.payload[0];
        if (!inFrame_) {
            answerEmpty();  // a sync without a frame: release the emulator at once
            return false;
        }
        [[fallthrough]];
    case id::kCommandOff:
        if (!inFrame_) {
            return false;
        }
        inFrame_ = false;
        frame_.commandReleasedAtUs = clock_.nowMicros();
        ++stats_.framesDelivered;
        return true;
    case id::kDataByteSync:
        pendingSync_ = message.payload[1];
        answerEmpty();  // write data frame: v1 is read-only and never plans this sync
        return false;
    case id::kSpeedChange:
        stats_.lastBaud = littleEndian32(message.payload);
        return false;
    case id::kColdReset:
    case id::kWarmReset:
        ++stats_.resets;
        if (inFrame_) {
            inFrame_ = false;
            ++stats_.framesDiscarded;
        }
        return false;
    case id::kCreditUpdate:
        credit_ = message.payload[0];
        return false;
    case id::kPingResponse:
        pingAnswered_ = true;
        return false;
    case id::kAliveResponse:
        ++stats_.aliveResponses;
        return false;
    default:
        ++stats_.ignoredMessages;
        return false;
    }
}

void NetSioSioTransport::append(Span<const uint8_t> bytes) noexcept
{
    if (!inFrame_) {
        ++stats_.ignoredMessages;  // data outside a command frame: not for a disk drive in v1
        return;
    }
    for (uint8_t byte : bytes) {
        if (frame_.count < frame_.bytes.size()) {
            frame_.bytes[frame_.count] = byte;
        }
        if (frame_.count < std::numeric_limits<uint8_t>::max()) {
            ++frame_.count;
        }
    }
}

}  // namespace rb::netsio
