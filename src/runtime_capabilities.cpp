#include "runtime_capabilities.h"

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
    message += L" reason=\"";
    message += NormalizeReason(manifest.reason.c_str());
    message += L"\"";
    monomyth::logger::Log(message);
}

}  // namespace monomyth::runtime
