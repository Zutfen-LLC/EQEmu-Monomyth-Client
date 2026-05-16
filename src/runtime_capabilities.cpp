#include "runtime_capabilities.h"

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
    manifest.fingerprint_matched =
        fingerprint.process_name_match &&
        fingerprint.version_strings_checked &&
        fingerprint.version_strings_match;
    manifest.hooks_allowed = proxy_ready && fingerprint.hooks_allowed;
    manifest.packet_hooks_allowed = false;
    manifest.ui_hooks_allowed = false;
    manifest.heartbeat_allowed = manifest.hooks_allowed;
    manifest.reason = NormalizeReason(fingerprint.reason.c_str());
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
    message += L" ";
    AppendBoolField(&message, L"hooks_allowed=", manifest.hooks_allowed);
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
    if (manifest.receive_dispatch_validated) {
        message += L" receive_dispatch_rva=";
        message += Hex32(manifest.receive_dispatch_rva);
        message += L" receive_dispatch_address=";
        message += HexPtr(manifest.receive_dispatch_address);
    }
    message += L" reason=\"";
    message += NormalizeReason(manifest.reason.c_str());
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
    if (discovery.validated) {
        manifest->receive_dispatch_rva = discovery.candidate_rva;
        manifest->receive_dispatch_address = discovery.candidate_address;
    } else {
        manifest->receive_dispatch_rva = 0;
        manifest->receive_dispatch_address = 0;
    }

    manifest->packet_hooks_allowed = false;
}

}  // namespace monomyth::runtime
