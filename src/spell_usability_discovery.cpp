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

constexpr char kGetSpellLevelNeededMangled[] =
    "?GetSpellLevelNeeded@EQ_Spell@EQClasses@@QBEEI@Z";
constexpr char kGetSpellLevelNeededWrapper[] = "EQ_Spell__GetSpellLevelNeeded";
constexpr char kHandleRButtonUpMangled[] =
    "?HandleRButtonUp@CInvSlot@EQClasses@@QAEXPAVCXPoint@2@@Z";
constexpr char kHandleRButtonUpWrapper[] = "CInvSlot__HandleRButtonUp";
constexpr char kCanEquipMangled[] =
    "?CanEquip@EQ_Character@EQClasses@@QAEHPAKKK@Z";
constexpr char kCanEquipWrapper[] = "EQ_Character__CanEquip";
constexpr char kGetUsableClassesWrapper[] = "EQ_Character__GetUsableClasses";
constexpr char kCanStartMemmingMangled[] =
    "?CanStartMemming@CSpellBookWnd@EQClasses@@QAE_NH@Z";
constexpr char kCanStartMemmingWrapper[] = "CSpellBookWnd__CanStartMemming";
constexpr char kGetSpellLevelNeededErrorString[] =
    "GetSpellLevelNeeded: Unable to get Class Data.";
constexpr std::size_t kStringReferenceScanBytes = 0x400;

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

struct ExportMatch {
    bool found = false;
    const char* name = nullptr;
    std::uint32_t export_rva = 0;
    std::uintptr_t export_address = 0;
};

struct WrapperForward {
    bool resolved = false;
    bool direct_alias = false;
    bool direct_jump = false;
    bool direct_call = false;
    std::uintptr_t target_address = 0;
    std::wstring evidence = L"wrapper evidence unavailable";
};

struct CandidateSource {
    EvidenceSource evidence_source = EvidenceSource::kUnavailable;
    std::uintptr_t address = 0;
    std::uint32_t rva = 0;
    std::wstring resolved_symbol;
    std::wstring discovery_method = L"image_lookup";
    bool runtime_export_found = false;
    bool wrapper_candidate_found = false;
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

std::wstring AsWide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return L"";
    }

    std::wstring wide;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        wide.push_back(static_cast<wchar_t>(*cursor));
    }
    return wide;
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

bool FindExportByName(
    const ImageView& image,
    const char* name,
    ExportMatch* match) noexcept {
    if (name == nullptr || name[0] == '\0' || match == nullptr) {
        return false;
    }

    *match = {};
    match->name = name;

    if (image.export_directory == nullptr) {
        return false;
    }

    const auto names_rva = image.export_directory->AddressOfNames;
    const auto ordinals_rva = image.export_directory->AddressOfNameOrdinals;
    const auto functions_rva = image.export_directory->AddressOfFunctions;
    const DWORD name_count = image.export_directory->NumberOfNames;
    const DWORD function_count = image.export_directory->NumberOfFunctions;
    if (!IsRvaWithinImage(image, names_rva, name_count * sizeof(DWORD)) ||
        !IsRvaWithinImage(image, ordinals_rva, name_count * sizeof(WORD)) ||
        !IsRvaWithinImage(image, functions_rva, function_count * sizeof(DWORD))) {
        return false;
    }

    const auto* names = reinterpret_cast<const DWORD*>(image.base + names_rva);
    const auto* ordinals = reinterpret_cast<const WORD*>(image.base + ordinals_rva);
    const auto* functions = reinterpret_cast<const DWORD*>(image.base + functions_rva);

    for (DWORD i = 0; i < name_count; ++i) {
        const DWORD export_name_rva = names[i];
        if (!IsRvaWithinImage(image, export_name_rva, 1)) {
            continue;
        }

        const char* export_name = reinterpret_cast<const char*>(image.base + export_name_rva);
        if (std::strcmp(export_name, name) != 0) {
            continue;
        }

        const WORD ordinal = ordinals[i];
        if (ordinal >= function_count) {
            return false;
        }

        const DWORD export_rva = functions[ordinal];
        if (!IsRvaWithinImage(image, export_rva, 1)) {
            return false;
        }

        match->found = true;
        match->export_rva = export_rva;
        match->export_address =
            reinterpret_cast<std::uintptr_t>(image.base) + export_rva;
        return true;
    }

    return false;
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

WrapperForward AnalyzeWrapperForward(
    const ImageView& image,
    const ExportMatch& wrapper) noexcept {
    WrapperForward forward = {};
    if (!wrapper.found || wrapper.export_address == 0 || !IsExecutableAddress(image, wrapper.export_address)) {
        forward.evidence = L"wrapper export missing or non-executable";
        return forward;
    }

    if (wrapper.export_rva != 0) {
        forward.resolved = true;
        forward.direct_alias = true;
        forward.target_address = wrapper.export_address;
        forward.evidence = L"wrapper direct_alias";
    }

    const auto* code = reinterpret_cast<const std::uint8_t*>(wrapper.export_address);
    if (wrapper.export_rva + 1 > image.image_size) {
        forward.evidence = L"wrapper RVA resolves outside loaded image";
        return forward;
    }

    if (code[0] == 0xe9 && IsRvaWithinImage(image, wrapper.export_rva, 5)) {
        std::int32_t relative = 0;
        std::memcpy(&relative, code + 1, sizeof(relative));
        const std::uintptr_t next_instruction = wrapper.export_address + 5;
        forward.resolved = true;
        forward.direct_jump = true;
        forward.target_address = next_instruction + relative;
        forward.evidence = L"wrapper direct_jmp";
        return forward;
    }

    if (code[0] == 0xe8 && IsRvaWithinImage(image, wrapper.export_rva, 6) &&
        (code[5] == 0xc3 || code[5] == 0xc2)) {
        std::int32_t relative = 0;
        std::memcpy(&relative, code + 1, sizeof(relative));
        const std::uintptr_t next_instruction = wrapper.export_address + 5;
        forward.resolved = true;
        forward.direct_call = true;
        forward.target_address = next_instruction + relative;
        forward.evidence = L"wrapper direct_call_then_ret";
        return forward;
    }

    forward.evidence = L"wrapper present but forwarding shape unsupported";
    return forward;
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

CandidateSource BuildRuntimeExportCandidate(
    const ExportMatch& export_match,
    const wchar_t* discovery_method) {
    CandidateSource candidate = {};
    if (!export_match.found) {
        return candidate;
    }

    candidate.evidence_source = EvidenceSource::kRuntimeExport;
    candidate.address = export_match.export_address;
    candidate.rva = export_match.export_rva;
    candidate.resolved_symbol = AsWide(export_match.name);
    candidate.discovery_method =
        discovery_method == nullptr ? L"export_lookup" : discovery_method;
    candidate.runtime_export_found = true;
    return candidate;
}

CandidateSource BuildWrapperCandidate(
    const ExportMatch& wrapper,
    const WrapperForward& forward,
    const wchar_t* discovery_method) {
    CandidateSource candidate = {};
    if (!wrapper.found) {
        return candidate;
    }

    candidate.evidence_source = EvidenceSource::kWrapperValidation;
    candidate.address = forward.resolved ? forward.target_address : wrapper.export_address;
    candidate.rva = static_cast<std::uint32_t>(
        candidate.address - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
    candidate.resolved_symbol = AsWide(wrapper.name);
    candidate.discovery_method =
        discovery_method == nullptr ? L"wrapper_lookup" : discovery_method;
    candidate.wrapper_candidate_found = true;
    return candidate;
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
    TargetResult target = {L"CInvSlot::HandleRButtonUp"};
    ExportMatch mangled = {};
    ExportMatch wrapper = {};
    FindExportByName(image, kHandleRButtonUpMangled, &mangled);
    FindExportByName(image, kHandleRButtonUpWrapper, &wrapper);

    WrapperForward wrapper_forward = {};
    if (wrapper.found) {
        wrapper_forward = AnalyzeWrapperForward(image, wrapper);
    } else {
        wrapper_forward.evidence = L"wrapper export missing";
    }

    CandidateSource candidate =
        mangled.found
        ? BuildRuntimeExportCandidate(mangled, L"export_or_wrapper")
        : BuildWrapperCandidate(wrapper, wrapper_forward, L"wrapper_or_cleanroom");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const auto* code = reinterpret_cast<const std::uint8_t*>(candidate.address);
    const bool plausible_prologue =
        candidate_executable && HasPlausibleX86Prologue(code, 8);
    const bool wrapper_matches =
        mangled.found &&
        wrapper_forward.resolved &&
        wrapper_forward.target_address == mangled.export_address;
    const bool wrapper_validation_passed =
        !mangled.found
        ? wrapper_forward.resolved
        : wrapper_matches;
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        mangled.found,
        false,
        wrapper.found,
        candidate_executable,
        plausible_prologue,
        true,
        wrapper_validation_passed,
        false,
        false,
        false,
    });

    std::wstring evidence = L"mangled_export=";
    evidence += mangled.found ? L"yes" : L"no";
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" prologue=";
    evidence += plausible_prologue ? L"plausible" : L"unsupported";
    evidence += L" wrapper=";
    evidence += wrapper.found ? L"yes" : L"no";
    evidence += L" wrapper_evidence=";
    evidence += wrapper_forward.evidence;
    evidence += L" wrapper_matches=";
    evidence += wrapper_matches ? L"yes" : L"no";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        mangled.found
            ? L"validated by runtime export plus wrapper forwarding evidence"
            : L"validated by wrapper forwarding cleanroom evidence",
        mangled.found
            ? L"runtime export candidate did not pass wrapper validation"
            : L"wrapper-export candidate did not resolve to a validated cleanroom target",
        &target);
    return target;
}

TargetResult DiscoverGetSpellLevelNeeded(const ImageView& image) {
    TargetResult target = {L"GetSpellLevelNeeded"};
    ExportMatch mangled = {};
    ExportMatch wrapper = {};
    FindExportByName(image, kGetSpellLevelNeededMangled, &mangled);
    FindExportByName(image, kGetSpellLevelNeededWrapper, &wrapper);

    WrapperForward wrapper_forward = {};
    if (wrapper.found) {
        wrapper_forward = AnalyzeWrapperForward(image, wrapper);
    } else {
        wrapper_forward.evidence = L"wrapper export missing";
    }

    CandidateSource candidate =
        mangled.found
        ? BuildRuntimeExportCandidate(mangled, L"export_or_wrapper")
        : BuildWrapperCandidate(wrapper, wrapper_forward, L"wrapper_or_cleanroom");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const auto* code = reinterpret_cast<const std::uint8_t*>(candidate.address);
    const bool plausible_prologue =
        candidate_executable && HasPlausibleX86Prologue(code, 8);
    const std::uint8_t* error_string = FindAsciiString(
        image,
        kGetSpellLevelNeededErrorString,
        sizeof(kGetSpellLevelNeededErrorString));
    const bool string_found = error_string != nullptr;
    const bool string_xref = string_found && HasImmediateReference(
        image,
        candidate.address,
        kStringReferenceScanBytes,
        reinterpret_cast<std::uintptr_t>(error_string));
    const bool wrapper_matches =
        mangled.found &&
        wrapper_forward.resolved &&
        wrapper_forward.target_address == mangled.export_address;
    const bool wrapper_validation_passed =
        !mangled.found
        ? wrapper_forward.resolved
        : (!wrapper.found || wrapper_matches);
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        mangled.found,
        false,
        wrapper.found,
        candidate_executable,
        plausible_prologue,
        false,
        wrapper_validation_passed,
        true,
        string_found,
        string_xref,
    });

    std::wstring evidence = L"mangled_export=";
    evidence += mangled.found ? L"yes" : L"no";
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" prologue=";
    evidence += plausible_prologue ? L"plausible" : L"unsupported";
    evidence += L" wrapper=";
    evidence += wrapper.found ? L"yes" : L"no";
    evidence += L" wrapper_evidence=";
    evidence += wrapper_forward.evidence;
    evidence += L" wrapper_matches=";
    evidence += wrapper_matches ? L"yes" : L"no";
    evidence += L" diagnostic_string=";
    evidence += string_found ? L"yes" : L"no";
    evidence += L" diagnostic_xref=";
    evidence += string_xref ? L"yes" : L"no";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        mangled.found
            ? L"validated by runtime export plus diagnostic string evidence"
            : L"validated by wrapper forwarding cleanroom evidence plus diagnostic string validation",
        L"GetSpellLevelNeeded candidate did not satisfy wrapper/string validation",
        &target);
    return target;
}

TargetResult DiscoverCanStartMemming(const ImageView& image) {
    TargetResult target = {L"CanStartMemming"};
    ExportMatch mangled = {};
    ExportMatch wrapper = {};
    FindExportByName(image, kCanStartMemmingMangled, &mangled);
    FindExportByName(image, kCanStartMemmingWrapper, &wrapper);

    WrapperForward wrapper_forward = {};
    if (wrapper.found) {
        wrapper_forward = AnalyzeWrapperForward(image, wrapper);
    } else {
        wrapper_forward.evidence = L"wrapper export missing";
    }

    CandidateSource candidate =
        mangled.found
        ? BuildRuntimeExportCandidate(mangled, L"export_or_wrapper")
        : BuildWrapperCandidate(wrapper, wrapper_forward, L"wrapper_or_cleanroom");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const auto* code = reinterpret_cast<const std::uint8_t*>(candidate.address);
    const bool plausible_prologue =
        candidate_executable && HasPlausibleX86Prologue(code, 8);
    const bool wrapper_matches =
        mangled.found &&
        wrapper_forward.resolved &&
        wrapper_forward.target_address == mangled.export_address;
    const bool wrapper_validation_passed =
        !mangled.found
        ? wrapper_forward.resolved
        : wrapper_matches;
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        mangled.found,
        false,
        wrapper.found,
        candidate_executable,
        plausible_prologue,
        true,
        wrapper_validation_passed,
        false,
        false,
        false,
    });

    std::wstring evidence = L"mangled_export=";
    evidence += mangled.found ? L"yes" : L"no";
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" prologue=";
    evidence += plausible_prologue ? L"plausible" : L"unsupported";
    evidence += L" wrapper=";
    evidence += wrapper.found ? L"yes" : L"no";
    evidence += L" wrapper_evidence=";
    evidence += wrapper_forward.evidence;
    evidence += L" wrapper_matches=";
    evidence += wrapper_matches ? L"yes" : L"no";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        mangled.found
            ? L"validated by runtime export plus wrapper forwarding evidence"
            : L"validated by wrapper forwarding cleanroom evidence",
        mangled.found
            ? L"runtime export candidate did not pass wrapper validation"
            : L"wrapper-export candidate did not resolve to a validated cleanroom target",
        &target);
    return target;
}

TargetResult DiscoverGetUsableClasses(const ImageView& image) {
    TargetResult target = {L"GetUsableClasses"};
    ExportMatch wrapper = {};
    FindExportByName(image, kGetUsableClassesWrapper, &wrapper);
    WrapperForward wrapper_forward = {};
    if (wrapper.found) {
        wrapper_forward = AnalyzeWrapperForward(image, wrapper);
    } else {
        wrapper_forward.evidence = L"wrapper export missing";
    }

    CandidateSource candidate =
        BuildWrapperCandidate(wrapper, wrapper_forward, L"wrapper_or_cleanroom");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const auto* code = reinterpret_cast<const std::uint8_t*>(candidate.address);
    const bool plausible_prologue =
        candidate_executable && HasPlausibleX86Prologue(code, 8);
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        false,
        false,
        wrapper.found,
        candidate_executable,
        plausible_prologue,
        true,
        wrapper_forward.resolved,
        false,
        false,
        false,
    });

    std::wstring evidence = L"wrapper_export=";
    evidence += wrapper.found ? L"yes" : L"no";
    evidence += L" wrapper_evidence=";
    evidence += wrapper_forward.evidence;
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" prologue=";
    evidence += plausible_prologue ? L"plausible" : L"unsupported";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by wrapper forwarding cleanroom evidence",
        L"GetUsableClasses wrapper candidate did not resolve to a validated cleanroom target",
        &target);
    return target;
}

TargetResult DiscoverCanEquip(const ImageView& image) {
    TargetResult target = {L"CanEquip"};
    ExportMatch mangled = {};
    ExportMatch wrapper = {};
    FindExportByName(image, kCanEquipMangled, &mangled);
    FindExportByName(image, kCanEquipWrapper, &wrapper);

    WrapperForward wrapper_forward = {};
    if (wrapper.found) {
        wrapper_forward = AnalyzeWrapperForward(image, wrapper);
    } else {
        wrapper_forward.evidence = L"wrapper export missing";
    }

    CandidateSource candidate =
        mangled.found
        ? BuildRuntimeExportCandidate(mangled, L"export_or_wrapper")
        : BuildWrapperCandidate(wrapper, wrapper_forward, L"wrapper_or_cleanroom");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const auto* code = reinterpret_cast<const std::uint8_t*>(candidate.address);
    const bool plausible_prologue =
        candidate_executable && HasPlausibleX86Prologue(code, 8);
    const bool wrapper_matches =
        mangled.found &&
        wrapper_forward.resolved &&
        wrapper_forward.target_address == mangled.export_address;
    const bool wrapper_validation_passed =
        !mangled.found
        ? wrapper_forward.resolved
        : wrapper_matches;
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        mangled.found,
        false,
        wrapper.found,
        candidate_executable,
        plausible_prologue,
        true,
        wrapper_validation_passed,
        false,
        false,
        false,
    });
    std::wstring evidence = L"mangled_export=";
    evidence += mangled.found ? L"yes" : L"no";
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" prologue=";
    evidence += plausible_prologue ? L"plausible" : L"unsupported";
    evidence += L" wrapper=";
    evidence += wrapper.found ? L"yes" : L"no";
    evidence += L" wrapper_evidence=";
    evidence += wrapper_forward.evidence;
    evidence += L" wrapper_matches=";
    evidence += wrapper_matches ? L"yes" : L"no";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        mangled.found
            ? L"validated by runtime export plus wrapper forwarding evidence"
            : L"validated by wrapper forwarding cleanroom evidence",
        mangled.found
            ? L"runtime export candidate did not pass wrapper validation"
            : L"wrapper-export candidate did not resolve to a validated cleanroom target",
        &target);
    return target;
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

    g_result.reason = L"attempted spell usability discovery using runtime exports and cleanroom wrapper evidence";
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
        message += L" state=";
        message += TargetStateName(target->state);
        message += L" evidence_source=";
        message += target->evidence_source;
        message += L" validation=";
        message += target->validation;
        message += L" failure_reason=";
        message += target->failure_reason;
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
