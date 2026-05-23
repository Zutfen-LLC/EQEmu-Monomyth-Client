#include "runtime_capabilities.h"

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

monomyth::class_display_discovery::TargetResult ValidatedTarget(const wchar_t* name) {
    monomyth::class_display_discovery::TargetResult target = {};
    target.target = name;
    target.state = monomyth::spell_usability_discovery::TargetState::kValidated;
    target.hook_safe = true;
    target.exact_signature_validated = true;
    target.candidate_rva = 0x1234;
    target.candidate_address = 0x401234;
    target.evidence_source = L"test";
    target.failure_reason = L"none";
    return target;
}

}  // namespace

namespace monomyth::logger {

void Log(std::wstring_view) noexcept {}
void Log(const wchar_t*) noexcept {}
void Flush() noexcept {}

}  // namespace monomyth::logger

namespace monomyth::fingerprint {

const wchar_t* MethodName(Method) noexcept {
    return L"test";
}

}  // namespace monomyth::fingerprint

namespace monomyth::receive_dispatch_discovery {

const wchar_t* StateName(State) noexcept {
    return L"test";
}

}  // namespace monomyth::receive_dispatch_discovery

int main() {
    bool passed = true;

    monomyth::fingerprint::Result fingerprint = {};
    fingerprint.process_name_match = true;
    fingerprint.matched = true;
    fingerprint.hooks_allowed = true;
    fingerprint.reason = L"matched";

    const auto manifest =
        monomyth::runtime::BuildCapabilityManifest(true, true, true, fingerprint);
    passed &= Expect(
        manifest.class_display_discovery_allowed,
        "build manifest enables class display discovery on validated ROF2");
    passed &= Expect(
        !manifest.multiclass_ui_display_allowed,
        "build manifest keeps multiclass ui display disabled by default");
    passed &= Expect(
        manifest.multiclass_skill_visibility_allowed,
        "build manifest enables activated skill visibility on validated ROF2");

    monomyth::fingerprint::Result denied_fingerprint = fingerprint;
    denied_fingerprint.hooks_allowed = false;
    const auto denied_manifest =
        monomyth::runtime::BuildCapabilityManifest(true, true, true, denied_fingerprint);
    passed &= Expect(
        !denied_manifest.multiclass_skill_visibility_allowed,
        "activated skill visibility requires hook allowance");

    monomyth::runtime::Manifest gated_manifest = {};
    monomyth::class_display_discovery::Result gated_discovery = {};
    gated_discovery.allowed = false;
    gated_discovery.reason = L"guard denied";
    monomyth::runtime::ApplyClassDisplayDiscovery(&gated_manifest, gated_discovery);
    passed &= Expect(
        !gated_manifest.multiclass_ui_display_allowed,
        "class display stays disabled when discovery is denied");
    passed &= Expect(
        !gated_manifest.ui_hooks_allowed,
        "ui hooks stay disabled when discovery is denied");
    passed &= Expect(
        gated_manifest.class_display_discovery_reason ==
            L"capability guard denied class display discovery",
        "denied class display discovery reason");

    monomyth::runtime::Manifest validated_manifest = {};
    monomyth::class_display_discovery::Result validated_discovery = {};
    validated_discovery.allowed = true;
    validated_discovery.reason = L"validated";
    validated_discovery.who_class_name =
        ValidatedTarget(L"LeftClickedOnPlayerSurrogate");
    validated_discovery.get_class_desc = ValidatedTarget(L"GetClassDesc");
    validated_discovery.get_class_three_letter_code =
        ValidatedTarget(L"GetClassThreeLetterCode");
    validated_discovery.char_select_class_name_func =
        ValidatedTarget(L"ProgressionSelectionClassValueWriter");
    monomyth::runtime::ApplyClassDisplayDiscovery(
        &validated_manifest,
        validated_discovery);
    passed &= Expect(
        validated_manifest.multiclass_ui_display_allowed,
        "class display enables only after all formatter hooks validate");
    passed &= Expect(
        validated_manifest.ui_hooks_allowed,
        "ui hooks follow class display enablement");
    passed &= Expect(
        validated_manifest.multiclass_ui_display_reason ==
            L"enabled by default for validated ROF2 local/self multiclass UI display",
        "validated class display reason");

    monomyth::runtime::Manifest producer_only_manifest = {};
    monomyth::class_display_discovery::Result producer_only_discovery =
        validated_discovery;
    producer_only_discovery.char_select_class_name_func.state =
        monomyth::spell_usability_discovery::TargetState::kFailed;
    producer_only_discovery.char_select_class_name_func.hook_safe = false;
    producer_only_discovery.char_select_class_name_func.failure_reason =
        L"cleanroom_locator_unpinned";
    monomyth::runtime::ApplyClassDisplayDiscovery(
        &producer_only_manifest,
        producer_only_discovery);
    passed &= Expect(
        producer_only_manifest.multiclass_ui_display_allowed,
        "core ui display stays enabled when only the progression selection surrogate is missing");

    monomyth::runtime::Manifest partial_manifest = {};
    monomyth::class_display_discovery::Result partial_discovery = validated_discovery;
    partial_discovery.get_class_desc.hook_safe = false;
    partial_discovery.get_class_desc.failure_reason = L"hook_unsafe";
    monomyth::runtime::ApplyClassDisplayDiscovery(&partial_manifest, partial_discovery);
    passed &= Expect(
        !partial_manifest.multiclass_ui_display_allowed,
        "hook-safe validation required for ui enablement");
    passed &= Expect(
        partial_manifest.multiclass_ui_display_reason.find(L"GetClassDesc") !=
            std::wstring::npos,
        "partial failure reason identifies failing target");

    return passed ? 0 : 1;
}
