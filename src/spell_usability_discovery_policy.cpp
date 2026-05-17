#include "spell_usability_discovery_policy.h"

namespace monomyth::spell_usability_discovery {
namespace {

EvidenceSource SelectEvidenceSource(const DecisionInput& input) noexcept {
    if (input.runtime_export_found) {
        return EvidenceSource::kRuntimeExport;
    }
    if (input.cleanroom_rva_found) {
        return EvidenceSource::kCleanroomRva;
    }
    if (input.wrapper_candidate_found) {
        return EvidenceSource::kWrapperValidation;
    }
    return EvidenceSource::kUnavailable;
}

}  // namespace

const wchar_t* TargetStateName(TargetState state) noexcept {
    switch (state) {
        case TargetState::kNotAttempted:
            return L"not_attempted";
        case TargetState::kFoundUnvalidated:
            return L"found_unvalidated";
        case TargetState::kValidated:
            return L"validated";
        case TargetState::kFailed:
            return L"failed";
    }

    return L"unknown";
}

const wchar_t* EvidenceSourceName(EvidenceSource source) noexcept {
    switch (source) {
        case EvidenceSource::kNotAttempted:
            return L"not_attempted";
        case EvidenceSource::kRuntimeExport:
            return L"runtime_export";
        case EvidenceSource::kCleanroomRva:
            return L"cleanroom_rva";
        case EvidenceSource::kWrapperValidation:
            return L"wrapper_validation";
        case EvidenceSource::kBytePattern:
            return L"byte_pattern";
        case EvidenceSource::kUnavailable:
            return L"unavailable";
    }

    return L"unknown";
}

DecisionResult EvaluateDecision(const DecisionInput& input) noexcept {
    DecisionResult result = {};

    if (!input.discovery_allowed) {
        result.state = TargetState::kFailed;
        result.validation = L"failed";
        result.failure_reason =
            input.fingerprint_matched ? L"capability_denied" : L"fingerprint_mismatch";
        return result;
    }

    result.evidence_source = SelectEvidenceSource(input);
    if (result.evidence_source == EvidenceSource::kUnavailable) {
        result.state = TargetState::kFailed;
        result.validation = L"failed";
        result.failure_reason = L"missing_cleanroom_target";
        return result;
    }

    if (!input.candidate_executable) {
        result.state = TargetState::kFailed;
        result.validation = L"failed";
        result.failure_reason = L"non_executable_target";
        return result;
    }

    if (!input.plausible_prologue) {
        result.state = TargetState::kFoundUnvalidated;
        result.validation = L"failed";
        result.failure_reason = L"unsupported_prologue";
        return result;
    }

    if (input.wrapper_validation_required && !input.wrapper_validation_passed) {
        result.state = TargetState::kFoundUnvalidated;
        result.validation = L"failed";
        result.failure_reason = L"wrapper_validation_failed";
        return result;
    }

    if (input.diagnostic_string_required && !input.diagnostic_string_found) {
        result.state = TargetState::kFoundUnvalidated;
        result.validation = L"failed";
        result.failure_reason = L"diagnostic_string_missing";
        return result;
    }

    if (input.diagnostic_string_required && !input.diagnostic_string_xref) {
        result.state = TargetState::kFoundUnvalidated;
        result.validation = L"failed";
        result.failure_reason = L"diagnostic_string_xref_missing";
        return result;
    }

    result.state = TargetState::kValidated;
    result.validation = L"passed";
    result.failure_reason = L"none";
    result.exact_signature_validated =
        result.evidence_source == EvidenceSource::kRuntimeExport;
    result.trace_safe = true;
    return result;
}

}  // namespace monomyth::spell_usability_discovery
