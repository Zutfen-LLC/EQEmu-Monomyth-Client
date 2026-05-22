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

void AppendTargetSourceAndFailure(
    std::wstring* message,
    const wchar_t* prefix,
    const std::wstring& evidence_source,
    const std::wstring& failure_reason) {
    if (message == nullptr || prefix == nullptr) {
        return;
    }

    message->append(L" ");
    message->append(prefix);
    message->append(L"_evidence_source=");
    message->append(NormalizeReason(evidence_source.c_str()));
    message->append(L" ");
    message->append(prefix);
    message->append(L"_failure_reason=");
    message->append(NormalizeReason(failure_reason.c_str()));
}

std::wstring DescribeTargetFailure(
    const wchar_t* target,
    monomyth::spell_usability_discovery::TargetState state,
    bool hook_safe,
    const std::wstring& failure_reason) {
    if (state == monomyth::spell_usability_discovery::TargetState::kValidated && hook_safe) {
        return L"";
    }

    std::wstring message = target == nullptr ? L"target" : target;
    if (state == monomyth::spell_usability_discovery::TargetState::kValidated && !hook_safe) {
        message += L" failure_reason=hook_unsafe";
        return message;
    }
    message += L" failure_reason=";
    message += NormalizeReason(failure_reason.c_str());
    return message;
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
    manifest.packet_hooks_dev_opt_in = false;
    manifest.packet_hooks_allowed = false;
    manifest.full_packet_trace_dev_opt_in = false;
    manifest.full_packet_trace_allowed = false;
    manifest.receive_introspection_dev_opt_in = false;
    manifest.receive_introspection_allowed = false;
    manifest.spell_usability_discovery_dev_opt_in = false;
    manifest.spell_usability_trace_dev_opt_in = false;
    manifest.scroll_scribe_trace_dev_opt_in = false;
    manifest.memorize_send_trace_dev_opt_in = false;
    manifest.multiclass_spell_usability_dev_opt_in = false;
    manifest.spell_usability_discovery_allowed =
        manifest.hooks_allowed &&
        manifest.fingerprint_matched;
    manifest.class_display_discovery_allowed =
        manifest.hooks_allowed &&
        manifest.fingerprint_matched;
    manifest.spell_usability_trace_allowed = false;
    manifest.scroll_scribe_trace_allowed = false;
    manifest.memorize_send_trace_allowed = false;
    manifest.multiclass_spell_usability_allowed = false;
    manifest.multiclass_item_usability_allowed = false;
    manifest.multiclass_ui_display_allowed = false;
    manifest.ui_hooks_allowed = false;
    manifest.heartbeat_allowed = manifest.hooks_allowed;
    manifest.reason = NormalizeReason(fingerprint.reason.c_str());
    manifest.packet_hooks_reason =
        L"validated receive dispatcher capture pending for native server auth stats observation";
    manifest.full_packet_trace_reason =
        L"full packet tracing retired; capability disabled by default";
    manifest.receive_introspection_reason =
        L"receive introspection retired; capability disabled by default";
    manifest.spell_usability_discovery_reason = manifest.spell_usability_discovery_allowed
        ? L"validated ROF2 spell usability discovery pending target resolution"
        : L"spell usability discovery requires validated ROF2 fingerprint and hook allowance";
    manifest.class_display_discovery_reason = manifest.class_display_discovery_allowed
        ? L"validated ROF2 class display discovery pending target resolution"
        : L"class display discovery requires validated ROF2 fingerprint and hook allowance";
    manifest.spell_usability_trace_reason =
        L"spell usability tracing retired; re-add explicitly if needed";
    manifest.scroll_scribe_trace_reason =
        L"scroll scribe tracing retired; re-add explicitly if needed";
    manifest.memorize_send_trace_reason =
        L"memorize send tracing retired; re-add explicitly if needed";
    manifest.multiclass_spell_usability_reason =
        L"default ROF2 multiclass spell usability pending target validation";
    manifest.multiclass_item_usability_reason =
        L"default ROF2 multiclass item usability pending target validation";
    manifest.multiclass_ui_display_reason =
        L"default ROF2 multiclass UI display pending target validation";
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
    manifest.packet_hooks_dev_opt_in = false;
    manifest.full_packet_trace_dev_opt_in = false;
    manifest.receive_introspection_dev_opt_in = false;
    manifest.spell_usability_discovery_dev_opt_in = false;
    manifest.spell_usability_trace_dev_opt_in = false;
    manifest.scroll_scribe_trace_dev_opt_in = false;
    manifest.memorize_send_trace_dev_opt_in = false;
    manifest.multiclass_spell_usability_dev_opt_in = false;
    manifest.packet_hooks_reason =
        L"receive dispatcher capture disabled before fingerprint/discovery gates";
    manifest.full_packet_trace_reason =
        L"full packet tracing retired; disabled before fingerprint/discovery gates";
    manifest.receive_introspection_reason =
        L"receive introspection retired; disabled before fingerprint/discovery gates";
    manifest.spell_usability_discovery_reason =
        L"spell usability discovery disabled before fingerprint/discovery gates";
    manifest.class_display_discovery_reason =
        L"class display discovery disabled before fingerprint/discovery gates";
    manifest.spell_usability_trace_reason =
        L"spell usability tracing retired; disabled before fingerprint/discovery gates";
    manifest.scroll_scribe_trace_reason =
        L"scroll scribe tracing retired; disabled before fingerprint/discovery gates";
    manifest.memorize_send_trace_reason =
        L"memorize send tracing retired; disabled before fingerprint/discovery gates";
    manifest.multiclass_spell_usability_reason =
        L"default multiclass spell usability disabled before fingerprint/discovery gates";
    manifest.multiclass_item_usability_reason =
        L"default multiclass item usability disabled before fingerprint/discovery gates";
    manifest.multiclass_ui_display_reason =
        L"default multiclass UI display disabled before fingerprint/discovery gates";
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
    AppendBoolField(&message, L"packet_hooks_allowed=", manifest.packet_hooks_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"full_packet_trace_allowed=",
        manifest.full_packet_trace_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"receive_introspection_allowed=",
        manifest.receive_introspection_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"spell_usability_discovery_allowed=",
        manifest.spell_usability_discovery_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"class_display_discovery_allowed=",
        manifest.class_display_discovery_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"spell_usability_trace_allowed=",
        manifest.spell_usability_trace_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"scroll_scribe_trace_allowed=",
        manifest.scroll_scribe_trace_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"memorize_send_trace_allowed=",
        manifest.memorize_send_trace_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"multiclass_spell_usability_allowed=",
        manifest.multiclass_spell_usability_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"multiclass_item_usability_allowed=",
        manifest.multiclass_item_usability_allowed);
    message += L" ";
    AppendBoolField(
        &message,
        L"multiclass_ui_display_allowed=",
        manifest.multiclass_ui_display_allowed);
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
    AppendTargetSourceAndFailure(
        &message,
        L"get_spell_level_needed",
        manifest.get_spell_level_needed_evidence_source,
        manifest.get_spell_level_needed_failure_reason);
    message += L" handle_rbutton_up_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.handle_rbutton_up_state);
    AppendTargetSourceAndFailure(
        &message,
        L"handle_rbutton_up",
        manifest.handle_rbutton_up_evidence_source,
        manifest.handle_rbutton_up_failure_reason);
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
    message += L" is_class_usable_predicate_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.is_class_usable_predicate_state);
    AppendTargetSourceAndFailure(
        &message,
        L"is_class_usable_predicate",
        manifest.is_class_usable_predicate_evidence_source,
        manifest.is_class_usable_predicate_failure_reason);
    if (manifest.is_class_usable_predicate_rva != 0) {
        message += L" is_class_usable_predicate_rva=";
        message += Hex32(manifest.is_class_usable_predicate_rva);
    }
    if (manifest.is_class_usable_predicate_address != 0) {
        message += L" is_class_usable_predicate_address=";
        message += HexPtr(manifest.is_class_usable_predicate_address);
    }
    message += L" can_equip_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.can_equip_state);
    AppendTargetSourceAndFailure(
        &message,
        L"can_equip",
        manifest.can_equip_evidence_source,
        manifest.can_equip_failure_reason);
    if (manifest.can_equip_rva != 0) {
        message += L" can_equip_rva=";
        message += Hex32(manifest.can_equip_rva);
    }
    if (manifest.can_equip_address != 0) {
        message += L" can_equip_address=";
        message += HexPtr(manifest.can_equip_address);
    }
    message += L" inv_slot_mgr_move_item_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.inv_slot_mgr_move_item_state);
    AppendTargetSourceAndFailure(
        &message,
        L"inv_slot_mgr_move_item",
        manifest.inv_slot_mgr_move_item_evidence_source,
        manifest.inv_slot_mgr_move_item_failure_reason);
    if (manifest.inv_slot_mgr_move_item_rva != 0) {
        message += L" inv_slot_mgr_move_item_rva=";
        message += Hex32(manifest.inv_slot_mgr_move_item_rva);
    }
    if (manifest.inv_slot_mgr_move_item_address != 0) {
        message += L" inv_slot_mgr_move_item_address=";
        message += HexPtr(manifest.inv_slot_mgr_move_item_address);
    }
    message += L" spellbook_dispatcher_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.spellbook_dispatcher_state);
    AppendTargetSourceAndFailure(
        &message,
        L"spellbook_dispatcher",
        manifest.spellbook_dispatcher_evidence_source,
        manifest.spellbook_dispatcher_failure_reason);
    if (manifest.spellbook_dispatcher_rva != 0) {
        message += L" spellbook_dispatcher_rva=";
        message += Hex32(manifest.spellbook_dispatcher_rva);
    }
    if (manifest.spellbook_dispatcher_address != 0) {
        message += L" spellbook_dispatcher_address=";
        message += HexPtr(manifest.spellbook_dispatcher_address);
    }
    message += L" start_spell_scribe_path_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_path_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_path",
        manifest.start_spell_scribe_path_evidence_source,
        manifest.start_spell_scribe_path_failure_reason);
    if (manifest.start_spell_scribe_path_rva != 0) {
        message += L" start_spell_scribe_path_rva=";
        message += Hex32(manifest.start_spell_scribe_path_rva);
    }
    if (manifest.start_spell_scribe_path_address != 0) {
        message += L" start_spell_scribe_path_address=";
        message += HexPtr(manifest.start_spell_scribe_path_address);
    }
    message += L" start_spell_scribe_precheck_mode_getter_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_mode_getter_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_mode_getter",
        manifest.start_spell_scribe_precheck_mode_getter_evidence_source,
        manifest.start_spell_scribe_precheck_mode_getter_failure_reason);
    if (manifest.start_spell_scribe_precheck_mode_getter_rva != 0) {
        message += L" start_spell_scribe_precheck_mode_getter_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_mode_getter_rva);
    }
    if (manifest.start_spell_scribe_precheck_mode_getter_address != 0) {
        message += L" start_spell_scribe_precheck_mode_getter_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_mode_getter_address);
    }
    message += L" start_spell_scribe_precheck_gate_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_gate_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_gate",
        manifest.start_spell_scribe_precheck_gate_evidence_source,
        manifest.start_spell_scribe_precheck_gate_failure_reason);
    if (manifest.start_spell_scribe_precheck_gate_rva != 0) {
        message += L" start_spell_scribe_precheck_gate_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_gate_rva);
    }
    if (manifest.start_spell_scribe_precheck_gate_address != 0) {
        message += L" start_spell_scribe_precheck_gate_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_gate_address);
    }
    message += L" start_spell_scribe_precheck_lookup_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_lookup_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_lookup",
        manifest.start_spell_scribe_precheck_lookup_evidence_source,
        manifest.start_spell_scribe_precheck_lookup_failure_reason);
    if (manifest.start_spell_scribe_precheck_lookup_rva != 0) {
        message += L" start_spell_scribe_precheck_lookup_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_lookup_rva);
    }
    if (manifest.start_spell_scribe_precheck_lookup_address != 0) {
        message += L" start_spell_scribe_precheck_lookup_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_lookup_address);
    }
    message += L" start_spell_scribe_precheck_fast_accept_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_fast_accept_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_fast_accept",
        manifest.start_spell_scribe_precheck_fast_accept_evidence_source,
        manifest.start_spell_scribe_precheck_fast_accept_failure_reason);
    if (manifest.start_spell_scribe_precheck_fast_accept_rva != 0) {
        message += L" start_spell_scribe_precheck_fast_accept_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_fast_accept_rva);
    }
    if (manifest.start_spell_scribe_precheck_fast_accept_address != 0) {
        message += L" start_spell_scribe_precheck_fast_accept_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_fast_accept_address);
    }
    message += L" start_spell_scribe_precheck_class_resolver_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_class_resolver_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_class_resolver",
        manifest.start_spell_scribe_precheck_class_resolver_evidence_source,
        manifest.start_spell_scribe_precheck_class_resolver_failure_reason);
    if (manifest.start_spell_scribe_precheck_class_resolver_rva != 0) {
        message += L" start_spell_scribe_precheck_class_resolver_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_class_resolver_rva);
    }
    if (manifest.start_spell_scribe_precheck_class_resolver_address != 0) {
        message += L" start_spell_scribe_precheck_class_resolver_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_class_resolver_address);
    }
    message += L" start_spell_scribe_precheck_assigned_mask_getter_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_assigned_mask_getter_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_assigned_mask_getter",
        manifest.start_spell_scribe_precheck_assigned_mask_getter_evidence_source,
        manifest.start_spell_scribe_precheck_assigned_mask_getter_failure_reason);
    if (manifest.start_spell_scribe_precheck_assigned_mask_getter_rva != 0) {
        message += L" start_spell_scribe_precheck_assigned_mask_getter_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_assigned_mask_getter_rva);
    }
    if (manifest.start_spell_scribe_precheck_assigned_mask_getter_address != 0) {
        message += L" start_spell_scribe_precheck_assigned_mask_getter_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_assigned_mask_getter_address);
    }
    message += L" start_spell_scribe_precheck_rule_4462c0_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_rule_4462c0_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_rule_4462c0",
        manifest.start_spell_scribe_precheck_rule_4462c0_evidence_source,
        manifest.start_spell_scribe_precheck_rule_4462c0_failure_reason);
    if (manifest.start_spell_scribe_precheck_rule_4462c0_rva != 0) {
        message += L" start_spell_scribe_precheck_rule_4462c0_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_rule_4462c0_rva);
    }
    if (manifest.start_spell_scribe_precheck_rule_4462c0_address != 0) {
        message += L" start_spell_scribe_precheck_rule_4462c0_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_rule_4462c0_address);
    }
    message += L" start_spell_scribe_precheck_rule_446190_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_rule_446190_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_rule_446190",
        manifest.start_spell_scribe_precheck_rule_446190_evidence_source,
        manifest.start_spell_scribe_precheck_rule_446190_failure_reason);
    if (manifest.start_spell_scribe_precheck_rule_446190_rva != 0) {
        message += L" start_spell_scribe_precheck_rule_446190_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_rule_446190_rva);
    }
    if (manifest.start_spell_scribe_precheck_rule_446190_address != 0) {
        message += L" start_spell_scribe_precheck_rule_446190_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_rule_446190_address);
    }
    message += L" start_spell_scribe_precheck_rule_446200_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_rule_446200_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_rule_446200",
        manifest.start_spell_scribe_precheck_rule_446200_evidence_source,
        manifest.start_spell_scribe_precheck_rule_446200_failure_reason);
    if (manifest.start_spell_scribe_precheck_rule_446200_rva != 0) {
        message += L" start_spell_scribe_precheck_rule_446200_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_rule_446200_rva);
    }
    if (manifest.start_spell_scribe_precheck_rule_446200_address != 0) {
        message += L" start_spell_scribe_precheck_rule_446200_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_rule_446200_address);
    }
    message += L" start_spell_scribe_precheck_rule_446380_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_scribe_precheck_rule_446380_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_scribe_precheck_rule_446380",
        manifest.start_spell_scribe_precheck_rule_446380_evidence_source,
        manifest.start_spell_scribe_precheck_rule_446380_failure_reason);
    if (manifest.start_spell_scribe_precheck_rule_446380_rva != 0) {
        message += L" start_spell_scribe_precheck_rule_446380_rva=";
        message += Hex32(manifest.start_spell_scribe_precheck_rule_446380_rva);
    }
    if (manifest.start_spell_scribe_precheck_rule_446380_address != 0) {
        message += L" start_spell_scribe_precheck_rule_446380_address=";
        message += HexPtr(manifest.start_spell_scribe_precheck_rule_446380_address);
    }
    message += L" can_start_memming_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.can_start_memming_state);
    AppendTargetSourceAndFailure(
        &message,
        L"can_start_memming",
        manifest.can_start_memming_evidence_source,
        manifest.can_start_memming_failure_reason);
    if (manifest.can_start_memming_rva != 0) {
        message += L" can_start_memming_rva=";
        message += Hex32(manifest.can_start_memming_rva);
    }
    if (manifest.can_start_memming_address != 0) {
        message += L" can_start_memming_address=";
        message += HexPtr(manifest.can_start_memming_address);
    }
    message += L" spellbook_memorize_send_path_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.spellbook_memorize_send_path_state);
    AppendTargetSourceAndFailure(
        &message,
        L"spellbook_memorize_send_path",
        manifest.spellbook_memorize_send_path_evidence_source,
        manifest.spellbook_memorize_send_path_failure_reason);
    if (manifest.spellbook_memorize_send_path_rva != 0) {
        message += L" spellbook_memorize_send_path_rva=";
        message += Hex32(manifest.spellbook_memorize_send_path_rva);
    }
    if (manifest.spellbook_memorize_send_path_address != 0) {
        message += L" spellbook_memorize_send_path_address=";
        message += HexPtr(manifest.spellbook_memorize_send_path_address);
    }
    message += L" start_spell_memorization_path_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.start_spell_memorization_path_state);
    AppendTargetSourceAndFailure(
        &message,
        L"start_spell_memorization_path",
        manifest.start_spell_memorization_path_evidence_source,
        manifest.start_spell_memorization_path_failure_reason);
    if (manifest.start_spell_memorization_path_rva != 0) {
        message += L" start_spell_memorization_path_rva=";
        message += Hex32(manifest.start_spell_memorization_path_rva);
    }
    if (manifest.start_spell_memorization_path_address != 0) {
        message += L" start_spell_memorization_path_address=";
        message += HexPtr(manifest.start_spell_memorization_path_address);
    }
    message += L" memorize_send_packet_wrapper_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.memorize_send_packet_wrapper_state);
    AppendTargetSourceAndFailure(
        &message,
        L"memorize_send_packet_wrapper",
        manifest.memorize_send_packet_wrapper_evidence_source,
        manifest.memorize_send_packet_wrapper_failure_reason);
    if (manifest.memorize_send_packet_wrapper_rva != 0) {
        message += L" memorize_send_packet_wrapper_rva=";
        message += Hex32(manifest.memorize_send_packet_wrapper_rva);
    }
    if (manifest.memorize_send_packet_wrapper_address != 0) {
        message += L" memorize_send_packet_wrapper_address=";
        message += HexPtr(manifest.memorize_send_packet_wrapper_address);
    }
    message += L" who_class_name_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.who_class_name_state);
    AppendTargetSourceAndFailure(
        &message,
        L"who_class_name",
        manifest.who_class_name_evidence_source,
        manifest.who_class_name_failure_reason);
    if (manifest.who_class_name_rva != 0) {
        message += L" who_class_name_rva=";
        message += Hex32(manifest.who_class_name_rva);
    }
    if (manifest.who_class_name_address != 0) {
        message += L" who_class_name_address=";
        message += HexPtr(manifest.who_class_name_address);
    }
    message += L" get_class_desc_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.get_class_desc_state);
    AppendTargetSourceAndFailure(
        &message,
        L"get_class_desc",
        manifest.get_class_desc_evidence_source,
        manifest.get_class_desc_failure_reason);
    if (manifest.get_class_desc_rva != 0) {
        message += L" get_class_desc_rva=";
        message += Hex32(manifest.get_class_desc_rva);
    }
    if (manifest.get_class_desc_address != 0) {
        message += L" get_class_desc_address=";
        message += HexPtr(manifest.get_class_desc_address);
    }
    message += L" get_class_three_letter_code_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.get_class_three_letter_code_state);
    AppendTargetSourceAndFailure(
        &message,
        L"get_class_three_letter_code",
        manifest.get_class_three_letter_code_evidence_source,
        manifest.get_class_three_letter_code_failure_reason);
    if (manifest.get_class_three_letter_code_rva != 0) {
        message += L" get_class_three_letter_code_rva=";
        message += Hex32(manifest.get_class_three_letter_code_rva);
    }
    if (manifest.get_class_three_letter_code_address != 0) {
        message += L" get_class_three_letter_code_address=";
        message += HexPtr(manifest.get_class_three_letter_code_address);
    }
    message += L" char_select_class_name_func_state=";
    message += monomyth::spell_usability_discovery::TargetStateName(
        manifest.char_select_class_name_func_state);
    AppendTargetSourceAndFailure(
        &message,
        L"char_select_class_name_func",
        manifest.char_select_class_name_func_evidence_source,
        manifest.char_select_class_name_func_failure_reason);
    if (manifest.char_select_class_name_func_rva != 0) {
        message += L" char_select_class_name_func_rva=";
        message += Hex32(manifest.char_select_class_name_func_rva);
    }
    if (manifest.char_select_class_name_func_address != 0) {
        message += L" char_select_class_name_func_address=";
        message += HexPtr(manifest.char_select_class_name_func_address);
    }
    message += L" reason=\"";
    message += NormalizeReason(manifest.reason.c_str());
    message += L"\"";
    message += L" packet_hooks_reason=\"";
    message += NormalizeReason(manifest.packet_hooks_reason.c_str());
    message += L"\"";
    message += L" full_packet_trace_reason=\"";
    message += NormalizeReason(manifest.full_packet_trace_reason.c_str());
    message += L"\"";
    message += L" receive_introspection_reason=\"";
    message += NormalizeReason(manifest.receive_introspection_reason.c_str());
    message += L"\"";
    message += L" spell_usability_discovery_reason=\"";
    message += NormalizeReason(manifest.spell_usability_discovery_reason.c_str());
    message += L"\"";
    message += L" class_display_discovery_reason=\"";
    message += NormalizeReason(manifest.class_display_discovery_reason.c_str());
    message += L"\"";
    message += L" spell_usability_trace_reason=\"";
    message += NormalizeReason(manifest.spell_usability_trace_reason.c_str());
    message += L"\"";
    message += L" scroll_scribe_trace_reason=\"";
    message += NormalizeReason(manifest.scroll_scribe_trace_reason.c_str());
    message += L"\"";
    message += L" memorize_send_trace_reason=\"";
    message += NormalizeReason(manifest.memorize_send_trace_reason.c_str());
    message += L"\"";
    message += L" multiclass_spell_usability_reason=\"";
    message += NormalizeReason(manifest.multiclass_spell_usability_reason.c_str());
    message += L"\"";
    message += L" multiclass_item_usability_reason=\"";
    message += NormalizeReason(manifest.multiclass_item_usability_reason.c_str());
    message += L"\"";
    message += L" multiclass_ui_display_reason=\"";
    message += NormalizeReason(manifest.multiclass_ui_display_reason.c_str());
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
        discovery.validated;
    manifest->full_packet_trace_allowed = false;
    manifest->receive_introspection_allowed = false;
    if (manifest->packet_hooks_allowed) {
        manifest->packet_hooks_reason =
            L"enabled by default for validated ROF2 server auth stats observation";
    } else if (!manifest->proxy_ready) {
        manifest->packet_hooks_reason = L"proxy is not ready";
    } else if (!manifest->hooks_allowed || !manifest->fingerprint_matched) {
        manifest->packet_hooks_reason = L"ROF2 fingerprint/host guard denied hook capability";
    } else if (!discovery.validated) {
        manifest->packet_hooks_reason =
            L"receive dispatcher discovery not validated";
    } else {
        manifest->packet_hooks_reason =
            L"receive dispatcher capture denied for unknown reason";
    }
    manifest->full_packet_trace_reason =
        L"full packet tracing retired; capability disabled by default";
    manifest->receive_introspection_reason =
        L"receive introspection retired; capability disabled by default";
}

void ApplySpellUsabilityDiscovery(
    Manifest* manifest,
    const monomyth::spell_usability_discovery::Result& discovery) noexcept {
    if (manifest == nullptr) {
        return;
    }

    manifest->spell_usability_discovery_allowed = discovery.allowed;
    manifest->spell_usability_trace_dev_opt_in = false;
    manifest->handle_rbutton_up_state = discovery.handle_rbutton_up.state;
    manifest->handle_rbutton_up_rva = discovery.handle_rbutton_up.candidate_rva;
    manifest->handle_rbutton_up_address = discovery.handle_rbutton_up.candidate_address;
    manifest->handle_rbutton_up_evidence_source = discovery.handle_rbutton_up.evidence_source;
    manifest->handle_rbutton_up_failure_reason = discovery.handle_rbutton_up.failure_reason;
    manifest->get_spell_level_needed_state = discovery.get_spell_level_needed.state;
    manifest->get_spell_level_needed_rva = discovery.get_spell_level_needed.candidate_rva;
    manifest->get_spell_level_needed_address = discovery.get_spell_level_needed.candidate_address;
    manifest->get_spell_level_needed_evidence_source =
        discovery.get_spell_level_needed.evidence_source;
    manifest->get_spell_level_needed_failure_reason =
        discovery.get_spell_level_needed.failure_reason;
    manifest->is_class_usable_predicate_state = discovery.is_class_usable_predicate.state;
    manifest->is_class_usable_predicate_rva =
        discovery.is_class_usable_predicate.candidate_rva;
    manifest->is_class_usable_predicate_address =
        discovery.is_class_usable_predicate.candidate_address;
    manifest->is_class_usable_predicate_evidence_source =
        discovery.is_class_usable_predicate.evidence_source;
    manifest->is_class_usable_predicate_failure_reason =
        discovery.is_class_usable_predicate.failure_reason;
    manifest->can_equip_state = discovery.can_equip.state;
    manifest->can_equip_rva = discovery.can_equip.candidate_rva;
    manifest->can_equip_address = discovery.can_equip.candidate_address;
    manifest->can_equip_evidence_source = discovery.can_equip.evidence_source;
    manifest->can_equip_failure_reason = discovery.can_equip.failure_reason;
    manifest->inv_slot_mgr_move_item_state = discovery.inv_slot_mgr_move_item.state;
    manifest->inv_slot_mgr_move_item_rva =
        discovery.inv_slot_mgr_move_item.candidate_rva;
    manifest->inv_slot_mgr_move_item_address =
        discovery.inv_slot_mgr_move_item.candidate_address;
    manifest->inv_slot_mgr_move_item_evidence_source =
        discovery.inv_slot_mgr_move_item.evidence_source;
    manifest->inv_slot_mgr_move_item_failure_reason =
        discovery.inv_slot_mgr_move_item.failure_reason;
    manifest->spellbook_dispatcher_state = discovery.spellbook_dispatcher.state;
    manifest->spellbook_dispatcher_rva = discovery.spellbook_dispatcher.candidate_rva;
    manifest->spellbook_dispatcher_address = discovery.spellbook_dispatcher.candidate_address;
    manifest->spellbook_dispatcher_evidence_source =
        discovery.spellbook_dispatcher.evidence_source;
    manifest->spellbook_dispatcher_failure_reason =
        discovery.spellbook_dispatcher.failure_reason;
    manifest->start_spell_scribe_path_state = discovery.start_spell_scribe_path.state;
    manifest->start_spell_scribe_path_rva =
        discovery.start_spell_scribe_path.candidate_rva;
    manifest->start_spell_scribe_path_address =
        discovery.start_spell_scribe_path.candidate_address;
    manifest->start_spell_scribe_path_evidence_source =
        discovery.start_spell_scribe_path.evidence_source;
    manifest->start_spell_scribe_path_failure_reason =
        discovery.start_spell_scribe_path.failure_reason;
    manifest->start_spell_scribe_precheck_mode_getter_state =
        discovery.start_spell_scribe_precheck_mode_getter.state;
    manifest->start_spell_scribe_precheck_mode_getter_rva =
        discovery.start_spell_scribe_precheck_mode_getter.candidate_rva;
    manifest->start_spell_scribe_precheck_mode_getter_address =
        discovery.start_spell_scribe_precheck_mode_getter.candidate_address;
    manifest->start_spell_scribe_precheck_mode_getter_evidence_source =
        discovery.start_spell_scribe_precheck_mode_getter.evidence_source;
    manifest->start_spell_scribe_precheck_mode_getter_failure_reason =
        discovery.start_spell_scribe_precheck_mode_getter.failure_reason;
    manifest->start_spell_scribe_precheck_gate_state =
        discovery.start_spell_scribe_precheck_gate.state;
    manifest->start_spell_scribe_precheck_gate_rva =
        discovery.start_spell_scribe_precheck_gate.candidate_rva;
    manifest->start_spell_scribe_precheck_gate_address =
        discovery.start_spell_scribe_precheck_gate.candidate_address;
    manifest->start_spell_scribe_precheck_gate_evidence_source =
        discovery.start_spell_scribe_precheck_gate.evidence_source;
    manifest->start_spell_scribe_precheck_gate_failure_reason =
        discovery.start_spell_scribe_precheck_gate.failure_reason;
    manifest->start_spell_scribe_precheck_lookup_state =
        discovery.start_spell_scribe_precheck_lookup.state;
    manifest->start_spell_scribe_precheck_lookup_rva =
        discovery.start_spell_scribe_precheck_lookup.candidate_rva;
    manifest->start_spell_scribe_precheck_lookup_address =
        discovery.start_spell_scribe_precheck_lookup.candidate_address;
    manifest->start_spell_scribe_precheck_lookup_evidence_source =
        discovery.start_spell_scribe_precheck_lookup.evidence_source;
    manifest->start_spell_scribe_precheck_lookup_failure_reason =
        discovery.start_spell_scribe_precheck_lookup.failure_reason;
    manifest->start_spell_scribe_precheck_fast_accept_state =
        discovery.start_spell_scribe_precheck_fast_accept.state;
    manifest->start_spell_scribe_precheck_fast_accept_rva =
        discovery.start_spell_scribe_precheck_fast_accept.candidate_rva;
    manifest->start_spell_scribe_precheck_fast_accept_address =
        discovery.start_spell_scribe_precheck_fast_accept.candidate_address;
    manifest->start_spell_scribe_precheck_fast_accept_evidence_source =
        discovery.start_spell_scribe_precheck_fast_accept.evidence_source;
    manifest->start_spell_scribe_precheck_fast_accept_failure_reason =
        discovery.start_spell_scribe_precheck_fast_accept.failure_reason;
    manifest->start_spell_scribe_precheck_class_resolver_state =
        discovery.start_spell_scribe_precheck_class_resolver.state;
    manifest->start_spell_scribe_precheck_class_resolver_rva =
        discovery.start_spell_scribe_precheck_class_resolver.candidate_rva;
    manifest->start_spell_scribe_precheck_class_resolver_address =
        discovery.start_spell_scribe_precheck_class_resolver.candidate_address;
    manifest->start_spell_scribe_precheck_class_resolver_evidence_source =
        discovery.start_spell_scribe_precheck_class_resolver.evidence_source;
    manifest->start_spell_scribe_precheck_class_resolver_failure_reason =
        discovery.start_spell_scribe_precheck_class_resolver.failure_reason;
    manifest->start_spell_scribe_precheck_assigned_mask_getter_state =
        discovery.start_spell_scribe_precheck_assigned_mask_getter.state;
    manifest->start_spell_scribe_precheck_assigned_mask_getter_rva =
        discovery.start_spell_scribe_precheck_assigned_mask_getter.candidate_rva;
    manifest->start_spell_scribe_precheck_assigned_mask_getter_address =
        discovery.start_spell_scribe_precheck_assigned_mask_getter.candidate_address;
    manifest->start_spell_scribe_precheck_assigned_mask_getter_evidence_source =
        discovery.start_spell_scribe_precheck_assigned_mask_getter.evidence_source;
    manifest->start_spell_scribe_precheck_assigned_mask_getter_failure_reason =
        discovery.start_spell_scribe_precheck_assigned_mask_getter.failure_reason;
    manifest->start_spell_scribe_precheck_rule_4462c0_state =
        discovery.start_spell_scribe_precheck_rule_4462c0.state;
    manifest->start_spell_scribe_precheck_rule_4462c0_rva =
        discovery.start_spell_scribe_precheck_rule_4462c0.candidate_rva;
    manifest->start_spell_scribe_precheck_rule_4462c0_address =
        discovery.start_spell_scribe_precheck_rule_4462c0.candidate_address;
    manifest->start_spell_scribe_precheck_rule_4462c0_evidence_source =
        discovery.start_spell_scribe_precheck_rule_4462c0.evidence_source;
    manifest->start_spell_scribe_precheck_rule_4462c0_failure_reason =
        discovery.start_spell_scribe_precheck_rule_4462c0.failure_reason;
    manifest->start_spell_scribe_precheck_rule_446190_state =
        discovery.start_spell_scribe_precheck_rule_446190.state;
    manifest->start_spell_scribe_precheck_rule_446190_rva =
        discovery.start_spell_scribe_precheck_rule_446190.candidate_rva;
    manifest->start_spell_scribe_precheck_rule_446190_address =
        discovery.start_spell_scribe_precheck_rule_446190.candidate_address;
    manifest->start_spell_scribe_precheck_rule_446190_evidence_source =
        discovery.start_spell_scribe_precheck_rule_446190.evidence_source;
    manifest->start_spell_scribe_precheck_rule_446190_failure_reason =
        discovery.start_spell_scribe_precheck_rule_446190.failure_reason;
    manifest->start_spell_scribe_precheck_rule_446200_state =
        discovery.start_spell_scribe_precheck_rule_446200.state;
    manifest->start_spell_scribe_precheck_rule_446200_rva =
        discovery.start_spell_scribe_precheck_rule_446200.candidate_rva;
    manifest->start_spell_scribe_precheck_rule_446200_address =
        discovery.start_spell_scribe_precheck_rule_446200.candidate_address;
    manifest->start_spell_scribe_precheck_rule_446200_evidence_source =
        discovery.start_spell_scribe_precheck_rule_446200.evidence_source;
    manifest->start_spell_scribe_precheck_rule_446200_failure_reason =
        discovery.start_spell_scribe_precheck_rule_446200.failure_reason;
    manifest->start_spell_scribe_precheck_rule_446380_state =
        discovery.start_spell_scribe_precheck_rule_446380.state;
    manifest->start_spell_scribe_precheck_rule_446380_rva =
        discovery.start_spell_scribe_precheck_rule_446380.candidate_rva;
    manifest->start_spell_scribe_precheck_rule_446380_address =
        discovery.start_spell_scribe_precheck_rule_446380.candidate_address;
    manifest->start_spell_scribe_precheck_rule_446380_evidence_source =
        discovery.start_spell_scribe_precheck_rule_446380.evidence_source;
    manifest->start_spell_scribe_precheck_rule_446380_failure_reason =
        discovery.start_spell_scribe_precheck_rule_446380.failure_reason;
    manifest->can_start_memming_state = discovery.can_start_memming.state;
    manifest->can_start_memming_rva = discovery.can_start_memming.candidate_rva;
    manifest->can_start_memming_address = discovery.can_start_memming.candidate_address;
    manifest->can_start_memming_evidence_source = discovery.can_start_memming.evidence_source;
    manifest->can_start_memming_failure_reason = discovery.can_start_memming.failure_reason;
    manifest->spellbook_memorize_send_path_state =
        discovery.spellbook_memorize_send_path.state;
    manifest->spellbook_memorize_send_path_rva =
        discovery.spellbook_memorize_send_path.candidate_rva;
    manifest->spellbook_memorize_send_path_address =
        discovery.spellbook_memorize_send_path.candidate_address;
    manifest->spellbook_memorize_send_path_evidence_source =
        discovery.spellbook_memorize_send_path.evidence_source;
    manifest->spellbook_memorize_send_path_failure_reason =
        discovery.spellbook_memorize_send_path.failure_reason;
    manifest->start_spell_memorization_path_state =
        discovery.start_spell_memorization_path.state;
    manifest->start_spell_memorization_path_rva =
        discovery.start_spell_memorization_path.candidate_rva;
    manifest->start_spell_memorization_path_address =
        discovery.start_spell_memorization_path.candidate_address;
    manifest->start_spell_memorization_path_evidence_source =
        discovery.start_spell_memorization_path.evidence_source;
    manifest->start_spell_memorization_path_failure_reason =
        discovery.start_spell_memorization_path.failure_reason;
    manifest->memorize_send_packet_wrapper_state =
        discovery.memorize_send_packet_wrapper.state;
    manifest->memorize_send_packet_wrapper_rva =
        discovery.memorize_send_packet_wrapper.candidate_rva;
    manifest->memorize_send_packet_wrapper_address =
        discovery.memorize_send_packet_wrapper.candidate_address;
    manifest->memorize_send_packet_wrapper_evidence_source =
        discovery.memorize_send_packet_wrapper.evidence_source;
    manifest->memorize_send_packet_wrapper_failure_reason =
        discovery.memorize_send_packet_wrapper.failure_reason;
    manifest->scroll_scribe_trace_dev_opt_in = false;
    manifest->memorize_send_trace_dev_opt_in = false;
    manifest->multiclass_spell_usability_dev_opt_in = false;
    manifest->full_packet_trace_dev_opt_in = false;

    manifest->spell_usability_trace_allowed = false;
    manifest->scroll_scribe_trace_allowed = false;
    manifest->memorize_send_trace_allowed = false;
    manifest->full_packet_trace_allowed = false;
    manifest->multiclass_spell_usability_allowed =
        discovery.allowed &&
        discovery.get_spell_level_needed.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.get_spell_level_needed.trace_safe;
    manifest->multiclass_item_usability_allowed =
        discovery.allowed &&
        discovery.is_class_usable_predicate.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.is_class_usable_predicate.trace_safe &&
        discovery.can_equip.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.can_equip.trace_safe;

    if (!discovery.allowed) {
        manifest->spell_usability_discovery_reason =
            L"capability guard denied spell usability discovery";
    } else {
        manifest->spell_usability_discovery_reason =
            NormalizeReason(discovery.reason.c_str());
    }

    manifest->spell_usability_trace_reason =
        L"spell usability tracing retired; re-add explicitly if needed";
    manifest->scroll_scribe_trace_reason =
        L"scroll scribe tracing retired; re-add explicitly if needed";
    manifest->memorize_send_trace_reason =
        L"memorize send tracing retired; re-add explicitly if needed";
    manifest->full_packet_trace_reason =
        L"full packet tracing retired; capability disabled by default";

    if (manifest->multiclass_spell_usability_allowed) {
        manifest->multiclass_spell_usability_reason =
            L"enabled by default for validated ROF2 multiclass spell usability";
    } else if (!discovery.allowed) {
        manifest->multiclass_spell_usability_reason =
            L"multiclass spell usability requires the ROF2 discovery capability gate";
    } else if (
        discovery.get_spell_level_needed.state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        !discovery.get_spell_level_needed.trace_safe) {
        std::wstring reason =
            L"multiclass spell usability denied because GetSpellLevelNeeded is not validated trace-safe";
        reason += L" failure_reason=";
        reason += NormalizeReason(discovery.get_spell_level_needed.failure_reason.c_str());
        manifest->multiclass_spell_usability_reason = reason;
    } else {
        manifest->multiclass_spell_usability_reason =
            L"multiclass spell usability gate denied for unknown reason";
    }

    if (manifest->multiclass_item_usability_allowed) {
        manifest->multiclass_item_usability_reason =
            L"enabled by default for validated ROF2 multiclass item usability";
    } else if (!discovery.allowed) {
        manifest->multiclass_item_usability_reason =
            L"multiclass item usability requires the ROF2 discovery capability gate";
    } else if (
        discovery.is_class_usable_predicate.state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        !discovery.is_class_usable_predicate.trace_safe ||
        discovery.can_equip.state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        !discovery.can_equip.trace_safe) {
        std::wstring reason =
            L"multiclass item usability denied because equip gates are not fully validated trace-safe";
        reason += L" predicate_failure_reason=";
        reason += NormalizeReason(discovery.is_class_usable_predicate.failure_reason.c_str());
        reason += L" can_equip_failure_reason=";
        reason += NormalizeReason(discovery.can_equip.failure_reason.c_str());
        manifest->multiclass_item_usability_reason = reason;
    } else {
        manifest->multiclass_item_usability_reason =
            L"multiclass item usability gate denied for unknown reason";
    }
}

void ApplyClassDisplayDiscovery(
    Manifest* manifest,
    const monomyth::class_display_discovery::Result& discovery) noexcept {
    if (manifest == nullptr) {
        return;
    }

    manifest->class_display_discovery_allowed = discovery.allowed;
    manifest->who_class_name_state = discovery.who_class_name.state;
    manifest->who_class_name_rva = discovery.who_class_name.candidate_rva;
    manifest->who_class_name_address = discovery.who_class_name.candidate_address;
    manifest->who_class_name_evidence_source = discovery.who_class_name.evidence_source;
    manifest->who_class_name_failure_reason = discovery.who_class_name.failure_reason;
    manifest->get_class_desc_state = discovery.get_class_desc.state;
    manifest->get_class_desc_rva = discovery.get_class_desc.candidate_rva;
    manifest->get_class_desc_address = discovery.get_class_desc.candidate_address;
    manifest->get_class_desc_evidence_source = discovery.get_class_desc.evidence_source;
    manifest->get_class_desc_failure_reason = discovery.get_class_desc.failure_reason;
    manifest->get_class_three_letter_code_state = discovery.get_class_three_letter_code.state;
    manifest->get_class_three_letter_code_rva =
        discovery.get_class_three_letter_code.candidate_rva;
    manifest->get_class_three_letter_code_address =
        discovery.get_class_three_letter_code.candidate_address;
    manifest->get_class_three_letter_code_evidence_source =
        discovery.get_class_three_letter_code.evidence_source;
    manifest->get_class_three_letter_code_failure_reason =
        discovery.get_class_three_letter_code.failure_reason;
    manifest->char_select_class_name_func_state =
        discovery.char_select_class_name_func.state;
    manifest->char_select_class_name_func_rva =
        discovery.char_select_class_name_func.candidate_rva;
    manifest->char_select_class_name_func_address =
        discovery.char_select_class_name_func.candidate_address;
    manifest->char_select_class_name_func_evidence_source =
        discovery.char_select_class_name_func.evidence_source;
    manifest->char_select_class_name_func_failure_reason =
        discovery.char_select_class_name_func.failure_reason;

    manifest->multiclass_ui_display_allowed =
        discovery.allowed &&
        discovery.who_class_name.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.who_class_name.hook_safe &&
        discovery.get_class_desc.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.get_class_desc.hook_safe &&
        discovery.get_class_three_letter_code.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        discovery.get_class_three_letter_code.hook_safe;
    manifest->ui_hooks_allowed = manifest->multiclass_ui_display_allowed;

    if (!discovery.allowed) {
        manifest->class_display_discovery_reason =
            L"capability guard denied class display discovery";
    } else {
        manifest->class_display_discovery_reason = NormalizeReason(discovery.reason.c_str());
    }

    if (manifest->multiclass_ui_display_allowed) {
        manifest->multiclass_ui_display_reason =
            L"enabled by default for validated ROF2 local/self multiclass UI display";
        return;
    }

    if (!discovery.allowed) {
        manifest->multiclass_ui_display_reason =
            L"multiclass UI display requires the ROF2 class display discovery capability gate";
        return;
    }

    const std::wstring who_failure = DescribeTargetFailure(
        L"LeftClickedOnPlayerSurrogate",
        discovery.who_class_name.state,
        discovery.who_class_name.hook_safe,
        discovery.who_class_name.failure_reason);
    const std::wstring desc_failure = DescribeTargetFailure(
        L"GetClassDesc",
        discovery.get_class_desc.state,
        discovery.get_class_desc.hook_safe,
        discovery.get_class_desc.failure_reason);
    const std::wstring code_failure = DescribeTargetFailure(
        L"GetClassThreeLetterCode",
        discovery.get_class_three_letter_code.state,
        discovery.get_class_three_letter_code.hook_safe,
        discovery.get_class_three_letter_code.failure_reason);
    std::wstring reason =
        L"multiclass UI display denied because one or more formatter hooks are not fully validated hook-safe";
    if (!who_failure.empty()) {
        reason += L" ";
        reason += who_failure;
    }
    if (!desc_failure.empty()) {
        reason += L" ";
        reason += desc_failure;
    }
    if (!code_failure.empty()) {
        reason += L" ";
        reason += code_failure;
    }
    manifest->multiclass_ui_display_reason = reason;
}

}  // namespace monomyth::runtime
