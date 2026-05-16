#pragma once

#include <string>

#include "fingerprint.h"

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

}  // namespace monomyth::runtime
