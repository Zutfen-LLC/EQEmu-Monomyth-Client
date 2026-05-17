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

bool IsReceiveIntrospectionDevOptInPresent() noexcept {
    wchar_t value[16] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(
        L"MONOMYTH_ENABLE_RECV_INTROSPECTION",
        value,
        kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        return false;
    }

    return value[0] == L'1' && value[1] == L'\0';
}

bool IsSpellUsabilityDiscoveryDevOptInPresent() noexcept {
    wchar_t value[16] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(
        L"MONOMYTH_ENABLE_SPELL_USABILITY_DISCOVERY",
        value,
        kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        return false;
    }

    return value[0] == L'1' && value[1] == L'\0';
}

bool IsSpellUsabilityTraceDevOptInPresent() noexcept {
    wchar_t value[16] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(
        L"MONOMYTH_ENABLE_SPELL_USABILITY_TRACE",
        value,
        kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        return false;
    }

    return value[0] == L'1' && value[1] == L'\0';
}

bool IsScrollScribeTraceDevOptInPresent() noexcept {
    wchar_t value[16] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(
        L"MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE",
        value,
        kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        return false;
    }

    return value[0] == L'1' && value[1] == L'\0';
}

bool IsMulticlassSpellUsabilityDevOptInPresent() noexcept {
    wchar_t value[16] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(
        L"MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY",
        value,
        kValueCapacity);
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

bool AnySpellUsabilityDiscoveryRequestPresent(
    bool discovery_dev_opt_in,
    bool spell_trace_dev_opt_in,
    bool scroll_scribe_trace_dev_opt_in,
    bool multiclass_spell_usability_dev_opt_in) noexcept {
    return discovery_dev_opt_in ||
        spell_trace_dev_opt_in ||
        scroll_scribe_trace_dev_opt_in ||
        multiclass_spell_usability_dev_opt_in;
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
    manifest.receive_introspection_dev_opt_in = IsReceiveIntrospectionDevOptInPresent();
    manifest.receive_introspection_allowed = false;
    manifest.spell_usability_discovery_dev_opt_in = IsSpellUsabilityDiscoveryDevOptInPresent();
    manifest.spell_usability_trace_dev_opt_in = IsSpellUsabilityTraceDevOptInPresent();
    manifest.scroll_scribe_trace_dev_opt_in = IsScrollScribeTraceDevOptInPresent();
    manifest.multiclass_spell_usability_dev_opt_in =
        IsMulticlassSpellUsabilityDevOptInPresent();
    manifest.spell_usability_discovery_allowed =
        manifest.hooks_allowed &&
        manifest.fingerprint_matched &&
        AnySpellUsabilityDiscoveryRequestPresent(
            manifest.spell_usability_discovery_dev_opt_in,
            manifest.spell_usability_trace_dev_opt_in,
            manifest.scroll_scribe_trace_dev_opt_in,
            manifest.multiclass_spell_usability_dev_opt_in);
    manifest.spell_usability_trace_allowed = false;
    manifest.scroll_scribe_trace_allowed = false;
    manifest.multiclass_spell_usability_allowed = false;
    manifest.ui_hooks_allowed = false;
    manifest.heartbeat_allowed = manifest.hooks_allowed;
    manifest.reason = NormalizeReason(fingerprint.reason.c_str());
    manifest.packet_hooks_reason = manifest.packet_hooks_dev_opt_in
        ? L"receive dispatcher discovery not run"
        : L"dev opt-in absent: set MONOMYTH_ENABLE_PACKET_HOOKS=1";
    manifest.receive_introspection_reason = manifest.receive_introspection_dev_opt_in
        ? L"receive introspection requested but packet hook gate has not passed"
        : L"dev opt-in absent: set MONOMYTH_ENABLE_RECV_INTROSPECTION=1";
    manifest.spell_usability_discovery_reason = manifest.spell_usability_discovery_dev_opt_in
        ? L"spell usability discovery requested but validation has not run"
        : AnySpellUsabilityDiscoveryRequestPresent(
              false,
              manifest.spell_usability_trace_dev_opt_in,
              manifest.scroll_scribe_trace_dev_opt_in,
              manifest.multiclass_spell_usability_dev_opt_in)
        ? L"spell usability discovery requested by a dependent trace/behavior opt-in but validation has not run"
        : L"dev opt-in absent: set MONOMYTH_ENABLE_SPELL_USABILITY_DISCOVERY=1";
    manifest.spell_usability_trace_reason = manifest.spell_usability_trace_dev_opt_in
        ? L"spell usability trace requested but validated targets are not available"
        : L"dev opt-in absent: set MONOMYTH_ENABLE_SPELL_USABILITY_TRACE=1";
    manifest.scroll_scribe_trace_reason = manifest.scroll_scribe_trace_dev_opt_in
        ? L"scroll scribe trace requested but validated targets are not available"
        : L"dev opt-in absent: set MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE=1";
    manifest.multiclass_spell_usability_reason =
        manifest.multiclass_spell_usability_dev_opt_in
        ? L"multiclass spell usability requested but validated GetSpellLevelNeeded target is not available"
        : L"dev opt-in absent: set MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY=1";
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
    manifest.receive_introspection_dev_opt_in = IsReceiveIntrospectionDevOptInPresent();
    manifest.spell_usability_discovery_dev_opt_in = IsSpellUsabilityDiscoveryDevOptInPresent();
    manifest.spell_usability_trace_dev_opt_in = IsSpellUsabilityTraceDevOptInPresent();
    manifest.scroll_scribe_trace_dev_opt_in = IsScrollScribeTraceDevOptInPresent();
    manifest.multiclass_spell_usability_dev_opt_in =
        IsMulticlassSpellUsabilityDevOptInPresent();
    manifest.packet_hooks_reason = L"disabled before fingerprint/discovery gates";
    manifest.receive_introspection_reason = L"disabled before fingerprint/discovery gates";
    manifest.spell_usability_discovery_reason = L"disabled before fingerprint/discovery gates";
    manifest.spell_usability_trace_reason = L"disabled before fingerprint/discovery gates";
    manifest.scroll_scribe_trace_reason = L"disabled before fingerprint/discovery gates";
    manifest.multiclass_spell_usability_reason =
        L"disabled before fingerprint/discovery gates";
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
    AppendBoolField(
        &message,
        L"receive_introspection_dev_opt_in=",
        manifest.receive_introspection_dev_opt_in);
    message += L" ";
    AppendBoolField(
        &message,
        L"receive_introspection_allowed=",
        manifest.receive_introspection_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"spell_usability_discovery_dev_opt_in=",
        manifest.spell_usability_discovery_dev_opt_in);
    message += L" ";
    AppendBoolField(
        &message,
        L"spell_usability_discovery_allowed=",
        manifest.spell_usability_discovery_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"spell_usability_trace_dev_opt_in=",
        manifest.spell_usability_trace_dev_opt_in);
    message += L" ";
    AppendBoolField(
        &message,
        L"spell_usability_trace_allowed=",
        manifest.spell_usability_trace_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"scroll_scribe_trace_dev_opt_in=",
        manifest.scroll_scribe_trace_dev_opt_in);
    message += L" ";
    AppendBoolField(
        &message,
        L"scroll_scribe_trace_allowed=",
        manifest.scroll_scribe_trace_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"multiclass_spell_usability_dev_opt_in=",
        manifest.multiclass_spell_usability_dev_opt_in);
    message += L" ";
    AppendBoolField(
        &message,
        L"multiclass_spell_usability_allowed=",
        manifest.multiclass_spell_usability_allowed);
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
    message += L" get_spell_level_needed_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.get_spell_level_needed_state);
    message += L" handle_rbutton_up_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.handle_rbutton_up_state);
    if (manifest.handle_rbutton_up_rva != 0) {
        message += L" handle_rbutton_up_rva=";
        message += Hex32(manifest.handle_rbutton_up_rva);
    }
    if (manifest.handle_rbutton_up_address != 0) {
        message += L" handle_rbutton_up_address=";
        message += HexPtr(manifest.handle_rbutton_up_address);
    }
    if (manifest.get_spell_level_needed_rva != 0) {
        message += L" get_spell_level_needed_rva=";
        message += Hex32(manifest.get_spell_level_needed_rva);
    }
    if (manifest.get_spell_level_needed_address != 0) {
        message += L" get_spell_level_needed_address=";
        message += HexPtr(manifest.get_spell_level_needed_address);
    }
    message += L" get_usable_classes_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.get_usable_classes_state);
    if (manifest.get_usable_classes_rva != 0) {
        message += L" get_usable_classes_rva=";
        message += Hex32(manifest.get_usable_classes_rva);
    }
    if (manifest.get_usable_classes_address != 0) {
        message += L" get_usable_classes_address=";
        message += HexPtr(manifest.get_usable_classes_address);
    }
    message += L" can_equip_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.can_equip_state);
    if (manifest.can_equip_rva != 0) {
        message += L" can_equip_rva=";
        message += Hex32(manifest.can_equip_rva);
    }
    if (manifest.can_equip_address != 0) {
        message += L" can_equip_address=";
        message += HexPtr(manifest.can_equip_address);
    }
    message += L" can_start_memming_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.can_start_memming_state);
    if (manifest.can_start_memming_rva != 0) {
        message += L" can_start_memming_rva=";
        message += Hex32(manifest.can_start_memming_rva);
    }
    if (manifest.can_start_memming_address != 0) {
        message += L" can_start_memming_address=";
        message += HexPtr(manifest.can_start_memming_address);
    }
    message += L" reason=\"";
    message += NormalizeReason(manifest.reason.c_str());
    message += L"\"";
    message += L" packet_hooks_reason=\"";
    message += NormalizeReason(manifest.packet_hooks_reason.c_str());
    message += L"\"";
    message += L" receive_introspection_reason=\"";
    message += NormalizeReason(manifest.receive_introspection_reason.c_str());
    message += L"\"";
    message += L" spell_usability_discovery_reason=\"";
    message += NormalizeReason(manifest.spell_usability_discovery_reason.c_str());
    message += L"\"";
    message += L" spell_usability_trace_reason=\"";
    message += NormalizeReason(manifest.spell_usability_trace_reason.c_str());
    message += L"\"";
    message += L" scroll_scribe_trace_reason=\"";
    message += NormalizeReason(manifest.scroll_scribe_trace_reason.c_str());
    message += L"\"";
    message += L" multiclass_spell_usability_reason=\"";
    message += NormalizeReason(manifest.multiclass_spell_usability_reason.c_str());
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
    manifest->receive_introspection_allowed =
        manifest->packet_hooks_allowed &&
        manifest->receive_introspection_dev_opt_in;

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

    if (manifest->receive_introspection_allowed) {
        manifest->receive_introspection_reason =
            L"enabled by explicit dev opt-in on top of packet hook gating";
    } else if (!manifest->receive_introspection_dev_opt_in) {
        manifest->receive_introspection_reason =
            L"dev opt-in absent: set MONOMYTH_ENABLE_RECV_INTROSPECTION=1";
    } else if (!manifest->packet_hooks_allowed) {
        manifest->receive_introspection_reason =
            L"receive introspection requires packet hook gating and remains fail-closed";
    } else {
        manifest->receive_introspection_reason =
            L"receive introspection gate denied for unknown reason";
    }
}

void ApplySpellUsabilityDiscovery(
    Manifest* manifest,
    const monomyth::spell_usability_discovery::Result& discovery) noexcept {
    if (manifest == nullptr) {
        return;
    }

    manifest->spell_usability_discovery_allowed = discovery.allowed;
    manifest->spell_usability_trace_dev_opt_in = discovery.trace_dev_opt_in;
    manifest->handle_rbutton_up_state = discovery.handle_rbutton_up.state;
    manifest->handle_rbutton_up_rva = discovery.handle_rbutton_up.candidate_rva;
    manifest->handle_rbutton_up_address = discovery.handle_rbutton_up.candidate_address;
    manifest->get_spell_level_needed_state = discovery.get_spell_level_needed.state;
    manifest->get_spell_level_needed_rva = discovery.get_spell_level_needed.candidate_rva;
    manifest->get_spell_level_needed_address = discovery.get_spell_level_needed.candidate_address;
    manifest->get_usable_classes_state = discovery.get_usable_classes.state;
    manifest->get_usable_classes_rva = discovery.get_usable_classes.candidate_rva;
    manifest->get_usable_classes_address = discovery.get_usable_classes.candidate_address;
    manifest->can_equip_state = discovery.can_equip.state;
    manifest->can_equip_rva = discovery.can_equip.candidate_rva;
    manifest->can_equip_address = discovery.can_equip.candidate_address;
    manifest->can_start_memming_state = discovery.can_start_memming.state;
    manifest->can_start_memming_rva = discovery.can_start_memming.candidate_rva;
    manifest->can_start_memming_address = discovery.can_start_memming.candidate_address;
    manifest->scroll_scribe_trace_dev_opt_in = IsScrollScribeTraceDevOptInPresent();
    manifest->multiclass_spell_usability_dev_opt_in =
        IsMulticlassSpellUsabilityDevOptInPresent();

    const bool any_trace_safe =
        (discovery.get_spell_level_needed.state ==
             monomyth::spell_usability_discovery::TargetState::kValidated &&
         discovery.get_spell_level_needed.trace_safe) ||
        (discovery.can_start_memming.state ==
             monomyth::spell_usability_discovery::TargetState::kValidated &&
         discovery.can_start_memming.trace_safe);
    const bool scroll_scribe_targets_validated =
        discovery.handle_rbutton_up.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.handle_rbutton_up.trace_safe &&
        discovery.get_usable_classes.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.get_usable_classes.trace_safe &&
        discovery.can_equip.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.can_equip.trace_safe;
    manifest->spell_usability_trace_allowed =
        discovery.allowed &&
        discovery.trace_dev_opt_in &&
        any_trace_safe;
    manifest->scroll_scribe_trace_allowed =
        discovery.allowed &&
        manifest->scroll_scribe_trace_dev_opt_in &&
        scroll_scribe_targets_validated;
    manifest->multiclass_spell_usability_allowed =
        discovery.allowed &&
        manifest->multiclass_spell_usability_dev_opt_in &&
        discovery.get_spell_level_needed.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.get_spell_level_needed.trace_safe;

    if (!discovery.allowed) {
        manifest->spell_usability_discovery_reason =
            L"capability guard denied spell usability discovery";
    } else {
        manifest->spell_usability_discovery_reason =
            NormalizeReason(discovery.reason.c_str());
    }

    if (manifest->spell_usability_trace_allowed) {
        manifest->spell_usability_trace_reason =
            L"enabled by explicit dev opt-in and validated spell usability targets";
    } else if (!discovery.trace_dev_opt_in) {
        manifest->spell_usability_trace_reason =
            L"dev opt-in absent: set MONOMYTH_ENABLE_SPELL_USABILITY_TRACE=1";
    } else if (!discovery.allowed) {
        manifest->spell_usability_trace_reason =
            L"spell usability trace requires the ROF2 discovery capability gate";
    } else if (!any_trace_safe) {
        manifest->spell_usability_trace_reason =
            L"spell usability trace denied because no target reached validated trace-safe state";
    } else {
        manifest->spell_usability_trace_reason =
            L"spell usability trace gate denied for unknown reason";
    }

    if (manifest->scroll_scribe_trace_allowed) {
        manifest->scroll_scribe_trace_reason =
            L"enabled by explicit dev opt-in and validated scroll scribe trace targets";
    } else if (!manifest->scroll_scribe_trace_dev_opt_in) {
        manifest->scroll_scribe_trace_reason =
            L"dev opt-in absent: set MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE=1";
    } else if (!discovery.allowed) {
        manifest->scroll_scribe_trace_reason =
            L"scroll scribe trace requires the ROF2 discovery capability gate";
    } else if (!scroll_scribe_targets_validated) {
        manifest->scroll_scribe_trace_reason =
            L"scroll scribe trace denied because one or more target validations were missing or ambiguous";
    } else {
        manifest->scroll_scribe_trace_reason =
            L"scroll scribe trace gate denied for unknown reason";
    }

    if (manifest->multiclass_spell_usability_allowed) {
        manifest->multiclass_spell_usability_reason =
            L"enabled by explicit dev opt-in and validated GetSpellLevelNeeded target";
    } else if (!manifest->multiclass_spell_usability_dev_opt_in) {
        manifest->multiclass_spell_usability_reason =
            L"dev opt-in absent: set MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY=1";
    } else if (!discovery.allowed) {
        manifest->multiclass_spell_usability_reason =
            L"multiclass spell usability requires the ROF2 discovery capability gate";
    } else if (
        discovery.get_spell_level_needed.state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        !discovery.get_spell_level_needed.trace_safe) {
        manifest->multiclass_spell_usability_reason =
            L"multiclass spell usability denied because GetSpellLevelNeeded is not validated trace-safe";
    } else {
        manifest->multiclass_spell_usability_reason =
            L"multiclass spell usability gate denied for unknown reason";
    }
}

}  // namespace monomyth::runtime
