#pragma once

#include <cstdint>
#include <string>

#include "fingerprint.h"
#include "receive_dispatch_discovery.h"

namespace monomyth::runtime {

struct Manifest {
    bool proxy_loaded = false;
    bool proxy_ready = false;
    bool host_process_supported = false;
    bool fingerprint_checked = false;
    bool fingerprint_matched = false;
    bool hooks_allowed = false;
    bool packet_hooks_allowed = false;
    bool ui_hooks_allowed = false;
    bool heartbeat_allowed = false;
    monomyth::receive_dispatch_discovery::State receive_dispatch_discovery_state =
        monomyth::receive_dispatch_discovery::State::kUnavailable;
    bool receive_dispatch_validated = false;
    std::uint32_t receive_dispatch_rva = 0;
    std::uintptr_t receive_dispatch_address = 0;
    std::wstring reason = L"manifest unavailable";
};

Manifest BuildCapabilityManifest(
    bool proxy_loaded,
    bool proxy_ready,
    bool fingerprint_checked,
    const monomyth::fingerprint::Result& fingerprint) noexcept;

Manifest BuildDisabledCapabilityManifest(
    bool proxy_loaded,
    bool proxy_ready,
    const wchar_t* reason) noexcept;

void LogCapabilityManifest(const Manifest& manifest) noexcept;

void ApplyReceiveDispatchDiscovery(
    Manifest* manifest,
    const monomyth::receive_dispatch_discovery::Result& discovery) noexcept;

}  // namespace monomyth::runtime
