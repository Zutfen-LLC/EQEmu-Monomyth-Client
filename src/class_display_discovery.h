#pragma once

#include <cstdint>
#include <string>

#include "spell_usability_discovery_types.h"

namespace monomyth::class_display_discovery {

struct TargetResult {
    const wchar_t* target = L"unknown";
    monomyth::spell_usability_discovery::TargetState state =
        monomyth::spell_usability_discovery::TargetState::kNotAttempted;
    std::uintptr_t module_base = 0;
    std::uint32_t candidate_rva = 0;
    std::uintptr_t candidate_address = 0;
    bool exact_signature_validated = false;
    bool hook_safe = false;
    std::wstring evidence_source = L"not_attempted";
    std::wstring discovery_method = L"not_attempted";
    std::wstring validation = L"not_attempted";
    std::wstring failure_reason = L"not_attempted";
    std::wstring validation_evidence = L"not attempted";
    std::wstring reason = L"not attempted";
};

struct Result {
    bool allowed = false;
    std::wstring reason = L"not run";
    TargetResult who_class_name = {L"WhoClassName"};
    TargetResult get_class_desc = {L"GetClassDesc"};
    TargetResult get_class_three_letter_code = {L"GetClassThreeLetterCode"};
    TargetResult char_select_class_name_func = {L"ProgressionSelectionClassValueWriter"};
};

void Initialize() noexcept;
Result Run(bool discovery_allowed, bool fingerprint_matched) noexcept;
void Shutdown() noexcept;
Result GetResult() noexcept;
void LogResult(const Result& result) noexcept;

}  // namespace monomyth::class_display_discovery
