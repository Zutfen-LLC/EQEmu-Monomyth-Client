#pragma once

#include <cstdint>
#include <string>

#include "spell_usability_discovery_types.h"

namespace monomyth::spell_usability_discovery {

struct TargetResult {
    const wchar_t* target = L"unknown";
    TargetState state = TargetState::kNotAttempted;
    std::uintptr_t module_base = 0;
    std::uint32_t candidate_rva = 0;
    std::uintptr_t candidate_address = 0;
    bool exact_signature_validated = false;
    bool trace_safe = false;
    std::wstring evidence_source = L"not_attempted";
    std::wstring discovery_method = L"not_attempted";
    std::wstring validation = L"not_attempted";
    std::wstring failure_reason = L"not_attempted";
    std::wstring resolved_symbol = L"";
    std::wstring validation_evidence = L"not attempted";
    std::wstring reason = L"not attempted";
};

struct Result {
    bool allowed = false;
    bool trace_dev_opt_in = false;
    std::wstring reason = L"not run";
    TargetResult handle_rbutton_up = {L"CInvSlot::HandleRButtonUp"};
    TargetResult get_spell_level_needed = {L"GetSpellLevelNeeded"};
    TargetResult is_class_usable_predicate = {L"EQ_Character::IsClassUsablePredicate"};
    TargetResult spellbook_dispatcher = {L"SpellbookDispatcher"};
    TargetResult start_spell_scribe_path = {L"StartSpellScribePath"};
    TargetResult start_spell_scribe_precheck_mode_getter = {
        L"StartSpellScribePrecheckModeGetter"};
    TargetResult start_spell_scribe_precheck_gate = {
        L"StartSpellScribePrecheckGate"};
    TargetResult start_spell_scribe_precheck_lookup = {
        L"StartSpellScribePrecheckLookup"};
    TargetResult start_spell_scribe_precheck_fast_accept = {
        L"StartSpellScribePrecheckFastAccept"};
    TargetResult start_spell_scribe_precheck_class_resolver = {
        L"StartSpellScribePrecheckClassResolver"};
    TargetResult start_spell_scribe_precheck_assigned_mask_getter = {
        L"StartSpellScribePrecheckAssignedMaskGetter"};
    TargetResult start_spell_scribe_precheck_rule_4462c0 = {
        L"StartSpellScribePrecheckRule4462c0"};
    TargetResult start_spell_scribe_precheck_rule_446190 = {
        L"StartSpellScribePrecheckRule446190"};
    TargetResult start_spell_scribe_precheck_rule_446200 = {
        L"StartSpellScribePrecheckRule446200"};
    TargetResult start_spell_scribe_precheck_rule_446380 = {
        L"StartSpellScribePrecheckRule446380"};
    TargetResult can_start_memming = {L"CanStartMemming"};
    TargetResult spellbook_memorize_send_path = {L"SpellbookMemorizeSendPath"};
    TargetResult start_spell_memorization_path = {L"StartSpellMemorizationPath"};
    TargetResult memorize_send_packet_wrapper = {L"MemorizeSendPacketWrapper"};
};

void Initialize() noexcept;
Result Run(bool discovery_allowed, bool fingerprint_matched) noexcept;
void Shutdown() noexcept;
Result GetResult() noexcept;
void LogResult(const Result& result) noexcept;

}  // namespace monomyth::spell_usability_discovery
