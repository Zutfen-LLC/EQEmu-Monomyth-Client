#pragma once

#include "spell_usability_discovery_types.h"

namespace monomyth::spell_usability_discovery {

struct DecisionInput {
    bool discovery_allowed = false;
    bool fingerprint_matched = false;
    bool runtime_export_found = false;
    bool cleanroom_rva_found = false;
    bool wrapper_candidate_found = false;
    bool candidate_executable = false;
    bool plausible_prologue = false;
    bool wrapper_validation_required = false;
    bool wrapper_validation_passed = false;
    bool diagnostic_string_required = false;
    bool diagnostic_string_found = false;
    bool diagnostic_string_xref = false;
};

struct DecisionResult {
    TargetState state = TargetState::kNotAttempted;
    EvidenceSource evidence_source = EvidenceSource::kNotAttempted;
    const wchar_t* validation = L"not_attempted";
    const wchar_t* failure_reason = L"not_attempted";
    bool exact_signature_validated = false;
    bool trace_safe = false;
};

DecisionResult EvaluateDecision(const DecisionInput& input) noexcept;

}  // namespace monomyth::spell_usability_discovery
