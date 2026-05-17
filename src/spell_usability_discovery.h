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
    TargetResult get_usable_classes = {L"GetUsableClasses"};
    TargetResult can_equip = {L"CanEquip"};
    TargetResult can_start_memming = {L"CanStartMemming"};
};

void Initialize() noexcept;
Result Run(bool discovery_allowed, bool fingerprint_matched) noexcept;
void Shutdown() noexcept;
Result GetResult() noexcept;
void LogResult(const Result& result) noexcept;

}  // namespace monomyth::spell_usability_discovery
