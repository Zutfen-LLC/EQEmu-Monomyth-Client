#pragma once

#include <cstdint>
#include <string>

#include "class_display_discovery.h"
#include "fingerprint.h"
#include "receive_dispatch_discovery.h"
#include "spell_usability_discovery.h"

namespace monomyth::runtime {

struct Manifest {
    bool proxy_loaded = false;
    bool proxy_ready = false;
    bool host_process_supported = false;
    bool fingerprint_checked = false;
    bool fingerprint_matched = false;
    monomyth::fingerprint::Method fingerprint_method = monomyth::fingerprint::Method::kUnavailable;
    bool hooks_allowed = false;
    bool packet_hooks_dev_opt_in = false;
    bool packet_hooks_allowed = false;
    bool full_packet_trace_dev_opt_in = false;
    bool full_packet_trace_allowed = false;
    bool receive_introspection_dev_opt_in = false;
    bool receive_introspection_allowed = false;
    bool spell_usability_discovery_dev_opt_in = false;
    bool spell_usability_discovery_allowed = false;
    bool class_display_discovery_allowed = false;
    bool spell_usability_trace_dev_opt_in = false;
    bool spell_usability_trace_allowed = false;
    bool scroll_scribe_trace_dev_opt_in = false;
    bool scroll_scribe_trace_allowed = false;
    bool memorize_send_trace_dev_opt_in = false;
    bool memorize_send_trace_allowed = false;
    bool multiclass_spell_usability_dev_opt_in = false;
    bool multiclass_spell_usability_allowed = false;
    bool multiclass_item_usability_allowed = false;
    bool multiclass_ui_display_allowed = false;
    bool ui_hooks_allowed = false;
    bool heartbeat_allowed = false;
    monomyth::receive_dispatch_discovery::State receive_dispatch_discovery_state =
        monomyth::receive_dispatch_discovery::State::kUnavailable;
    bool receive_dispatch_validated = false;
    monomyth::spell_usability_discovery::TargetState get_spell_level_needed_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState handle_rbutton_up_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState is_class_usable_predicate_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState can_equip_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState inv_slot_mgr_move_item_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState spellbook_dispatcher_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState start_spell_scribe_path_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_mode_getter_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState start_spell_scribe_precheck_gate_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState start_spell_scribe_precheck_lookup_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_fast_accept_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_class_resolver_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_assigned_mask_getter_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_rule_4462c0_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_rule_446190_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_rule_446200_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState
        start_spell_scribe_precheck_rule_446380_state =
            monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState can_start_memming_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState spellbook_memorize_send_path_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState start_spell_memorization_path_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState memorize_send_packet_wrapper_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState who_class_name_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState get_class_desc_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState get_class_three_letter_code_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    monomyth::spell_usability_discovery::TargetState char_select_class_name_func_state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    std::uintptr_t runtime_module_base = 0;
    std::uint32_t receive_dispatch_rva = 0;
    std::uintptr_t receive_dispatch_address = 0;
    std::uint32_t handle_rbutton_up_rva = 0;
    std::uintptr_t handle_rbutton_up_address = 0;
    std::uint32_t get_spell_level_needed_rva = 0;
    std::uintptr_t get_spell_level_needed_address = 0;
    std::uint32_t is_class_usable_predicate_rva = 0;
    std::uintptr_t is_class_usable_predicate_address = 0;
    std::uint32_t can_equip_rva = 0;
    std::uintptr_t can_equip_address = 0;
    std::uint32_t inv_slot_mgr_move_item_rva = 0;
    std::uintptr_t inv_slot_mgr_move_item_address = 0;
    std::uint32_t spellbook_dispatcher_rva = 0;
    std::uintptr_t spellbook_dispatcher_address = 0;
    std::uint32_t start_spell_scribe_path_rva = 0;
    std::uintptr_t start_spell_scribe_path_address = 0;
    std::uint32_t start_spell_scribe_precheck_mode_getter_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_mode_getter_address = 0;
    std::uint32_t start_spell_scribe_precheck_gate_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_gate_address = 0;
    std::uint32_t start_spell_scribe_precheck_lookup_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_lookup_address = 0;
    std::uint32_t start_spell_scribe_precheck_fast_accept_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_fast_accept_address = 0;
    std::uint32_t start_spell_scribe_precheck_class_resolver_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_class_resolver_address = 0;
    std::uint32_t start_spell_scribe_precheck_assigned_mask_getter_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_assigned_mask_getter_address = 0;
    std::uint32_t start_spell_scribe_precheck_rule_4462c0_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_rule_4462c0_address = 0;
    std::uint32_t start_spell_scribe_precheck_rule_446190_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_rule_446190_address = 0;
    std::uint32_t start_spell_scribe_precheck_rule_446200_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_rule_446200_address = 0;
    std::uint32_t start_spell_scribe_precheck_rule_446380_rva = 0;
    std::uintptr_t start_spell_scribe_precheck_rule_446380_address = 0;
    std::uint32_t can_start_memming_rva = 0;
    std::uintptr_t can_start_memming_address = 0;
    std::uint32_t spellbook_memorize_send_path_rva = 0;
    std::uintptr_t spellbook_memorize_send_path_address = 0;
    std::uint32_t start_spell_memorization_path_rva = 0;
    std::uintptr_t start_spell_memorization_path_address = 0;
    std::uint32_t memorize_send_packet_wrapper_rva = 0;
    std::uintptr_t memorize_send_packet_wrapper_address = 0;
    std::uint32_t who_class_name_rva = 0;
    std::uintptr_t who_class_name_address = 0;
    std::uint32_t get_class_desc_rva = 0;
    std::uintptr_t get_class_desc_address = 0;
    std::uint32_t get_class_three_letter_code_rva = 0;
    std::uintptr_t get_class_three_letter_code_address = 0;
    std::uint32_t char_select_class_name_func_rva = 0;
    std::uintptr_t char_select_class_name_func_address = 0;
    std::wstring handle_rbutton_up_evidence_source = L"not_attempted";
    std::wstring handle_rbutton_up_failure_reason = L"not_attempted";
    std::wstring get_spell_level_needed_evidence_source = L"not_attempted";
    std::wstring get_spell_level_needed_failure_reason = L"not_attempted";
    std::wstring is_class_usable_predicate_evidence_source = L"not_attempted";
    std::wstring is_class_usable_predicate_failure_reason = L"not_attempted";
    std::wstring can_equip_evidence_source = L"not_attempted";
    std::wstring can_equip_failure_reason = L"not_attempted";
    std::wstring inv_slot_mgr_move_item_evidence_source = L"not_attempted";
    std::wstring inv_slot_mgr_move_item_failure_reason = L"not_attempted";
    std::wstring spellbook_dispatcher_evidence_source = L"not_attempted";
    std::wstring spellbook_dispatcher_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_path_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_path_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_mode_getter_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_mode_getter_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_gate_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_gate_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_lookup_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_lookup_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_fast_accept_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_fast_accept_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_class_resolver_evidence_source =
        L"not_attempted";
    std::wstring start_spell_scribe_precheck_class_resolver_failure_reason =
        L"not_attempted";
    std::wstring start_spell_scribe_precheck_assigned_mask_getter_evidence_source =
        L"not_attempted";
    std::wstring start_spell_scribe_precheck_assigned_mask_getter_failure_reason =
        L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_4462c0_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_4462c0_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_446190_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_446190_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_446200_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_446200_failure_reason = L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_446380_evidence_source = L"not_attempted";
    std::wstring start_spell_scribe_precheck_rule_446380_failure_reason = L"not_attempted";
    std::wstring can_start_memming_evidence_source = L"not_attempted";
    std::wstring can_start_memming_failure_reason = L"not_attempted";
    std::wstring spellbook_memorize_send_path_evidence_source = L"not_attempted";
    std::wstring spellbook_memorize_send_path_failure_reason = L"not_attempted";
    std::wstring start_spell_memorization_path_evidence_source = L"not_attempted";
    std::wstring start_spell_memorization_path_failure_reason = L"not_attempted";
    std::wstring memorize_send_packet_wrapper_evidence_source = L"not_attempted";
    std::wstring memorize_send_packet_wrapper_failure_reason = L"not_attempted";
    std::wstring who_class_name_evidence_source = L"not_attempted";
    std::wstring who_class_name_failure_reason = L"not_attempted";
    std::wstring get_class_desc_evidence_source = L"not_attempted";
    std::wstring get_class_desc_failure_reason = L"not_attempted";
    std::wstring get_class_three_letter_code_evidence_source = L"not_attempted";
    std::wstring get_class_three_letter_code_failure_reason = L"not_attempted";
    std::wstring char_select_class_name_func_evidence_source = L"not_attempted";
    std::wstring char_select_class_name_func_failure_reason = L"not_attempted";
    std::wstring reason = L"manifest unavailable";
    std::wstring packet_hooks_reason = L"packet hooks unavailable";
    std::wstring full_packet_trace_reason = L"full packet trace unavailable";
    std::wstring receive_introspection_reason = L"receive introspection unavailable";
    std::wstring spell_usability_discovery_reason = L"spell usability discovery unavailable";
    std::wstring class_display_discovery_reason = L"class display discovery unavailable";
    std::wstring spell_usability_trace_reason = L"spell usability trace unavailable";
    std::wstring scroll_scribe_trace_reason = L"scroll scribe trace unavailable";
    std::wstring memorize_send_trace_reason = L"memorize send trace unavailable";
    std::wstring multiclass_spell_usability_reason =
        L"multiclass spell usability unavailable";
    std::wstring multiclass_item_usability_reason =
        L"multiclass item usability unavailable";
    std::wstring multiclass_ui_display_reason =
        L"multiclass UI display unavailable";
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

void ApplySpellUsabilityDiscovery(
    Manifest* manifest,
    const monomyth::spell_usability_discovery::Result& discovery) noexcept;

void ApplyClassDisplayDiscovery(
    Manifest* manifest,
    const monomyth::class_display_discovery::Result& discovery) noexcept;

}  // namespace monomyth::runtime
