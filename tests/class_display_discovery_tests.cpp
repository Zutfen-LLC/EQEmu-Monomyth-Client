#include "class_display_discovery.h"

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

bool IsFailClosedReason(std::wstring_view reason) {
    return !reason.empty() &&
        reason != L"none" &&
        reason != L"not_attempted";
}

}  // namespace

namespace monomyth::logger {

void Log(std::wstring_view) noexcept {}
void Log(const wchar_t*) noexcept {}
void Flush() noexcept {}

}  // namespace monomyth::logger

int main() {
    bool passed = true;

    using monomyth::spell_usability_discovery::TargetState;

    monomyth::class_display_discovery::Initialize();
    const auto denied = monomyth::class_display_discovery::Run(false, false);
    passed &= Expect(!denied.allowed, "discovery denied when guards fail");
    passed &= Expect(
        denied.reason ==
            L"class display discovery requires validated ROF2 fingerprint and hook allowance",
        "denied discovery reason");
    passed &= Expect(
        denied.who_class_name.state == TargetState::kNotAttempted,
        "who_class_name remains not_attempted when denied");

    const auto unpinned = monomyth::class_display_discovery::Run(true, true);
    passed &= Expect(unpinned.allowed, "discovery allowed when guards pass");
    passed &= Expect(
        unpinned.who_class_name.state == TargetState::kFailed,
        "who_class_name fails closed when unpinned");
    passed &= Expect(
        unpinned.get_class_desc.state == TargetState::kFailed,
        "get_class_desc fails closed when unpinned");
    passed &= Expect(
        unpinned.get_class_three_letter_code.state == TargetState::kFailed,
        "get_class_three_letter_code fails closed when unpinned");
    passed &= Expect(
        unpinned.char_select_class_name_func.state == TargetState::kFailed,
        "char_select_class_name_func fails closed when unpinned");
    passed &= Expect(
        IsFailClosedReason(unpinned.who_class_name.failure_reason),
        "unpinned failure reason");

    const auto cached = monomyth::class_display_discovery::GetResult();
    passed &= Expect(
        cached.get_class_desc.failure_reason == unpinned.get_class_desc.failure_reason,
        "cached result matches last run");

    monomyth::class_display_discovery::Shutdown();
    const auto shutdown = monomyth::class_display_discovery::GetResult();
    passed &= Expect(
        shutdown.reason == L"shutdown",
        "shutdown resets global result state");

    return passed ? 0 : 1;
}
