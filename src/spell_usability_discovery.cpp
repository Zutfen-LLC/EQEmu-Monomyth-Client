#include "spell_usability_discovery.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "logger.h"
#include "spell_usability_discovery_policy.h"

namespace monomyth::spell_usability_discovery {
namespace {

constexpr char kGetSpellLevelNeededErrorString[] =
    "GetSpellLevelNeeded: Unable to get Class Data.";
constexpr std::size_t kStringReferenceScanBytes = 0x400;
constexpr std::size_t kFunctionStartBacktrackBytes = 0x80;
constexpr std::size_t kMaxImmediateReferenceMatches = 8;
constexpr wchar_t kResolverVersion[] = L"v2_fingerprint_cleanroom";
constexpr wchar_t kPacketId[] = L"CLIENT-SPELL-UI-DISCOVERY-FIX-V2";

Result g_result = {};

struct ImageView {
    const std::uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD section_count = 0;
    std::uint32_t image_size = 0;
    const IMAGE_EXPORT_DIRECTORY* export_directory = nullptr;
    std::uint32_t export_directory_size = 0;
};

struct CandidateSource {
    EvidenceSource evidence_source = EvidenceSource::kUnavailable;
    std::uintptr_t address = 0;
    std::uint32_t rva = 0;
    std::wstring resolved_symbol;
    std::wstring discovery_method = L"image_lookup";
};

std::wstring Hex32(std::uint32_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring HexPtr(std::uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

bool IsTraceDevOptInPresent() noexcept {
    wchar_t value[16] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length =
        GetEnvironmentVariableW(L"MONOMYTH_ENABLE_SPELL_USABILITY_TRACE", value, kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        return false;
    }

    return value[0] == L'1' && value[1] == L'\0';
}

bool BuildImageView(ImageView* view) noexcept {
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
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
        nt->FileHeader.NumberOfSections == 0) {
        return false;
    }

    const IMAGE_DATA_DIRECTORY& export_data =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const bool export_dir_available =
        export_data.VirtualAddress != 0 &&
        export_data.Size >= sizeof(IMAGE_EXPORT_DIRECTORY) &&
        export_data.VirtualAddress < nt->OptionalHeader.SizeOfImage &&
        static_cast<std::uint64_t>(export_data.VirtualAddress) + export_data.Size <=
            nt->OptionalHeader.SizeOfImage;

    view->base = base;
    view->nt = nt;
    view->sections = IMAGE_FIRST_SECTION(nt);
    view->section_count = nt->FileHeader.NumberOfSections;
    view->image_size = nt->OptionalHeader.SizeOfImage;
    view->export_directory = export_dir_available
        ? reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + export_data.VirtualAddress)
        : nullptr;
    view->export_directory_size = export_dir_available ? export_data.Size : 0;
    return true;
}

std::uint32_t SectionSpan(const IMAGE_SECTION_HEADER& section) noexcept {
    return std::max(section.Misc.VirtualSize, section.SizeOfRawData);
}

bool SectionContains(
    const IMAGE_SECTION_HEADER& section,
    std::uint32_t rva,
    std::size_t length) noexcept {
    const std::uint32_t span = SectionSpan(section);
    if (span == 0 || rva < section.VirtualAddress) {
        return false;
    }

    const std::uint32_t offset = rva - section.VirtualAddress;
    return offset <= span && length <= (span - offset);
}

const IMAGE_SECTION_HEADER* FindSection(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length,
    DWORD required_characteristics) noexcept {
    for (WORD i = 0; i < image.section_count; ++i) {
        const IMAGE_SECTION_HEADER& section = image.sections[i];
        if ((section.Characteristics & required_characteristics) != required_characteristics) {
            continue;
        }

        if (SectionContains(section, rva, length)) {
            return &section;
        }
    }

    return nullptr;
}

const std::uint8_t* SearchBytes(
    const std::uint8_t* haystack,
    std::size_t haystack_size,
    const std::uint8_t* needle,
    std::size_t needle_size) noexcept {
    if (haystack == nullptr || needle == nullptr || needle_size == 0 || haystack_size < needle_size) {
        return nullptr;
    }

    for (std::size_t i = 0; i <= haystack_size - needle_size; ++i) {
        if (std::memcmp(haystack + i, needle, needle_size) == 0) {
            return haystack + i;
        }
    }

    return nullptr;
}

bool IsRvaWithinImage(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length) noexcept {
    if (image.image_size == 0 || rva >= image.image_size) {
        return false;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(rva) + length;
    return end <= image.image_size;
}

bool IsExecutableAddress(
    const ImageView& image,
    std::uintptr_t address) noexcept {
    if (address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return false;
    }

    const std::uint32_t rva =
        static_cast<std::uint32_t>(address - reinterpret_cast<std::uintptr_t>(image.base));
    return FindSection(image, rva, 1, IMAGE_SCN_MEM_EXECUTE) != nullptr;
}

const std::uint8_t* FindAsciiString(
    const ImageView& image,
    const char* needle,
    std::size_t needle_size) noexcept {
    if (needle == nullptr || needle_size == 0) {
        return nullptr;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(needle);
    for (WORD i = 0; i < image.section_count; ++i) {
        const IMAGE_SECTION_HEADER& section = image.sections[i];
        const bool readable = (section.Characteristics & IMAGE_SCN_MEM_READ) != 0;
        const bool executable = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (!readable || executable || SectionSpan(section) < needle_size) {
            continue;
        }

        const auto* start = image.base + section.VirtualAddress;
        const std::uint32_t span = SectionSpan(section);
        const auto* found = SearchBytes(start, span, bytes, needle_size);
        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

bool HasImmediateReference(
    const ImageView& image,
    std::uintptr_t function_address,
    std::size_t scan_bytes,
    std::uintptr_t referenced_address) noexcept {
    if (!IsExecutableAddress(image, function_address) ||
        function_address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return false;
    }

    const std::uint32_t function_rva = static_cast<std::uint32_t>(
        function_address - reinterpret_cast<std::uintptr_t>(image.base));
    const IMAGE_SECTION_HEADER* section =
        FindSection(image, function_rva, 1, IMAGE_SCN_MEM_EXECUTE);
    if (section == nullptr) {
        return false;
    }

    const std::uint32_t available = section->VirtualAddress + SectionSpan(*section) - function_rva;
    const std::size_t bounded_scan = std::min(scan_bytes, static_cast<std::size_t>(available));
    if (bounded_scan < sizeof(std::uint32_t)) {
        return false;
    }

    const std::uint32_t referenced_address_32 =
        static_cast<std::uint32_t>(referenced_address);
    return SearchBytes(
               reinterpret_cast<const std::uint8_t*>(function_address),
               bounded_scan,
               reinterpret_cast<const std::uint8_t*>(&referenced_address_32),
               sizeof(referenced_address_32)) != nullptr;
}

std::size_t FindImmediateReferenceMatches(
    const ImageView& image,
    std::uintptr_t referenced_address,
    std::array<std::uintptr_t, kMaxImmediateReferenceMatches>* matches) noexcept {
    if (matches == nullptr) {
        return 0;
    }

    matches->fill(0);
    std::size_t count = 0;
    const std::uint32_t referenced_address_32 =
        static_cast<std::uint32_t>(referenced_address);

    for (WORD i = 0; i < image.section_count; ++i) {
        const IMAGE_SECTION_HEADER& section = image.sections[i];
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }

        const std::uint32_t span = SectionSpan(section);
        if (span < sizeof(referenced_address_32)) {
            continue;
        }

        const auto* start = image.base + section.VirtualAddress;
        std::size_t offset = 0;
        while (offset <= span - sizeof(referenced_address_32)) {
            const auto* found = SearchBytes(
                start + offset,
                span - offset,
                reinterpret_cast<const std::uint8_t*>(&referenced_address_32),
                sizeof(referenced_address_32));
            if (found == nullptr) {
                break;
            }

            if (count < matches->size()) {
                (*matches)[count] = reinterpret_cast<std::uintptr_t>(found);
            }
            ++count;
            offset = static_cast<std::size_t>((found - start) + 1);
        }
    }

    return count;
}

bool HasPlausibleX86Prologue(
    const std::uint8_t* code,
    std::size_t available) noexcept {
    if (code == nullptr || available < 3) {
        return false;
    }

    const std::array<std::array<std::uint8_t, 3>, 4> patterns = {{
        {0x55, 0x8b, 0xec},
        {0x56, 0x8b, 0xf1},
        {0x57, 0x8b, 0xf9},
        {0x8b, 0x44, 0x24},
    }};

    for (const auto& pattern : patterns) {
        if (std::memcmp(code, pattern.data(), pattern.size()) == 0) {
            return true;
        }
    }

    return code[0] == 0x8b || code[0] == 0x55 || code[0] == 0x56 || code[0] == 0x57;
}

void PopulateCandidateFields(
    const ImageView& image,
    const CandidateSource& candidate,
    TargetResult* target) {
    if (target == nullptr) {
        return;
    }

    target->module_base = reinterpret_cast<std::uintptr_t>(image.base);
    target->candidate_rva = candidate.rva;
    target->candidate_address = candidate.address;
    target->resolved_symbol = candidate.resolved_symbol;
    target->discovery_method = candidate.discovery_method;
    target->evidence_source = EvidenceSourceName(candidate.evidence_source);
}

CandidateSource BuildFingerprintCandidate(
    const ImageView& image,
    std::uintptr_t address,
    const wchar_t* discovery_method,
    const wchar_t* resolved_symbol) {
    CandidateSource candidate = {};
    if (address == 0 || address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return candidate;
    }

    candidate.evidence_source = EvidenceSource::kFingerprintRva;
    candidate.address = address;
    candidate.rva = static_cast<std::uint32_t>(
        candidate.address - reinterpret_cast<std::uintptr_t>(image.base));
    candidate.resolved_symbol = resolved_symbol == nullptr ? L"" : resolved_symbol;
    candidate.discovery_method =
        discovery_method == nullptr ? L"fingerprint_locator" : discovery_method;
    return candidate;
}

TargetResult BuildUnavailableTarget(
    const ImageView& image,
    const wchar_t* target_name,
    const wchar_t* discovery_method,
    const wchar_t* failure_reason,
    const wchar_t* reason,
    const wchar_t* validation_evidence) {
    TargetResult target = {target_name == nullptr ? L"unknown" : target_name};
    target.state = TargetState::kFailed;
    target.module_base = reinterpret_cast<std::uintptr_t>(image.base);
    target.evidence_source = EvidenceSourceName(EvidenceSource::kUnavailable);
    target.discovery_method = discovery_method == nullptr ? L"unknown" : discovery_method;
    target.validation = L"failed";
    target.failure_reason = failure_reason == nullptr ? L"unknown" : failure_reason;
    target.validation_evidence =
        validation_evidence == nullptr ? L"unknown" : validation_evidence;
    target.reason = reason == nullptr ? L"validation failed" : reason;
    return target;
}

bool FindFunctionStartBeforeAddress(
    const ImageView& image,
    std::uintptr_t reference_address,
    std::size_t backtrack_bytes,
    std::uintptr_t* function_start) noexcept {
    if (function_start == nullptr ||
        reference_address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return false;
    }

    const std::uint32_t reference_rva = static_cast<std::uint32_t>(
        reference_address - reinterpret_cast<std::uintptr_t>(image.base));
    const IMAGE_SECTION_HEADER* section =
        FindSection(image, reference_rva, 1, IMAGE_SCN_MEM_EXECUTE);
    if (section == nullptr) {
        return false;
    }

    const std::uintptr_t section_start =
        reinterpret_cast<std::uintptr_t>(image.base) + section->VirtualAddress;
    const std::uintptr_t lower_bound =
        reference_address > backtrack_bytes
        ? std::max(section_start, reference_address - backtrack_bytes)
        : section_start;
    for (std::uintptr_t cursor = reference_address; cursor >= lower_bound; --cursor) {
        const auto* code = reinterpret_cast<const std::uint8_t*>(cursor);
        if (HasPlausibleX86Prologue(code, 8)) {
            *function_start = cursor;
            return true;
        }
        if (cursor == lower_bound) {
            break;
        }
    }

    return false;
}

void ApplyDecision(
    const ImageView& image,
    const CandidateSource& candidate,
    const DecisionResult& decision,
    const std::wstring& validation_evidence,
    const wchar_t* success_reason,
    const wchar_t* failure_reason,
    TargetResult* target) {
    if (target == nullptr) {
        return;
    }

    PopulateCandidateFields(image, candidate, target);
    target->state = decision.state;
    target->exact_signature_validated = decision.exact_signature_validated;
    target->trace_safe = decision.trace_safe;
    target->validation = decision.validation;
    target->failure_reason = decision.failure_reason;
    target->evidence_source = EvidenceSourceName(decision.evidence_source);
    target->validation_evidence = validation_evidence;
    target->reason =
        decision.state == TargetState::kValidated
        ? (success_reason == nullptr ? L"validated" : success_reason)
        : (failure_reason == nullptr ? L"validation failed" : failure_reason);
}

TargetResult DiscoverHandleRButtonUp(const ImageView& image) {
    return BuildUnavailableTarget(
        image,
        L"CInvSlot::HandleRButtonUp",
        L"fingerprint_cleanroom_required",
        L"missing_cleanroom_target",
        L"no checked-in cleanroom locator for CInvSlot::HandleRButtonUp",
        L"runtime_export_lookup=skipped cleanroom_locator=absent");
}

TargetResult DiscoverGetSpellLevelNeeded(const ImageView& image) {
    TargetResult target = {L"GetSpellLevelNeeded"};
    const std::uint8_t* error_string = FindAsciiString(
        image,
        kGetSpellLevelNeededErrorString,
        sizeof(kGetSpellLevelNeededErrorString));
    const bool string_found = error_string != nullptr;
    if (!string_found) {
        return BuildUnavailableTarget(
            image,
            L"GetSpellLevelNeeded",
            L"fingerprint_string_xref",
            L"diagnostic_string_missing",
            L"GetSpellLevelNeeded diagnostic string evidence was not present in the loaded image",
            L"runtime_export_lookup=skipped diagnostic_string=no");
    }

    std::array<std::uintptr_t, kMaxImmediateReferenceMatches> xref_matches = {};
    const std::size_t xref_count = FindImmediateReferenceMatches(
        image,
        reinterpret_cast<std::uintptr_t>(error_string),
        &xref_matches);
    if (xref_count != 1 || xref_matches[0] == 0) {
        std::wstring evidence = L"runtime_export_lookup=skipped diagnostic_string=yes xref_count=";
        evidence += std::to_wstring(xref_count);
        target = BuildUnavailableTarget(
            image,
            L"GetSpellLevelNeeded",
            L"fingerprint_string_xref",
            xref_count == 0 ? L"diagnostic_string_xref_missing" : L"diagnostic_string_xref_ambiguous",
            xref_count == 0
                ? L"GetSpellLevelNeeded diagnostic string had no executable xref"
                : L"GetSpellLevelNeeded diagnostic string had multiple executable xrefs",
            evidence.c_str());
        return target;
    }

    std::uintptr_t function_start = 0;
    if (!FindFunctionStartBeforeAddress(
            image,
            xref_matches[0],
            kFunctionStartBacktrackBytes,
            &function_start)) {
        return BuildUnavailableTarget(
            image,
            L"GetSpellLevelNeeded",
            L"fingerprint_string_xref",
            L"unsupported_prologue",
            L"GetSpellLevelNeeded xref was found but a plausible function start was not",
            L"runtime_export_lookup=skipped diagnostic_string=yes xref_count=1 prologue=no");
    }

    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_string_xref",
        L"GetSpellLevelNeeded");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const auto* code = reinterpret_cast<const std::uint8_t*>(candidate.address);
    const bool plausible_prologue =
        candidate_executable && HasPlausibleX86Prologue(code, 8);
    const bool string_xref = HasImmediateReference(
        image,
        candidate.address,
        kStringReferenceScanBytes,
        reinterpret_cast<std::uintptr_t>(error_string));
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        true,
        false,
        false,
        false,
        candidate_executable,
        plausible_prologue,
        false,
        false,
        true,
        string_found,
        string_xref,
    });

    std::wstring evidence = L"runtime_export_lookup=skipped diagnostic_string=yes";
    evidence += L" xref_count=";
    evidence += std::to_wstring(xref_count);
    evidence += L" xref_address=";
    evidence += HexPtr(xref_matches[0]);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" prologue=";
    evidence += plausible_prologue ? L"plausible" : L"unsupported";
    evidence += L" diagnostic_xref=";
    evidence += string_xref ? L"yes" : L"no";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated diagnostic string xref evidence",
        L"GetSpellLevelNeeded candidate did not satisfy diagnostic string validation",
        &target);
    return target;
}

TargetResult DiscoverCanStartMemming(const ImageView& image) {
    return BuildUnavailableTarget(
        image,
        L"CanStartMemming",
        L"fingerprint_cleanroom_required",
        L"missing_cleanroom_target",
        L"no checked-in cleanroom locator for CanStartMemming",
        L"runtime_export_lookup=skipped cleanroom_locator=absent");
}

TargetResult DiscoverGetUsableClasses(const ImageView& image) {
    return BuildUnavailableTarget(
        image,
        L"GetUsableClasses",
        L"fingerprint_cleanroom_required",
        L"missing_cleanroom_target",
        L"no checked-in cleanroom locator for GetUsableClasses",
        L"runtime_export_lookup=skipped cleanroom_locator=absent");
}

TargetResult DiscoverCanEquip(const ImageView& image) {
    return BuildUnavailableTarget(
        image,
        L"CanEquip",
        L"fingerprint_cleanroom_required",
        L"missing_cleanroom_target",
        L"no checked-in cleanroom locator for CanEquip",
        L"runtime_export_lookup=skipped cleanroom_locator=absent");
}

}  // namespace

void Initialize() noexcept {
    g_result = {};
    g_result.reason = L"initialized";
    g_result.handle_rbutton_up = {L"CInvSlot::HandleRButtonUp"};
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.get_usable_classes = {L"GetUsableClasses"};
    g_result.can_equip = {L"CanEquip"};
    g_result.can_start_memming = {L"CanStartMemming"};
}

Result Run(bool discovery_allowed, bool fingerprint_matched) noexcept {
    g_result = {};
    g_result.allowed = discovery_allowed;
    g_result.trace_dev_opt_in = IsTraceDevOptInPresent();
    g_result.handle_rbutton_up = {L"CInvSlot::HandleRButtonUp"};
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.get_usable_classes = {L"GetUsableClasses"};
    g_result.can_equip = {L"CanEquip"};
    g_result.can_start_memming = {L"CanStartMemming"};

    if (!discovery_allowed) {
        g_result.reason =
            L"skipped because capability manifest denied spell usability discovery";
        if (!fingerprint_matched) {
            g_result.reason = L"skipped because ROF2 fingerprint did not match";
        }
        const wchar_t* failure_reason =
            fingerprint_matched ? L"capability_denied" : L"fingerprint_mismatch";
        g_result.handle_rbutton_up.reason = L"discovery skipped by capability";
        g_result.handle_rbutton_up.state = TargetState::kFailed;
        g_result.handle_rbutton_up.validation = L"failed";
        g_result.handle_rbutton_up.failure_reason = failure_reason;
        g_result.handle_rbutton_up.evidence_source = EvidenceSourceName(EvidenceSource::kNotAttempted);
        g_result.handle_rbutton_up.validation_evidence = L"capability_gate=denied";
        g_result.get_spell_level_needed.reason = L"discovery skipped by capability";
        g_result.get_spell_level_needed.state = TargetState::kFailed;
        g_result.get_spell_level_needed.validation = L"failed";
        g_result.get_spell_level_needed.failure_reason = failure_reason;
        g_result.get_spell_level_needed.evidence_source =
            EvidenceSourceName(EvidenceSource::kNotAttempted);
        g_result.get_spell_level_needed.validation_evidence = L"capability_gate=denied";
        g_result.get_usable_classes.reason = L"discovery skipped by capability";
        g_result.get_usable_classes.state = TargetState::kFailed;
        g_result.get_usable_classes.validation = L"failed";
        g_result.get_usable_classes.failure_reason = failure_reason;
        g_result.get_usable_classes.evidence_source =
            EvidenceSourceName(EvidenceSource::kNotAttempted);
        g_result.get_usable_classes.validation_evidence = L"capability_gate=denied";
        g_result.can_equip.reason = L"discovery skipped by capability";
        g_result.can_equip.state = TargetState::kFailed;
        g_result.can_equip.validation = L"failed";
        g_result.can_equip.failure_reason = failure_reason;
        g_result.can_equip.evidence_source = EvidenceSourceName(EvidenceSource::kNotAttempted);
        g_result.can_equip.validation_evidence = L"capability_gate=denied";
        g_result.can_start_memming.reason = L"discovery skipped by capability";
        g_result.can_start_memming.state = TargetState::kFailed;
        g_result.can_start_memming.validation = L"failed";
        g_result.can_start_memming.failure_reason = failure_reason;
        g_result.can_start_memming.evidence_source =
            EvidenceSourceName(EvidenceSource::kNotAttempted);
        g_result.can_start_memming.validation_evidence = L"capability_gate=denied";
        return g_result;
    }

    ImageView image = {};
    if (!BuildImageView(&image)) {
        g_result.reason = L"failed because host PE image unavailable";
        g_result.handle_rbutton_up.state = TargetState::kFailed;
        g_result.handle_rbutton_up.discovery_method = L"image_lookup";
        g_result.handle_rbutton_up.evidence_source = EvidenceSourceName(EvidenceSource::kUnavailable);
        g_result.handle_rbutton_up.validation = L"failed";
        g_result.handle_rbutton_up.failure_reason = L"image_view_unavailable";
        g_result.handle_rbutton_up.reason = L"host PE image unavailable";
        g_result.handle_rbutton_up.validation_evidence = L"image_view=no";
        g_result.get_spell_level_needed.state = TargetState::kFailed;
        g_result.get_spell_level_needed.discovery_method = L"image_lookup";
        g_result.get_spell_level_needed.evidence_source =
            EvidenceSourceName(EvidenceSource::kUnavailable);
        g_result.get_spell_level_needed.validation = L"failed";
        g_result.get_spell_level_needed.failure_reason = L"image_view_unavailable";
        g_result.get_spell_level_needed.reason = L"host PE image unavailable";
        g_result.get_spell_level_needed.validation_evidence = L"image_view=no";
        g_result.get_usable_classes.state = TargetState::kFailed;
        g_result.get_usable_classes.discovery_method = L"image_lookup";
        g_result.get_usable_classes.evidence_source = EvidenceSourceName(EvidenceSource::kUnavailable);
        g_result.get_usable_classes.validation = L"failed";
        g_result.get_usable_classes.failure_reason = L"image_view_unavailable";
        g_result.get_usable_classes.reason = L"host PE image unavailable";
        g_result.get_usable_classes.validation_evidence = L"image_view=no";
        g_result.can_equip.state = TargetState::kFailed;
        g_result.can_equip.discovery_method = L"image_lookup";
        g_result.can_equip.evidence_source = EvidenceSourceName(EvidenceSource::kUnavailable);
        g_result.can_equip.validation = L"failed";
        g_result.can_equip.failure_reason = L"image_view_unavailable";
        g_result.can_equip.reason = L"host PE image unavailable";
        g_result.can_equip.validation_evidence = L"image_view=no";
        g_result.can_start_memming.state = TargetState::kFailed;
        g_result.can_start_memming.discovery_method = L"image_lookup";
        g_result.can_start_memming.evidence_source = EvidenceSourceName(EvidenceSource::kUnavailable);
        g_result.can_start_memming.validation = L"failed";
        g_result.can_start_memming.failure_reason = L"image_view_unavailable";
        g_result.can_start_memming.reason = L"host PE image unavailable";
        g_result.can_start_memming.validation_evidence = L"image_view=no";
        return g_result;
    }

    g_result.reason = L"resolver=";
    g_result.reason += kResolverVersion;
    g_result.reason += L" packet_id=";
    g_result.reason += kPacketId;
    g_result.reason += L" known_non_export_targets=cleanroom_or_fail_closed";
    g_result.handle_rbutton_up = DiscoverHandleRButtonUp(image);
    g_result.get_spell_level_needed = DiscoverGetSpellLevelNeeded(image);
    g_result.get_usable_classes = DiscoverGetUsableClasses(image);
    g_result.can_equip = DiscoverCanEquip(image);
    g_result.can_start_memming = DiscoverCanStartMemming(image);
    return g_result;
}

void Shutdown() noexcept {
    g_result = {};
    g_result.reason = L"shutdown";
    g_result.handle_rbutton_up = {L"CInvSlot::HandleRButtonUp"};
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.get_usable_classes = {L"GetUsableClasses"};
    g_result.can_equip = {L"CanEquip"};
    g_result.can_start_memming = {L"CanStartMemming"};
}

Result GetResult() noexcept {
    return g_result;
}

void LogResult(const Result& result) noexcept {
    const TargetResult* targets[] = {
        &result.handle_rbutton_up,
        &result.get_spell_level_needed,
        &result.get_usable_classes,
        &result.can_equip,
        &result.can_start_memming,
    };

    for (const TargetResult* target : targets) {
        if (target == nullptr) {
            continue;
        }

        std::wstring message = L"SpellUsabilityDiscovery target=";
        message += target->target;
        message += L" enabled=";
        message += target->state == TargetState::kValidated ? L"true" : L"false";
        message += L" state=";
        message += TargetStateName(target->state);
        message += L" evidence_source=";
        message += target->evidence_source;
        message += L" validation=";
        message += target->validation;
        message += L" failure_reason=";
        message += target->failure_reason;
        message += L" resolver=";
        message += kResolverVersion;
        message += L" packet_id=";
        message += kPacketId;
        if (target->module_base != 0) {
            message += L" module_base=";
            message += HexPtr(target->module_base);
        }
        if (target->candidate_rva != 0) {
            message += L" rva=";
            message += Hex32(target->candidate_rva);
        }
        if (target->candidate_address != 0) {
            message += L" address=";
            message += HexPtr(target->candidate_address);
        }
        message += L" trace_safe=";
        message += target->trace_safe ? L"true" : L"false";
        message += L" method=";
        message += target->discovery_method.empty() ? L"unknown" : target->discovery_method;
        if (!target->resolved_symbol.empty()) {
            message += L" symbol=\"";
            message += target->resolved_symbol;
            message += L"\"";
        }
        message += L" reason=\"";
        message += target->reason.empty() ? L"unknown" : target->reason;
        message += L"\"";
        message += L" evidence=\"";
        message += target->validation_evidence.empty() ? L"unknown" : target->validation_evidence;
        message += L"\"";
        monomyth::logger::Log(message);
    }
}

}  // namespace monomyth::spell_usability_discovery
