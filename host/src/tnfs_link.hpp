#pragma once

// What main.cpp gives TnfsLoader for each TNFS source (menu-tnfs T013):
//   * TnfsTracePort: a DatagramPort that writes every TNFS request to the host
//     log before sending it (command, sequence, and for OPEN its flags and
//     mode), so a run shows that only read-only requests left (AC12);
//   * UdpTnfsLink: the TnfsLink of a source named in --sources, over
//     UdpTnfsPort (T008) and a TnfsTracePort;
//   * HostTnfsClock: the TNFS client's clock over the host clock.
// Everything here runs on the loader thread; the log is shared with the SIO
// thread, which is why JsonlLog takes a lock.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "menu_controller.hpp"
#include "rb/app/ports/clock.hpp"
#include "rb/core/span.hpp"
#include "rb/net_tnfs/tnfs_client.hpp"
#include "rb/netsio/datagram_port.hpp"
#include "tnfs_loader.hpp"
#include "udp_tnfs_port.hpp"

namespace rb::host {

static_assert(TnfsLoader::kMaxServer == MenuSources::kMaxName, "a link holds any name of --sources");

// One line for the log about a request: "cmd=OPEN seq=3 flags=0x0001
// mode=0x0000 path=/a.atr". Unknown commands are shown by number; a datagram
// shorter than the header says so. Never reads outside `request`.
void describeTnfsRequest(rb::core::Span<const uint8_t> request, char* out, std::size_t size) noexcept;

class TnfsTracePort final : public rb::netsio::DatagramPort {
public:
    TnfsTracePort(rb::netsio::DatagramPort& inner, unsigned source, MenuEvents& events) noexcept
        : inner_(inner), source_(source), events_(events)
    {
    }

    rb::core::Result<void> send(rb::core::Span<const uint8_t> datagram) override;
    rb::core::Result<std::size_t> receive(rb::core::Span<uint8_t> out, uint32_t timeoutUs) override
    {
        return inner_.receive(out, timeoutUs);
    }

private:
    rb::netsio::DatagramPort& inner_;
    unsigned source_;
    MenuEvents& events_;
};

class UdpTnfsLink final : public TnfsLink {
public:
    // The link keeps its own copy of `server` ("" for a source not configured yet),
    // so the loader thread never reads MenuSources while the menu edits it.
    UdpTnfsLink(const char* server, unsigned source, NameResolver& resolver, MenuEvents& events) noexcept
        : udp_(resolver), trace_(udp_, source, events)
    {
        rename(server);
    }
    UdpTnfsLink(const UdpTnfsLink&) = delete;
    UdpTnfsLink& operator=(const UdpTnfsLink&) = delete;

    // NotFound when the name does not resolve (ERROR: SERVIDOR NO ENCONTRADO).
    rb::core::Result<void> open() override { return udp_.open(server_); }
    rb::netsio::DatagramPort& port() override { return trace_; }
    void rename(const char* server) noexcept override
    {
        std::snprintf(server_, sizeof server_, "%s", server != nullptr ? server : "");
    }
    const char* server() const noexcept { return server_; }

private:
    char server_[TnfsLoader::kMaxServer + 1] = {};
    UdpTnfsPort udp_;
    TnfsTracePort trace_;
};

class HostTnfsClock final : public rb::net_tnfs::TnfsClock {
public:
    explicit HostTnfsClock(rb::app::Clock& clock) noexcept : clock_(clock) {}
    uint64_t nowMicros() const override { return clock_.nowMicros(); }
    void sleepUntil(uint64_t micros) override { clock_.waitUntil(micros); }

private:
    rb::app::Clock& clock_;
};

}  // namespace rb::host
