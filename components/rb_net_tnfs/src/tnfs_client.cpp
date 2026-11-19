// TnfsClient (see tnfs_client.hpp for the rules it follows). Written from the
// public protocol document (docs/fuentes.md); no TNFS implementation was read.
#include "rb/net_tnfs/tnfs_client.hpp"

namespace rb::net_tnfs {

using rb::core::ErrorCode;
using rb::core::Span;

uint32_t TnfsClient::waitUs(bool firstContact) const noexcept
{
    if (firstContact || !info_.hasMinRetry || info_.minRetryMs == 0) {
        return firstContact ? kFirstContactWaitUs : kDefaultWaitUs;
    }
    const uint32_t announced = uint32_t{info_.minRetryMs} * 1000u;
    return announced < kMaxWaitUs ? announced : kMaxWaitUs;
}

TnfsResult TnfsClient::toResult(Op op) noexcept
{
    switch (op) {
    case Op::Ok:
        return TnfsResult::Ok;
    case Op::NotOpened:
        return TnfsResult::NotOpened;
    case Op::Malformed:
        return TnfsResult::Malformed;
    case Op::Cancelled:
        return TnfsResult::Cancelled;
    case Op::NoResponse:
    case Op::SessionLost:
        break;
    }
    return TnfsResult::NoResponse;
}

TnfsClient::Op TnfsClient::failed(Exchange exchange) noexcept
{
    switch (exchange) {
    case Exchange::Malformed:
        return Op::Malformed;
    case Exchange::Cancelled:
        return Op::Cancelled;
    case Exchange::Reply:
    case Exchange::NoResponse:
        break;
    }
    return Op::NoResponse;
}

bool TnfsClient::sleepFor(uint64_t us, CancelCheck* cancel) noexcept
{
    const uint64_t until = clock_.nowMicros() + us;
    for (;;) {
        if (cancel != nullptr && cancel->cancelled()) {
            return false;
        }
        const uint64_t now = clock_.nowMicros();
        if (now >= until) {
            return true;
        }
        clock_.sleepUntil(until - now < kCancelCheckUs ? until : now + kCancelCheckUs);
    }
}

TnfsClient::Exchange TnfsClient::exchange(std::size_t length, uint8_t command, uint8_t sequence,
                                          bool firstContact, Reply& reply, CancelCheck* cancel) noexcept
{
    const Span<const uint8_t> request(request_.data(), length);
    unsigned attempts = 0;
    unsigned eagains = 0;
    bool sawMalformed = false;
    while (attempts < kAttempts) {
        bool again = false;  // EAGAIN: send once more without using an attempt
        if (port_.send(request).ok()) {
            const uint64_t deadline = clock_.nowMicros() + waitUs(firstContact);
            for (;;) {
                if (cancel != nullptr && cancel->cancelled()) {
                    return Exchange::Cancelled;
                }
                const uint64_t now = clock_.nowMicros();
                if (now >= deadline) {
                    break;
                }
                const uint64_t slice = deadline - now < kCancelCheckUs ? deadline - now : kCancelCheckUs;
                auto got = port_.receive(Span<uint8_t>(received_), static_cast<uint32_t>(slice));
                if (!got.ok()) {
                    if (got.error() == ErrorCode::Timeout) {
                        continue;  // the next slice of this attempt's wait
                    }
                    break;  // the port failed: this attempt is over
                }
                const Reply r = parse(Span<const uint8_t>(received_.data(), got.value()), command, sequence);
                if (r.kind == ReplyKind::Foreign) {
                    continue;
                }
                if (r.kind == ReplyKind::Malformed) {
                    sawMalformed = true;
                    continue;
                }
                if (r.status == status::kEagain) {
                    auto delay = decodeEagainDelay(r);
                    if (!delay.ok()) {
                        sawMalformed = true;
                        continue;
                    }
                    if (++eagains > kMaxEagain) {
                        return Exchange::NoResponse;
                    }
                    const uint64_t asked = uint64_t{delay.value()} * 1000u;
                    const uint64_t most = waitUs(firstContact);  // a server cannot hold the client longer
                    if (!sleepFor(asked < most ? asked : most, cancel)) {
                        return Exchange::Cancelled;
                    }
                    again = true;
                    break;
                }
                reply = r;
                return Exchange::Reply;
            }
        }
        if (!again) {
            ++attempts;
        }
    }
    return sawMalformed ? Exchange::Malformed : Exchange::NoResponse;
}

TnfsResult TnfsClient::mount() noexcept
{
    umount();  // a second session would be left open on the server
    const uint8_t sequence = nextSequence();
    auto length = encodeMount(sequence, "/", Span<uint8_t>(request_));
    if (!length.ok()) {
        return TnfsResult::Malformed;  // cannot happen: "/" and the buffer are fixed
    }
    Reply reply;
    const Exchange ex = exchange(length.value(), cmd::kMount, sequence, true, reply, nullptr);
    if (ex != Exchange::Reply) {
        return ex == Exchange::Malformed ? TnfsResult::Malformed : TnfsResult::NoResponse;
    }
    if (reply.status != status::kOk) {
        lastStatus_ = reply.status;
        return TnfsResult::Rejected;
    }
    auto info = decodeMount(reply);
    if (!info.ok()) {
        return TnfsResult::Malformed;
    }
    connection_ = reply.connection;
    info_ = info.value();
    mounted_ = true;
    return TnfsResult::Ok;
}

TnfsResult TnfsClient::ensureMounted() noexcept
{
    if (mounted_) {
        return TnfsResult::Ok;
    }
    const TnfsResult result = mount();
    return result == TnfsResult::Rejected ? TnfsResult::NotOpened : result;
}

bool TnfsClient::reconnect() noexcept
{
    mounted_ = false;  // the server forgot it: no UMOUNT
    return mount() == TnfsResult::Ok;
}

void TnfsClient::closeQuietly(uint8_t command, uint8_t handle) noexcept
{
    const uint8_t sequence = nextSequence();
    auto length = command == cmd::kCloseDir ? encodeCloseDir(connection_, sequence, handle, Span<uint8_t>(request_))
                                            : encodeClose(connection_, sequence, handle, Span<uint8_t>(request_));
    if (!length.ok()) {
        return;
    }
    Reply reply;
    (void)exchange(length.value(), command, sequence, false, reply, nullptr);  // nothing more to do if it fails
}

// --- listDir ------------------------------------------------------------------

TnfsClient::Op TnfsClient::listOnce(const char* path, DirVisitor& visitor) noexcept
{
    if (visitor.cancelled()) {
        return Op::Cancelled;
    }
    uint8_t sequence = nextSequence();
    auto length = encodeOpenDir(connection_, sequence, path, Span<uint8_t>(request_));
    if (!length.ok()) {
        return Op::NotOpened;
    }
    Reply reply;
    // Not cancellable: if it ran, its handle must be known to be closed.
    const Exchange opened = exchange(length.value(), cmd::kOpenDir, sequence, false, reply, nullptr);
    if (opened != Exchange::Reply) {
        return failed(opened);
    }
    if (reply.status != status::kOk) {
        lastStatus_ = reply.status;
        return sessionLost(reply.status) ? Op::SessionLost : Op::NotOpened;
    }
    auto handle = decodeHandle(reply);
    if (!handle.ok()) {
        return Op::Malformed;
    }

    Op result = Op::Ok;
    for (;;) {
        if (visitor.cancelled()) {
            result = Op::Cancelled;
            break;
        }
        sequence = nextSequence();
        length = encodeReadDir(connection_, sequence, handle.value(), Span<uint8_t>(request_));
        if (!length.ok()) {
            result = Op::Malformed;
            break;
        }
        const Exchange ex = exchange(length.value(), cmd::kReadDir, sequence, false, reply, &visitor);
        if (ex != Exchange::Reply) {
            result = failed(ex);
            break;
        }
        if (reply.status == status::kEof) {
            break;
        }
        if (reply.status != status::kOk) {
            lastStatus_ = reply.status;
            result = sessionLost(reply.status) ? Op::SessionLost : Op::NotOpened;
            break;
        }
        auto name = decodeDirEntry(reply);
        if (!name.ok()) {
            result = Op::Malformed;
            break;
        }
        if (!visitor.entry(name.value())) {
            result = Op::Cancelled;
            break;
        }
    }
    if (result != Op::SessionLost) {  // a lost session has no handle left to close
        closeQuietly(cmd::kCloseDir, handle.value());
    }
    return result;
}

TnfsResult TnfsClient::listDir(const char* path, DirVisitor& visitor) noexcept
{
    TnfsResult mounted = ensureMounted();
    if (mounted != TnfsResult::Ok) {
        return mounted;
    }
    const Op first = listOnce(path, visitor);
    if (first != Op::SessionLost) {
        return toResult(first);
    }
    if (!reconnect()) {
        return TnfsResult::NoResponse;
    }
    visitor.restart();
    const Op second = listOnce(path, visitor);
    if (second == Op::SessionLost) {
        mounted_ = false;
    }
    return toResult(second);
}

// --- stat ---------------------------------------------------------------------

TnfsClient::Op TnfsClient::statOnce(const char* path, StatResult& out, CancelCheck* cancel) noexcept
{
    const uint8_t sequence = nextSequence();
    auto length = encodeStat(connection_, sequence, path, Span<uint8_t>(request_));
    if (!length.ok()) {
        return Op::NotOpened;
    }
    Reply reply;
    const Exchange ex = exchange(length.value(), cmd::kStat, sequence, false, reply, cancel);
    if (ex != Exchange::Reply) {
        return failed(ex);
    }
    if (reply.status != status::kOk) {
        lastStatus_ = reply.status;
        return sessionLost(reply.status) ? Op::SessionLost : Op::NotOpened;
    }
    auto info = decodeStat(reply);
    if (!info.ok()) {
        return Op::Malformed;
    }
    out.isDirectory = info.value().isDirectory();
    out.size = info.value().size;
    return Op::Ok;
}

StatResult TnfsClient::stat(const char* path, CancelCheck* cancel) noexcept
{
    StatResult out;
    out.result = ensureMounted();
    if (out.result != TnfsResult::Ok) {
        return out;
    }
    Op op = statOnce(path, out, cancel);
    if (op == Op::SessionLost) {
        if (!reconnect()) {
            out.result = TnfsResult::NoResponse;
            return out;
        }
        op = statOnce(path, out, cancel);
        if (op == Op::SessionLost) {
            mounted_ = false;
        }
    }
    out.result = toResult(op);
    return out;
}

// --- readWholeReadOnly --------------------------------------------------------

TnfsClient::Op TnfsClient::readOnce(const char* path, ReadSink& sink) noexcept
{
    if (sink.cancelled()) {
        return Op::Cancelled;
    }
    StatResult st;
    const Op statted = statOnce(path, st, &sink);
    if (statted != Op::Ok) {
        return statted;
    }
    if (st.isDirectory || !sink.begin(st.size)) {
        return Op::NotOpened;
    }

    uint8_t sequence = nextSequence();
    auto length = encodeOpenReadOnly(connection_, sequence, path, Span<uint8_t>(request_));
    if (!length.ok()) {
        return Op::NotOpened;
    }
    Reply reply;
    // Not cancellable: if it ran, its handle must be known to be closed.
    const Exchange opened = exchange(length.value(), cmd::kOpen, sequence, false, reply, nullptr);
    if (opened != Exchange::Reply) {
        return failed(opened);
    }
    if (reply.status != status::kOk) {
        lastStatus_ = reply.status;
        return sessionLost(reply.status) ? Op::SessionLost : Op::NotOpened;
    }
    auto handle = decodeHandle(reply);
    if (!handle.ok()) {
        return Op::Malformed;
    }

    Op result = Op::Ok;
    uint32_t offset = 0;
    unsigned emptyReads = 0;
    while (offset < st.size) {
        if (sink.cancelled()) {
            result = Op::Cancelled;
            break;
        }
        const uint32_t left = st.size - offset;
        const uint16_t want = left < kReadBlock ? static_cast<uint16_t>(left) : kReadBlock;
        sequence = nextSequence();
        length = encodeRead(connection_, sequence, handle.value(), want, Span<uint8_t>(request_));
        if (!length.ok()) {
            result = Op::Malformed;
            break;
        }
        const Exchange ex = exchange(length.value(), cmd::kRead, sequence, false, reply, &sink);
        if (ex != Exchange::Reply) {
            result = failed(ex);
            break;
        }
        if (reply.status == status::kEof) {  // shorter than STAT said
            result = Op::Malformed;
            break;
        }
        if (reply.status != status::kOk) {
            lastStatus_ = reply.status;
            result = sessionLost(reply.status) ? Op::SessionLost : Op::NotOpened;
            break;
        }
        auto data = decodeReadData(reply, want);  // also rejects more than asked for
        if (!data.ok()) {
            result = Op::Malformed;
            break;
        }
        if (data.value().size() == 0) {
            if (++emptyReads >= kMaxEmptyReads) {
                result = Op::Malformed;
                break;
            }
            continue;
        }
        emptyReads = 0;
        sink.data(offset, data.value());
        offset += static_cast<uint32_t>(data.value().size());
    }
    if (result != Op::SessionLost) {
        closeQuietly(cmd::kClose, handle.value());
    }
    return result;
}

TnfsResult TnfsClient::readWholeReadOnly(const char* path, ReadSink& sink) noexcept
{
    TnfsResult mounted = ensureMounted();
    if (mounted != TnfsResult::Ok) {
        return mounted;
    }
    const Op first = readOnce(path, sink);
    if (first != Op::SessionLost) {
        return toResult(first);
    }
    if (!reconnect()) {
        return TnfsResult::NoResponse;
    }
    const Op second = readOnce(path, sink);  // begin() again, from offset 0
    if (second == Op::SessionLost) {
        mounted_ = false;
    }
    return toResult(second);
}

// --- umount -------------------------------------------------------------------

void TnfsClient::umount() noexcept
{
    if (!mounted_) {
        return;
    }
    mounted_ = false;
    auto length = encodeUmount(connection_, Span<uint8_t>(request_));
    if (!length.ok()) {
        return;
    }
    Reply reply;
    (void)exchange(length.value(), cmd::kUmount, 0, false, reply, nullptr);  // forgotten whatever the answer
}

void TnfsClient::umountWithoutWaiting() noexcept
{
    if (!mounted_) {
        return;
    }
    mounted_ = false;
    auto length = encodeUmount(connection_, Span<uint8_t>(request_));
    if (length.ok()) {
        (void)port_.send(Span<const uint8_t>(request_.data(), length.value()));
    }
}

}  // namespace rb::net_tnfs
