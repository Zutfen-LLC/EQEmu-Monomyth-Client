#include "class_display_discovery.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <sstream>

#include "logger.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace monomyth::class_display_discovery {
namespace {

constexpr std::uint32_t kGetClassDescRva = 0x001153C0;
constexpr std::array<std::uint8_t, 24> kGetClassDescEntryBytes = {{
    0x8B, 0x44, 0x24, 0x04, 0x48, 0x83, 0xF8, 0x0F,
    0x0F, 0x87, 0x57, 0x01, 0x00, 0x00, 0xFF, 0x24,
    0x85, 0x3C, 0x55, 0x51, 0x00, 0x8B, 0x0D, 0xC4,
}};
constexpr std::uint32_t kWhoClassNameRva = 0x00136310;
constexpr std::array<std::uint8_t, 32> kWhoClassNameEntryBytes = {{
    0x6A, 0xFF, 0x68, 0xA4, 0x89, 0x97, 0x00, 0x64,
    0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89,
    0x25, 0x00, 0x00, 0x00, 0x00, 0x81, 0xEC, 0x14,
    0x02, 0x00, 0x00, 0x53, 0x57, 0x33, 0xFF, 0x89,
}};
constexpr std::uint32_t kGetClassThreeLetterCodeRva = 0x00114DC0;
constexpr std::array<std::uint8_t, 24> kGetClassThreeLetterCodeEntryBytes = {{
    0x8B, 0x44, 0x24, 0x04, 0x48, 0x83, 0xF8, 0x41,
    0x0F, 0x87, 0xED, 0x02, 0x00, 0x00, 0x0F, 0xB6,
    0x80, 0x60, 0x51, 0x51, 0x00, 0xFF, 0x24, 0x85,
}};
constexpr std::uint32_t kCharSelectClassNameFuncRva = 0x00321210;
constexpr std::array<std::uint8_t, 32> kCharSelectClassNameFuncEntryBytes = {{
    0x6A, 0xFF, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
    0x68, 0x15, 0x2D, 0x99, 0x00, 0x50, 0x64, 0x89,
    0x25, 0x00, 0x00, 0x00, 0x00, 0x81, 0xEC, 0x98,
    0x00, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57, 0x8B,
}};

Result g_result = {};

struct ImageView {
    const std::uint8_t* base = nullptr;
    std::uint32_t image_size = 0;
};

bool BuildImageView(ImageView* view) noexcept {
#if defined(_WIN32)
    if (view == nullptr) {
        return false;
    }

    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return false;
    }

    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) {
        return false;
    }

    view->base = base;
    view->image_size = nt->OptionalHeader.SizeOfImage;
    return true;
#else
    (void)view;
    return false;
#endif
}

bool IsRvaWithinImage(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length) noexcept {
    if (image.base == nullptr || image.image_size == 0 || rva >= image.image_size) {
        return false;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(rva) + length;
    return end <= image.image_size;
}

bool ResolveRvaAddress(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length,
    std::uintptr_t* address) noexcept {
    if (address == nullptr || !IsRvaWithinImage(image, rva, length)) {
        return false;
    }

    *address = reinterpret_cast<std::uintptr_t>(image.base) + rva;
    return true;
}

bool BytesMatchAtRva(
    const ImageView& image,
    std::uint32_t rva,
    const std::uint8_t* expected,
    std::size_t length) noexcept {
    if (expected == nullptr || length == 0 || !IsRvaWithinImage(image, rva, length)) {
        return false;
    }

    return std::memcmp(image.base + rva, expected, length) == 0;
}

bool IsExecutableAddress(
    const ImageView& image,
    std::uintptr_t address) noexcept {
#if defined(_WIN32)
    if (image.base == nullptr || address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return false;
    }

    const auto* base = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        image.base + base->e_lfanew);
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    const std::uint32_t rva =
        static_cast<std::uint32_t>(address - reinterpret_cast<std::uintptr_t>(image.base));
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& section = sections[i];
        const std::uint32_t span = section.Misc.VirtualSize > section.SizeOfRawData
            ? section.Misc.VirtualSize
            : section.SizeOfRawData;
        if (span == 0 || rva < section.VirtualAddress) {
            continue;
        }

        const std::uint32_t offset = rva - section.VirtualAddress;
        if (offset < span &&
            (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == IMAGE_SCN_MEM_EXECUTE) {
            return true;
        }
    }

    return false;
#else
    (void)image;
    (void)address;
    return false;
#endif
}

TargetResult DiscoverPinnedEntryBytesTarget(
    const ImageView& image,
    const wchar_t* target_name,
    std::uint32_t rva,
    const std::uint8_t* expected_bytes,
    std::size_t expected_length,
    const wchar_t* rva_failure_reason,
    const wchar_t* executable_failure_reason,
    const wchar_t* bytes_failure_reason,
    const wchar_t* success_reason) noexcept;

void ResetTarget(
    TargetResult* target,
    const wchar_t* name) noexcept {
    if (target == nullptr) {
        return;
    }

    *target = {};
    target->target = name == nullptr ? L"unknown" : name;
}

void MarkUnavailable(
    TargetResult* target,
    const wchar_t* failure_reason,
    const wchar_t* reason) noexcept {
    if (target == nullptr) {
        return;
    }

    target->state = monomyth::spell_usability_discovery::TargetState::kFailed;
    target->validation = L"failed";
    target->failure_reason =
        failure_reason == nullptr ? L"unknown_failure" : failure_reason;
    target->reason = reason == nullptr ? L"target unavailable" : reason;
    target->discovery_method = L"cleanroom_locator_pending";
    target->evidence_source = monomyth::spell_usability_discovery::EvidenceSourceName(
        monomyth::spell_usability_discovery::EvidenceSource::kUnavailable);
    target->validation_evidence = L"cleanroom locator for current ROF2 binary is not pinned";
}

TargetResult DiscoverPinnedGetClassDesc(const ImageView& image) noexcept {
    return DiscoverPinnedEntryBytesTarget(
        image,
        L"GetClassDesc",
        kGetClassDescRva,
        kGetClassDescEntryBytes.data(),
        kGetClassDescEntryBytes.size(),
        L"GetClassDesc cleanroom RVA did not resolve inside the loaded image",
        L"GetClassDesc cleanroom RVA resolved to a non-executable region",
        L"GetClassDesc cleanroom RVA resolved, but the entry bytes did not match",
        L"GetClassDesc validated by exact cleanroom RVA and entry-byte match");
}

TargetResult DiscoverPinnedEntryBytesTarget(
    const ImageView& image,
    const wchar_t* target_name,
    std::uint32_t rva,
    const std::uint8_t* expected_bytes,
    std::size_t expected_length,
    const wchar_t* rva_failure_reason,
    const wchar_t* executable_failure_reason,
    const wchar_t* bytes_failure_reason,
    const wchar_t* success_reason) noexcept {
    TargetResult target = {target_name};
    target.discovery_method = L"cleanroom_rva";
    target.evidence_source = monomyth::spell_usability_discovery::EvidenceSourceName(
        monomyth::spell_usability_discovery::EvidenceSource::kCleanroomRva);
    target.module_base = reinterpret_cast<std::uintptr_t>(image.base);
    target.candidate_rva = rva;
    ResolveRvaAddress(image, rva, expected_length, &target.candidate_address);

    if (target.candidate_address == 0) {
        target.state = monomyth::spell_usability_discovery::TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"rva_out_of_range";
        target.reason = rva_failure_reason == nullptr ? L"cleanroom RVA did not resolve" : rva_failure_reason;
        std::wstringstream stream;
        stream << L"cleanroom_rva=0x" << std::hex << rva << L" resolved=no";
        target.validation_evidence = stream.str();
        return target;
    }

    const bool executable = IsExecutableAddress(image, target.candidate_address);
    const bool exact_match =
        BytesMatchAtRva(image, rva, expected_bytes, expected_length);
    target.exact_signature_validated = exact_match;
    target.hook_safe = executable && exact_match;

    std::wstringstream evidence;
    evidence << L"cleanroom_rva=0x" << std::hex << rva
             << L" entry_bytes=" << (exact_match ? L"matched" : L"mismatch")
             << L" executable=" << (executable ? L"yes" : L"no");
    target.validation_evidence = evidence.str();

    if (!executable) {
        target.state = monomyth::spell_usability_discovery::TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"non_executable_target";
        target.reason = executable_failure_reason == nullptr
            ? L"cleanroom RVA resolved to a non-executable region"
            : executable_failure_reason;
        target.hook_safe = false;
        return target;
    }

    if (!exact_match) {
        target.state = monomyth::spell_usability_discovery::TargetState::kFoundUnvalidated;
        target.validation = L"failed";
        target.failure_reason = L"entry_bytes_mismatch";
        target.reason = bytes_failure_reason == nullptr
            ? L"cleanroom RVA resolved, but the entry bytes did not match"
            : bytes_failure_reason;
        target.hook_safe = false;
        return target;
    }

    target.state = monomyth::spell_usability_discovery::TargetState::kValidated;
    target.validation = L"passed";
    target.failure_reason = L"none";
    target.reason = success_reason == nullptr ? L"validated" : success_reason;
    return target;
}

TargetResult DiscoverPinnedWhoClassName(const ImageView& image) noexcept {
    return DiscoverPinnedEntryBytesTarget(
        image,
        L"WhoClassName",
        kWhoClassNameRva,
        kWhoClassNameEntryBytes.data(),
        kWhoClassNameEntryBytes.size(),
        L"WhoClassName surrogate RVA did not resolve inside the loaded image",
        L"WhoClassName surrogate RVA resolved to a non-executable region",
        L"WhoClassName surrogate RVA resolved, but the entry bytes did not match",
        L"WhoClassName validated by exact cleanroom RVA and entry-byte match on the strongest recovered implementation seam");
}

TargetResult DiscoverPinnedGetClassThreeLetterCode(const ImageView& image) noexcept {
    return DiscoverPinnedEntryBytesTarget(
        image,
        L"GetClassThreeLetterCode",
        kGetClassThreeLetterCodeRva,
        kGetClassThreeLetterCodeEntryBytes.data(),
        kGetClassThreeLetterCodeEntryBytes.size(),
        L"GetClassThreeLetterCode cleanroom RVA did not resolve inside the loaded image",
        L"GetClassThreeLetterCode cleanroom RVA resolved to a non-executable region",
        L"GetClassThreeLetterCode cleanroom RVA resolved, but the entry bytes did not match",
        L"GetClassThreeLetterCode validated by exact cleanroom RVA and entry-byte match");
}

TargetResult DiscoverPinnedCharSelectClassNameFunc(const ImageView& image) noexcept {
    return DiscoverPinnedEntryBytesTarget(
        image,
        L"ProgressionSelectionClassValueWriter",
        kCharSelectClassNameFuncRva,
        kCharSelectClassNameFuncEntryBytes.data(),
        kCharSelectClassNameFuncEntryBytes.size(),
        L"ProgressionSelectionClassValueWriter cleanroom RVA did not resolve inside the loaded image",
        L"ProgressionSelectionClassValueWriter cleanroom RVA resolved to a non-executable region",
        L"ProgressionSelectionClassValueWriter cleanroom RVA resolved, but the entry bytes did not match",
        L"ProgressionSelectionClassValueWriter validated by exact cleanroom RVA and entry-byte match on the strongest recovered selected-entry class-value writer seam");
}

void LogTarget(const TargetResult& target) {
    std::wstring message = L"ClassDisplayDiscovery target=";
    message += target.target == nullptr ? L"unknown" : target.target;
    message += L" state=";
    message += monomyth::spell_usability_discovery::TargetStateName(target.state);
    message += L" evidence_source=";
    message += target.evidence_source.empty() ? L"unknown" : target.evidence_source;
    message += L" discovery_method=";
    message += target.discovery_method.empty() ? L"unknown" : target.discovery_method;
    message += L" validation=";
    message += target.validation.empty() ? L"unknown" : target.validation;
    message += L" failure_reason=";
    message += target.failure_reason.empty() ? L"unknown" : target.failure_reason;
    message += L" reason=\"";
    message += target.reason.empty() ? L"unknown" : target.reason;
    message += L"\"";
    message += L" validation_evidence=\"";
    message += target.validation_evidence.empty() ? L"unknown" : target.validation_evidence;
    message += L"\"";
    monomyth::logger::Log(message);
}

void InitializeResult() noexcept {
    g_result = {};
    g_result.reason = L"initialized";
    ResetTarget(&g_result.who_class_name, L"WhoClassName");
    ResetTarget(&g_result.get_class_desc, L"GetClassDesc");
    ResetTarget(&g_result.get_class_three_letter_code, L"GetClassThreeLetterCode");
    ResetTarget(
        &g_result.char_select_class_name_func,
        L"ProgressionSelectionClassValueWriter");
}

}  // namespace

void Initialize() noexcept {
    InitializeResult();
}

Result Run(bool discovery_allowed, bool fingerprint_matched) noexcept {
    InitializeResult();
    g_result.allowed = discovery_allowed && fingerprint_matched;
    if (!g_result.allowed) {
        g_result.reason =
            L"class display discovery requires validated ROF2 fingerprint and hook allowance";
        return g_result;
    }

    ImageView image = {};
    if (!BuildImageView(&image)) {
#if !defined(_WIN32)
        g_result.reason =
            L"class display discovery is enabled, but current ROF2 formatter targets are not yet pinned";
        MarkUnavailable(
            &g_result.who_class_name,
            L"cleanroom_locator_unpinned",
            L"WhoClassName target is not pinned for the current ROF2 binary");
        MarkUnavailable(
            &g_result.get_class_desc,
            L"cleanroom_locator_unpinned",
            L"GetClassDesc target is not pinned for the current ROF2 binary");
        MarkUnavailable(
            &g_result.get_class_three_letter_code,
            L"cleanroom_locator_unpinned",
            L"GetClassThreeLetterCode target is not pinned for the current ROF2 binary");
        MarkUnavailable(
            &g_result.char_select_class_name_func,
            L"cleanroom_locator_unpinned",
            L"ProgressionSelectionClassValueWriter target is not pinned for the current ROF2 binary");
        return g_result;
#else
        g_result.reason = L"class display discovery could not build a view of the loaded image";
        MarkUnavailable(
            &g_result.who_class_name,
            L"image_view_unavailable",
            L"WhoClassName target could not be evaluated because the host PE image was unavailable");
        MarkUnavailable(
            &g_result.get_class_desc,
            L"image_view_unavailable",
            L"GetClassDesc target could not be evaluated because the host PE image was unavailable");
        MarkUnavailable(
            &g_result.get_class_three_letter_code,
            L"image_view_unavailable",
            L"GetClassThreeLetterCode target could not be evaluated because the host PE image was unavailable");
        MarkUnavailable(
            &g_result.char_select_class_name_func,
            L"image_view_unavailable",
            L"ProgressionSelectionClassValueWriter target could not be evaluated because the host PE image was unavailable");
        return g_result;
#endif
    }

    g_result.reason =
        L"class display discovery is partially enabled; current ROF2 formatter coverage is incomplete";
    g_result.who_class_name = DiscoverPinnedWhoClassName(image);
    g_result.get_class_desc = DiscoverPinnedGetClassDesc(image);
    g_result.get_class_three_letter_code = DiscoverPinnedGetClassThreeLetterCode(image);
    g_result.char_select_class_name_func = DiscoverPinnedCharSelectClassNameFunc(image);
    return g_result;
}

void Shutdown() noexcept {
    InitializeResult();
    g_result.reason = L"shutdown";
}

Result GetResult() noexcept {
    return g_result;
}

void LogResult(const Result& result) noexcept {
    std::wstring summary = L"ClassDisplayDiscovery allowed=";
    summary += result.allowed ? L"true" : L"false";
    summary += L" reason=\"";
    summary += result.reason.empty() ? L"unknown" : result.reason;
    summary += L"\"";
    monomyth::logger::Log(summary);
    LogTarget(result.who_class_name);
    LogTarget(result.get_class_desc);
    LogTarget(result.get_class_three_letter_code);
    LogTarget(result.char_select_class_name_func);
}

}  // namespace monomyth::class_display_discovery
