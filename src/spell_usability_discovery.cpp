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

namespace monomyth::spell_usability_discovery {
namespace {

constexpr char kGetSpellLevelNeededMangled[] =
    "?GetSpellLevelNeeded@EQ_Spell@EQClasses@@QBEEI@Z";
constexpr char kGetSpellLevelNeededWrapper[] = "EQ_Spell__GetSpellLevelNeeded";
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

bool PopulateBaseTargetFields(
    const ImageView& image,
    const ExportMatch& mangled,
    TargetResult* target) {
    if (target == nullptr) {
        return false;
    }

    if (!mangled.found) {
        target->state = TargetState::kFailed;
        target->discovery_method = L"export_lookup";
        target->reason = L"exact mangled export not found in eqgame.exe export table";
        target->validation_evidence = L"mangled_export=no";
        return false;
    }

    target->module_base = reinterpret_cast<std::uintptr_t>(image.base);
    target->candidate_rva = mangled.export_rva;
    target->candidate_address = mangled.export_address;
    target->resolved_symbol = AsWide(mangled.name);
    target->discovery_method = L"export_mangled";

    if (!IsExecutableAddress(image, mangled.export_address)) {
        target->state = TargetState::kFailed;
        target->reason = L"exact mangled export resolved outside an executable image section";
        target->validation_evidence = L"mangled_export=yes executable=no";
        return false;
    }

    const auto* code = reinterpret_cast<const std::uint8_t*>(mangled.export_address);
    if (!HasPlausibleX86Prologue(code, 8)) {
        target->state = TargetState::kFoundUnvalidated;
        target->reason = L"exact mangled export resolved but entry prologue shape is not confidently recognized";
        target->validation_evidence = L"mangled_export=yes executable=yes prologue=unsupported";
        return false;
    }

    target->state = TargetState::kFoundUnvalidated;
    target->reason = L"exact mangled export resolved but target-specific validation is incomplete";
    target->validation_evidence = L"mangled_export=yes executable=yes prologue=plausible";
    return true;
}

TargetResult DiscoverGetSpellLevelNeeded(const ImageView& image) {
    TargetResult target = {L"GetSpellLevelNeeded"};
    ExportMatch mangled = {};
    ExportMatch wrapper = {};
    FindExportByName(image, kGetSpellLevelNeededMangled, &mangled);
    FindExportByName(image, kGetSpellLevelNeededWrapper, &wrapper);

    if (!PopulateBaseTargetFields(image, mangled, &target)) {
        return target;
    }

    WrapperForward wrapper_forward = {};
    if (wrapper.found) {
        wrapper_forward = AnalyzeWrapperForward(image, wrapper);
    } else {
        wrapper_forward.evidence = L"wrapper export missing";
    }

    const std::uint8_t* error_string = FindAsciiString(
        image,
        kGetSpellLevelNeededErrorString,
        sizeof(kGetSpellLevelNeededErrorString));
    const bool string_found = error_string != nullptr;
    const bool string_xref = string_found && HasImmediateReference(
        image,
        target.candidate_address,
        kStringReferenceScanBytes,
        reinterpret_cast<std::uintptr_t>(error_string));
    const bool wrapper_matches =
        wrapper_forward.resolved && wrapper_forward.target_address == target.candidate_address;

    std::wstring evidence = L"mangled_export=yes executable=yes prologue=plausible";
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
    target.validation_evidence = evidence;

    if (wrapper_matches && string_xref) {
        target.state = TargetState::kValidated;
        target.exact_signature_validated = true;
        target.trace_safe = true;
        target.reason =
            L"validated by exact mangled export, wrapper forward match, and diagnostic string reference";
        return target;
    }

    if (!wrapper.found && string_xref) {
        target.state = TargetState::kValidated;
        target.exact_signature_validated = true;
        target.trace_safe = true;
        target.reason =
            L"validated by exact mangled export and diagnostic string reference";
        return target;
    }

    if (!wrapper_matches && string_xref) {
        target.reason =
            L"diagnostic string reference matched but wrapper forwarding evidence did not confirm the exact target";
        return target;
    }

    if (wrapper_matches && !string_xref) {
        target.reason =
            L"wrapper forwarding matched exact export but diagnostic string reference was not found near the candidate";
        return target;
    }

    target.reason =
        L"exact mangled export resolved but wrapper forwarding and diagnostic string evidence were insufficient";
    return target;
}

TargetResult DiscoverCanStartMemming(const ImageView& image) {
    TargetResult target = {L"CanStartMemming"};
    ExportMatch mangled = {};
    ExportMatch wrapper = {};
    FindExportByName(image, kCanStartMemmingMangled, &mangled);
    FindExportByName(image, kCanStartMemmingWrapper, &wrapper);

    if (!PopulateBaseTargetFields(image, mangled, &target)) {
        return target;
    }

    WrapperForward wrapper_forward = {};
    if (wrapper.found) {
        wrapper_forward = AnalyzeWrapperForward(image, wrapper);
    } else {
        wrapper_forward.evidence = L"wrapper export missing";
    }

    const bool wrapper_matches =
        wrapper_forward.resolved && wrapper_forward.target_address == target.candidate_address;

    std::wstring evidence = L"mangled_export=yes executable=yes prologue=plausible";
    evidence += L" wrapper=";
    evidence += wrapper.found ? L"yes" : L"no";
    evidence += L" wrapper_evidence=";
    evidence += wrapper_forward.evidence;
    evidence += L" wrapper_matches=";
    evidence += wrapper_matches ? L"yes" : L"no";
    target.validation_evidence = evidence;

    if (wrapper_matches) {
        target.state = TargetState::kValidated;
        target.exact_signature_validated = true;
        target.trace_safe = true;
        target.reason = L"validated by exact mangled export and wrapper forward match";
        return target;
    }

    target.reason =
        L"exact mangled export resolved but wrapper forwarding evidence did not confirm the exact target";
    return target;
}

}  // namespace

void Initialize() noexcept {
    g_result = {};
    g_result.reason = L"initialized";
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.can_start_memming = {L"CanStartMemming"};
}

Result Run(bool discovery_allowed) noexcept {
    g_result = {};
    g_result.allowed = discovery_allowed;
    g_result.trace_dev_opt_in = IsTraceDevOptInPresent();
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.can_start_memming = {L"CanStartMemming"};

    if (!discovery_allowed) {
        g_result.reason =
            L"skipped because capability manifest denied spell usability discovery";
        g_result.get_spell_level_needed.reason = L"discovery skipped by capability";
        g_result.get_spell_level_needed.validation_evidence = L"capability_gate=denied";
        g_result.can_start_memming.reason = L"discovery skipped by capability";
        g_result.can_start_memming.validation_evidence = L"capability_gate=denied";
        return g_result;
    }

    ImageView image = {};
    if (!BuildImageView(&image)) {
        g_result.reason = L"failed because host PE image unavailable";
        g_result.get_spell_level_needed.state = TargetState::kFailed;
        g_result.get_spell_level_needed.discovery_method = L"export_lookup";
        g_result.get_spell_level_needed.reason = L"host PE image unavailable";
        g_result.get_spell_level_needed.validation_evidence = L"image_view=no";
        g_result.can_start_memming.state = TargetState::kFailed;
        g_result.can_start_memming.discovery_method = L"export_lookup";
        g_result.can_start_memming.reason = L"host PE image unavailable";
        g_result.can_start_memming.validation_evidence = L"image_view=no";
        return g_result;
    }

    g_result.reason = L"attempted export-based spell usability discovery";
    g_result.get_spell_level_needed = DiscoverGetSpellLevelNeeded(image);
    g_result.can_start_memming = DiscoverCanStartMemming(image);
    return g_result;
}

void Shutdown() noexcept {
    g_result = {};
    g_result.reason = L"shutdown";
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.can_start_memming = {L"CanStartMemming"};
}

Result GetResult() noexcept {
    return g_result;
}

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

void LogResult(const Result& result) noexcept {
    const TargetResult* targets[] = {
        &result.get_spell_level_needed,
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
