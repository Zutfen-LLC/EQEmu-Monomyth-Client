#include "receive_dispatch_discovery.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <sstream>
#include <string>

#include "logger.h"

namespace monomyth::receive_dispatch_discovery {
namespace {

struct DiscoveryRvas {
    static constexpr std::uint32_t kDispatcherEntry = 0x000c3250;
    static constexpr std::uint32_t kCompareTree = 0x000c3317;
    static constexpr std::uint32_t kUnknownMessagePath = 0x000d3f4e;
    static constexpr std::uint32_t kEpilogue = 0x000d40c7;
    static constexpr std::uint32_t kFeederCallA = 0x0006240d;
    static constexpr std::uint32_t kFeederCallB = 0x001427a7;
};

constexpr std::size_t kEntryDispatchScanBytes = 0x1000;
constexpr std::size_t kCompareTreeScanBytes = 0x400;
constexpr std::size_t kUnknownMessageEvidenceScanBytes = 0x300;
constexpr std::size_t kEpilogueScanBytes = 0x40;
constexpr char kUnknownMessageString[] = "Received unknown message #%i (0x%x) (%s)!";

Result g_result = {};

struct ImageView {
    const std::uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD section_count = 0;
    std::uint32_t image_size = 0;
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

enum class CheckDisposition {
    kPass,
    kFail,
    kAdvisoryPass,
    kAdvisoryFail,
    kSkipped,
};

struct CheckStatus {
    const wchar_t* name = L"unknown";
    CheckDisposition disposition = CheckDisposition::kSkipped;
    std::wstring detail = L"not evaluated";
};

const wchar_t* CheckDispositionName(CheckDisposition disposition) noexcept {
    switch (disposition) {
        case CheckDisposition::kPass:
            return L"pass";
        case CheckDisposition::kFail:
            return L"fail";
        case CheckDisposition::kAdvisoryPass:
            return L"advisory_pass";
        case CheckDisposition::kAdvisoryFail:
            return L"advisory_fail";
        case CheckDisposition::kSkipped:
            return L"skipped";
    }

    return L"unknown";
}

void SetCheck(
    CheckStatus* check,
    const wchar_t* name,
    CheckDisposition disposition,
    const wchar_t* detail) {
    if (check == nullptr) {
        return;
    }

    check->name = name;
    check->disposition = disposition;
    check->detail = detail == nullptr ? L"unknown" : detail;
}

void SetCheck(
    CheckStatus* check,
    const wchar_t* name,
    CheckDisposition disposition,
    const std::wstring& detail) {
    SetCheck(check, name, disposition, detail.c_str());
}

std::wstring SummarizeChecks(
    const CheckStatus* checks,
    std::size_t count) {
    std::wstring summary;
    for (std::size_t i = 0; i < count; ++i) {
        if (!summary.empty()) {
            summary += L"; ";
        }

        summary += checks[i].name;
        summary += L"=";
        summary += CheckDispositionName(checks[i].disposition);
        summary += L"(";
        summary += checks[i].detail;
        summary += L")";
    }

    return summary;
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

    view->base = base;
    view->nt = nt;
    view->sections = IMAGE_FIRST_SECTION(nt);
    view->section_count = nt->FileHeader.NumberOfSections;
    view->image_size = nt->OptionalHeader.SizeOfImage;
    return true;
}

bool IsRangeWithinImage(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length) noexcept {
    if (image.image_size == 0 || rva >= image.image_size) {
        return false;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(rva) + length;
    return end <= image.image_size;
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

const std::uint8_t* RvaToPtr(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length,
    DWORD required_characteristics) noexcept {
    if (FindSection(image, rva, length, required_characteristics) == nullptr) {
        return nullptr;
    }

    return image.base + rva;
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

const std::uint8_t* FindUnknownMessageString(const ImageView& image) noexcept {
    const auto* needle = reinterpret_cast<const std::uint8_t*>(kUnknownMessageString);
    const std::size_t needle_size = sizeof(kUnknownMessageString);

    for (WORD i = 0; i < image.section_count; ++i) {
        const IMAGE_SECTION_HEADER& section = image.sections[i];
        const bool readable = (section.Characteristics & IMAGE_SCN_MEM_READ) != 0;
        const bool executable = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (!readable || executable || SectionSpan(section) < needle_size) {
            continue;
        }

        const std::uint8_t* start = image.base + section.VirtualAddress;
        const std::uint32_t span = SectionSpan(section);
        const std::uint8_t* found = SearchBytes(start, span, needle, needle_size);
        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

bool HasRet10Epilogue(const ImageView& image, const IMAGE_SECTION_HEADER& code_section) noexcept {
    const std::uint32_t section_end = code_section.VirtualAddress + SectionSpan(code_section);
    if (DiscoveryRvas::kDispatcherEntry >= section_end) {
        return false;
    }

    if (DiscoveryRvas::kEpilogue < DiscoveryRvas::kDispatcherEntry ||
        DiscoveryRvas::kEpilogue >= section_end) {
        return false;
    }

    const std::size_t available = section_end - DiscoveryRvas::kEpilogue;
    const std::size_t scan_size = std::min(kEpilogueScanBytes, available);
    const std::uint8_t pattern[] = {0xc2, 0x10, 0x00};
    return SearchBytes(
               image.base + DiscoveryRvas::kEpilogue,
               scan_size,
               pattern,
               sizeof(pattern)) != nullptr;
}

bool ValidateDirectCall(
    const ImageView& image,
    std::uint32_t call_rva,
    std::uintptr_t expected_target) noexcept {
    const std::uint8_t* call = RvaToPtr(image, call_rva, 5, IMAGE_SCN_MEM_EXECUTE);
    if (call == nullptr || call[0] != 0xe8) {
        return false;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, call + 1, sizeof(relative));
    const std::uintptr_t next_instruction = reinterpret_cast<std::uintptr_t>(call + 5);
    const std::uintptr_t target = next_instruction + relative;
    return target == expected_target;
}

bool HasDispatchLikeLocalShape(
    const ImageView& image,
    std::uint32_t start_rva,
    std::size_t requested_scan_size) noexcept {
    const IMAGE_SECTION_HEADER* code_section =
        FindSection(image, start_rva, 1, IMAGE_SCN_MEM_EXECUTE);
    if (code_section == nullptr) {
        return false;
    }

    const std::uint32_t section_end = code_section->VirtualAddress + SectionSpan(*code_section);
    if (start_rva >= section_end) {
        return false;
    }

    const std::size_t available = section_end - start_rva;
    const std::size_t scan_size = std::min(requested_scan_size, available);
    const std::uint8_t* code = image.base + start_rva;
    int compare_like = 0;
    int branch_like = 0;

    for (std::size_t i = 0; i < scan_size; ++i) {
        const std::uint8_t byte = code[i];
        if (byte == 0x3d || byte == 0x39 || byte == 0x3b ||
            byte == 0x81 || byte == 0x83 || byte == 0x85) {
            ++compare_like;
        }

        if ((byte >= 0x70 && byte <= 0x7f) ||
            (byte == 0x0f && i + 1 < scan_size && code[i + 1] >= 0x80 && code[i + 1] <= 0x8f)) {
            ++branch_like;
        }
    }

    return compare_like >= 4 && branch_like >= 4;
}

bool HasUnknownMessagePathEvidence(
    const ImageView& image,
    const std::uint8_t* unknown_message) noexcept {
    if (unknown_message == nullptr) {
        return false;
    }

    const std::uint8_t* path = RvaToPtr(
        image,
        DiscoveryRvas::kUnknownMessagePath,
        kUnknownMessageEvidenceScanBytes,
        IMAGE_SCN_MEM_EXECUTE);
    if (path == nullptr) {
        return false;
    }

    const std::uintptr_t string_address = reinterpret_cast<std::uintptr_t>(unknown_message);
    const std::uint32_t string_address_32 = static_cast<std::uint32_t>(string_address);
    const auto* string_address_bytes =
        reinterpret_cast<const std::uint8_t*>(&string_address_32);
    if (SearchBytes(
            path,
            kUnknownMessageEvidenceScanBytes,
            string_address_bytes,
            sizeof(string_address_32)) != nullptr) {
        return true;
    }

    const std::uint32_t path_absolute = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(path));
    const std::uint8_t* path_end = path + kUnknownMessageEvidenceScanBytes;
    for (const std::uint8_t* cursor = path; cursor + 5 <= path_end; ++cursor) {
        if (*cursor != 0xe8) {
            continue;
        }

        std::int32_t relative = 0;
        std::memcpy(&relative, cursor + 1, sizeof(relative));
        const std::uint32_t call_target =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cursor + 5) + relative);
        if (call_target >= path_absolute &&
            call_target < path_absolute + kUnknownMessageEvidenceScanBytes) {
            return true;
        }
    }

    return false;
}

Result MakeFailure(const Result& partial, const wchar_t* reason) {
    Result result = partial;
    result.state = State::kFailed;
    result.validated = false;
    result.reason = reason;
    return result;
}

Result ValidateCandidate() noexcept {
    CheckStatus checks[] = {
        {L"candidate_range"},
        {L"candidate_executable"},
        {L"entry_dispatch_shape"},
        {L"compare_tree_shape"},
        {L"unknown_message_string"},
        {L"unknown_message_path"},
        {L"feeder_calls"},
        {L"ret10_epilogue"},
    };

    ImageView image = {};
    if (!BuildImageView(&image)) {
        Result result = MakeFailure({}, L"host PE image unavailable");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return result;
    }

    Result result = {};
    result.module_base = reinterpret_cast<std::uintptr_t>(image.base);
    result.candidate_rva = DiscoveryRvas::kDispatcherEntry;
    result.candidate_address = result.module_base + DiscoveryRvas::kDispatcherEntry;

    if (!IsRangeWithinImage(image, DiscoveryRvas::kDispatcherEntry, 1)) {
        SetCheck(
            &checks[0],
            L"candidate_range",
            CheckDisposition::kFail,
            L"dispatcher entry RVA resolves outside loaded module image");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return MakeFailure(result, L"candidate RVA resolves outside loaded module image");
    }
    SetCheck(
        &checks[0],
        L"candidate_range",
        CheckDisposition::kPass,
        L"dispatcher entry RVA resolves inside loaded module image");

    const auto* code_section = FindSection(
        image,
        DiscoveryRvas::kDispatcherEntry,
        1,
        IMAGE_SCN_MEM_EXECUTE);
    if (code_section == nullptr) {
        SetCheck(
            &checks[1],
            L"candidate_executable",
            CheckDisposition::kFail,
            L"dispatcher entry RVA is not in an executable image section");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return MakeFailure(result, L"candidate RVA is not in executable image section");
    }
    SetCheck(
        &checks[1],
        L"candidate_executable",
        CheckDisposition::kPass,
        L"dispatcher entry RVA is in an executable image section");

    result.state = State::kCandidateFound;

    if (!HasDispatchLikeLocalShape(
            image,
            DiscoveryRvas::kDispatcherEntry,
            kEntryDispatchScanBytes)) {
        SetCheck(
            &checks[2],
            L"entry_dispatch_shape",
            CheckDisposition::kFail,
            L"entry-adjacent compare/branch evidence missing");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return MakeFailure(
            result,
            L"structural validation failed: candidate missing entry-adjacent dispatch evidence");
    }
    SetCheck(
        &checks[2],
        L"entry_dispatch_shape",
        CheckDisposition::kPass,
        L"entry-adjacent compare/branch evidence present");

    if (!HasDispatchLikeLocalShape(
            image,
            DiscoveryRvas::kCompareTree,
            kCompareTreeScanBytes)) {
        SetCheck(
            &checks[3],
            L"compare_tree_shape",
            CheckDisposition::kFail,
            L"known compare-tree RVA range missing compare/branch evidence");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return MakeFailure(
            result,
            L"structural validation failed: known compare-tree evidence missing near dispatcher");
    }
    SetCheck(
        &checks[3],
        L"compare_tree_shape",
        CheckDisposition::kPass,
        L"known compare-tree RVA range shows compare/branch evidence");

    const std::uint8_t* unknown_message = FindUnknownMessageString(image);
    if (unknown_message == nullptr) {
        SetCheck(
            &checks[4],
            L"unknown_message_string",
            CheckDisposition::kFail,
            L"unknown-message string not found in readable image section");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return MakeFailure(
            result,
            L"structural validation failed: unknown-message string not found in readable image section");
    }
    SetCheck(
        &checks[4],
        L"unknown_message_string",
        CheckDisposition::kPass,
        L"unknown-message string found in readable image section");

    if (!HasUnknownMessagePathEvidence(image, unknown_message)) {
        SetCheck(
            &checks[5],
            L"unknown_message_path",
            CheckDisposition::kFail,
            L"known unknown-message path RVA range missing bounded string/reference evidence");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return MakeFailure(
            result,
            L"structural validation failed: unknown-message path evidence missing near known RVA");
    }
    SetCheck(
        &checks[5],
        L"unknown_message_path",
        CheckDisposition::kPass,
        L"known unknown-message path RVA range contains bounded string/reference evidence");

    if (!ValidateDirectCall(image, DiscoveryRvas::kFeederCallA, result.candidate_address) ||
        !ValidateDirectCall(image, DiscoveryRvas::kFeederCallB, result.candidate_address)) {
        SetCheck(
            &checks[6],
            L"feeder_calls",
            CheckDisposition::kFail,
            L"known feeder callsites do not both target dispatcher candidate");
        result.checks = SummarizeChecks(checks, std::size(checks));
        return MakeFailure(
            result,
            L"structural validation failed: known direct feeder callsites do not target candidate");
    }
    SetCheck(
        &checks[6],
        L"feeder_calls",
        CheckDisposition::kPass,
        L"known feeder callsites both target dispatcher candidate");

    if (HasRet10Epilogue(image, *code_section)) {
        SetCheck(
            &checks[7],
            L"ret10_epilogue",
            CheckDisposition::kAdvisoryPass,
            L"bounded scan near known epilogue RVA found ret 0x10");
    } else {
        SetCheck(
            &checks[7],
            L"ret10_epilogue",
            CheckDisposition::kAdvisoryFail,
            L"bounded scan near known epilogue RVA did not find ret 0x10; dispatcher is large so entry-local epilogue evidence is not required");
    }

    result.state = State::kValidated;
    result.validated = true;
    result.reason =
        L"structural validation succeeded: mandatory range, entry/compare-tree, unknown-message path, and feeder checks passed";
    result.checks = SummarizeChecks(checks, std::size(checks));
    return result;
}

}  // namespace

void Initialize() noexcept {
    g_result = {};
    g_result.state = State::kUnavailable;
    g_result.reason = L"initialized";
    g_result.checks = L"initialized";
}

Result Run(bool enhancement_discovery_allowed) noexcept {
    if (!enhancement_discovery_allowed) {
        g_result = {};
        g_result.state = State::kSkippedByCapability;
        g_result.reason = L"skipped because capability manifest denied enhancement discovery";
        g_result.checks = L"candidate_range=skipped(capability gate denied discovery)";
        return g_result;
    }

    g_result = ValidateCandidate();
    return g_result;
}

void Shutdown() noexcept {
    if (g_result.state == State::kUnavailable || g_result.state == State::kShutdown) {
        return;
    }

    g_result.state = State::kShutdown;
    g_result.validated = false;
    g_result.module_base = 0;
    g_result.candidate_rva = 0;
    g_result.candidate_address = 0;
    g_result.reason = L"shutdown";
    g_result.checks = L"shutdown";
}

Result GetResult() noexcept {
    return g_result;
}

const wchar_t* StateName(State state) noexcept {
    switch (state) {
        case State::kUnavailable:
            return L"unavailable";
        case State::kSkippedByCapability:
            return L"skipped_by_capability";
        case State::kCandidateFound:
            return L"candidate_found";
        case State::kValidated:
            return L"validated";
        case State::kFailed:
            return L"failed";
        case State::kShutdown:
            return L"shutdown";
    }

    return L"unknown";
}

void LogResult(const Result& result) noexcept {
    std::wstring message = L"ReceiveDispatchDiscovery state=";
    message += StateName(result.state);
    if (result.module_base != 0) {
        message += L" module_base=";
        message += HexPtr(result.module_base);
    }
    if (result.candidate_rva != 0) {
        message += L" candidate_rva=";
        message += Hex32(result.candidate_rva);
    }
    if (result.candidate_address != 0) {
        message += L" candidate_address=";
        message += HexPtr(result.candidate_address);
    }

    message += L" reason=\"";
    if (result.reason.empty()) {
        message += L"unknown";
    } else {
        message += result.reason;
    }
    message += L"\"";
    message += L" checks=\"";
    if (result.checks.empty()) {
        message += L"unknown";
    } else {
        message += result.checks;
    }
    message += L"\"";
    monomyth::logger::Log(message);
}

}  // namespace monomyth::receive_dispatch_discovery
