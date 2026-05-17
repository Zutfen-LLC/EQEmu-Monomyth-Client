#include "spell_usability_discovery_policy.h"

#include <iostream>
#include <string_view>

namespace {

bool Expect(bool condition, std::string_view name) {
    if (condition) {
        return true;
    }

    std::cerr << "failed: " << name << "\n";
    return false;
}

}  // namespace

int main() {
    bool passed = true;

    const auto wrapper_success = monomyth::spell_usability_discovery::EvaluateDecision({
        true,
        true,
        false,
        false,
        false,
        true,
        true,
        true,
        true,
        true,
        false,
        false,
        false,
    });
    passed &= Expect(
        wrapper_success.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated,
        "wrapper evidence validates when runtime export is absent");
    passed &= Expect(
        wrapper_success.evidence_source ==
            monomyth::spell_usability_discovery::EvidenceSource::kWrapperValidation,
        "wrapper evidence source selected");

    const auto missing_cleanroom = monomyth::spell_usability_discovery::EvaluateDecision({
        true,
        true,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
    });
    passed &= Expect(
        missing_cleanroom.state ==
            monomyth::spell_usability_discovery::TargetState::kFailed,
        "missing cleanroom target fails closed");
    passed &= Expect(
        std::wstring_view(missing_cleanroom.failure_reason) == L"missing_cleanroom_target",
        "missing cleanroom target reason");

    const auto fingerprint_mismatch = monomyth::spell_usability_discovery::EvaluateDecision({
        false,
        false,
        true,
        false,
        true,
        false,
        true,
        true,
        false,
        false,
        false,
        false,
        false,
    });
    passed &= Expect(
        fingerprint_mismatch.state ==
            monomyth::spell_usability_discovery::TargetState::kFailed,
        "fingerprint mismatch denies discovery");
    passed &= Expect(
        std::wstring_view(fingerprint_mismatch.failure_reason) == L"fingerprint_mismatch",
        "fingerprint mismatch reason");

    const auto spell_string_missing = monomyth::spell_usability_discovery::EvaluateDecision({
        true,
        true,
        true,
        false,
        false,
        false,
        true,
        true,
        false,
        false,
        true,
        false,
        false,
    });
    passed &= Expect(
        spell_string_missing.state ==
            monomyth::spell_usability_discovery::TargetState::kFoundUnvalidated,
        "diagnostic string absence keeps spell target fail-closed");
    passed &= Expect(
        std::wstring_view(spell_string_missing.failure_reason) == L"diagnostic_string_missing",
        "diagnostic string missing reason");

    const auto sibling_success = monomyth::spell_usability_discovery::EvaluateDecision({
        true,
        true,
        false,
        false,
        false,
        true,
        true,
        true,
        true,
        true,
        false,
        false,
        false,
    });
    const auto sibling_failure = monomyth::spell_usability_discovery::EvaluateDecision({
        true,
        true,
        false,
        false,
        false,
        true,
        true,
        true,
        true,
        false,
        false,
        false,
        false,
    });
    passed &= Expect(
        sibling_success.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated,
        "one target can still validate");
    passed &= Expect(
        sibling_failure.state !=
            monomyth::spell_usability_discovery::TargetState::kValidated,
        "another target can still fail independently");

    const auto fingerprint_success = monomyth::spell_usability_discovery::EvaluateDecision({
        true,
        true,
        true,
        true,
        false,
        false,
        true,
        true,
        false,
        false,
        true,
        true,
        true,
    });
    passed &= Expect(
        fingerprint_success.state ==
            monomyth::spell_usability_discovery::TargetState::kValidated,
        "fingerprint locator validates without runtime export");
    passed &= Expect(
        fingerprint_success.evidence_source ==
            monomyth::spell_usability_discovery::EvidenceSource::kFingerprintRva,
        "fingerprint locator preferred over runtime export");

    return passed ? 0 : 1;
}
