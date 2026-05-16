#include "runtime_capabilities.h"

#include <windows.h>

#include <sstream>
#include <string>

#include "logger.h"

namespace monomyth::runtime {
namespace {

std::wstring NormalizeReason(const wchar_t* reason) {
    if (reason == nullptr || reason[0] == L'\0') {
        return L"unknown";
    }

    return reason;
}

void AppendBoolField(std::wstring* message, const wchar_t* field, bool value) {
    message->append(field);
    message->append(value ? L"true" : L"false");
}

std::wstring Hex32(std::uint32_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring HexPtr(std::uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

bool IsPacketHookDevOptInPresent() noexcept {
    wchar_t value[16] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length =
        GetEnvironmentVariableW(L"MONOMYTH_ENABLE_PACKET_HOOKS", value, kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        return false;
    }

    return value[0] == L'1' && value[1] == L'\0';
}

std::wstring PacketHookDiscoveryReason(
    const monomyth::receive_dispatch_discovery::Result& discovery) {
    std::wstring reason = L"receive dispatcher discovery not validated";
    reason += L" state=";
    reason += monomyth::receive_dispatch_discovery::StateName(discovery.state);
    if (!discovery.reason.empty()) {
        reason += L" reason=\"";
        reason += discovery.reason;
        reason += L"\"";
    }
    return reason;
}

}  // namespace

Manifest BuildCapabilityManifest(
    bool proxy_loaded,
    bool proxy_ready,
    bool fingerprint_checked,
    const monomyth::fingerprint::Result& fingerprint) noexcept {
    Manifest manifest = {};
    manifest.proxy_loaded = proxy_loaded;
    manifest.proxy_ready = proxy_ready;
    manifest.host_process_supported = fingerprint.process_name_match;
    manifest.fingerprint_checked = fingerprint_checked;
    manifest.fingerprint_matched = fingerprint.process_name_match && fingerprint.matched;
    manifest.fingerprint_method = fingerprint.method;
    manifest.hooks_allowed = proxy_ready && fingerprint.hooks_allowed;
    manifest.packet_hooks_dev_opt_in = IsPacketHookDevOptInPresent();
    manifest.packet_hooks_allowed = false;
    manifest.ui_hooks_allowed = false;
    manifest.heartbeat_allowed = manifest.hooks_allowed;
    manifest.reason = NormalizeReason(fingerprint.reason.c_str());
    manifest.packet_hooks_reason = manifest.packet_hooks_dev_opt_in
        ? L"receive dispatcher discovery not run"
        : L"dev opt-in absent: set MONOMYTH_ENABLE_PACKET_HOOKS=1";
    return manifest;
}

Manifest BuildDisabledCapabilityManifest(
    bool proxy_loaded,
    bool proxy_ready,
    const wchar_t* reason) noexcept {
    Manifest manifest = {};
    manifest.proxy_loaded = proxy_loaded;
    manifest.proxy_ready = proxy_ready;
    manifest.reason = NormalizeReason(reason);
    manifest.packet_hooks_dev_opt_in = IsPacketHookDevOptInPresent();
    manifest.packet_hooks_reason = L"disabled before fingerprint/discovery gates";
    return manifest;
}

void LogCapabilityManifest(const Manifest& manifest) noexcept {
    std::wstring message = L"CapabilityManifest ";
    AppendBoolField(&message, L"proxy_loaded=", manifest.proxy_loaded);
    message += L" ";
    AppendBoolField(&message, L"proxy_ready=", manifest.proxy_ready);
    message += L" ";
    AppendBoolField(&message, L"host_process_supported=", manifest.host_process_supported);
    message += L" ";
    AppendBoolField(&message, L"fingerprint_checked=", manifest.fingerprint_checked);
    message += L" ";
    AppendBoolField(&message, L"fingerprint_matched=", manifest.fingerprint_matched);
    message += L" fingerprint_method=";
    message += monomyth::fingerprint::MethodName(manifest.fingerprint_method);
    message += L" ";
    AppendBoolField(&message, L"hooks_allowed=", manifest.hooks_allowed);
    message += L" ";
    AppendBoolField(&message, L"packet_hooks_dev_opt_in=", manifest.packet_hooks_dev_opt_in);
    message += L" ";
    AppendBoolField(&message, L"packet_hooks_allowed=", manifest.packet_hooks_allowed);
    message += L" ";
    AppendBoolField(&message, L"ui_hooks_allowed=", manifest.ui_hooks_allowed);
    message += L" ";
    AppendBoolField(&message, L"heartbeat_allowed=", manifest.heartbeat_allowed);
    message += L" receive_dispatch_discovery=";
    message += monomyth::receive_dispatch_discovery::StateName(
        manifest.receive_dispatch_discovery_state);
    message += L" ";
    AppendBoolField(&message, L"receive_dispatch_validated=", manifest.receive_dispatch_validated);
    if (manifest.runtime_module_base != 0) {
        message += L" runtime_module_base=";
        message += HexPtr(manifest.runtime_module_base);
    }
    if (manifest.receive_dispatch_rva != 0) {
        message += L" receive_dispatch_rva=";
        message += Hex32(manifest.receive_dispatch_rva);
    }
    if (manifest.receive_dispatch_address != 0) {
        message += L" receive_dispatch_address=";
        message += HexPtr(manifest.receive_dispatch_address);
    }
    message += L" reason=\"";
    message += NormalizeReason(manifest.reason.c_str());
    message += L"\"";
    message += L" packet_hooks_reason=\"";
    message += NormalizeReason(manifest.packet_hooks_reason.c_str());
    message += L"\"";
    monomyth::logger::Log(message);
}

void ApplyReceiveDispatchDiscovery(
    Manifest* manifest,
    const monomyth::receive_dispatch_discovery::Result& discovery) noexcept {
    if (manifest == nullptr) {
        return;
    }

    manifest->receive_dispatch_discovery_state = discovery.state;
    manifest->receive_dispatch_validated = discovery.validated;
    manifest->runtime_module_base = discovery.module_base;
    manifest->receive_dispatch_rva = discovery.candidate_rva;
    manifest->receive_dispatch_address = discovery.candidate_address;

    manifest->packet_hooks_allowed =
        manifest->proxy_ready &&
        manifest->hooks_allowed &&
        manifest->fingerprint_matched &&
        discovery.validated &&
        manifest->packet_hooks_dev_opt_in;

    if (manifest->packet_hooks_allowed) {
        manifest->packet_hooks_reason =
            L"enabled by explicit dev opt-in, ROF2 fingerprint, and receive dispatcher validation";
    } else if (!manifest->packet_hooks_dev_opt_in) {
        manifest->packet_hooks_reason =
            L"dev opt-in absent: set MONOMYTH_ENABLE_PACKET_HOOKS=1";
    } else if (!manifest->proxy_ready) {
        manifest->packet_hooks_reason = L"proxy is not ready";
    } else if (!manifest->hooks_allowed || !manifest->fingerprint_matched) {
        manifest->packet_hooks_reason = L"ROF2 fingerprint/host guard denied hook capability";
    } else if (!discovery.validated) {
        manifest->packet_hooks_reason = PacketHookDiscoveryReason(discovery);
    } else {
        manifest->packet_hooks_reason = L"packet hook gate denied for unknown reason";
    }
}

}  // namespace monomyth::runtime
