#pragma once

#include <cstdint>
#include <string>

namespace monomyth::spell_usability_discovery {

enum class TargetState {
    kNotAttempted,
    kFoundUnvalidated,
    kValidated,
    kFailed,
};

struct TargetResult {
    const wchar_t* target = L"unknown";
    TargetState state = TargetState::kNotAttempted;
    std::uintptr_t module_base = 0;
    std::uint32_t candidate_rva = 0;
    std::uintptr_t candidate_address = 0;
    bool exact_signature_validated = false;
    bool trace_safe = false;
    std::wstring discovery_method = L"not_attempted";
    std::wstring resolved_symbol = L"";
    std::wstring validation_evidence = L"not attempted";
    std::wstring reason = L"not attempted";
};

struct Result {
    bool allowed = false;
    bool trace_dev_opt_in = false;
    std::wstring reason = L"not run";
    TargetResult get_spell_level_needed = {L"GetSpellLevelNeeded"};
    TargetResult can_start_memming = {L"CanStartMemming"};
};

void Initialize() noexcept;
Result Run(bool discovery_allowed) noexcept;
void Shutdown() noexcept;
Result GetResult() noexcept;

const wchar_t* TargetStateName(TargetState state) noexcept;
void LogResult(const Result& result) noexcept;

}  // namespace monomyth::spell_usability_discovery
