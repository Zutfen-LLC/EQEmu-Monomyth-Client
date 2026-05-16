#include "receive_dispatch_discovery.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "logger.h"

namespace monomyth::receive_dispatch_discovery {
namespace {

constexpr std::uint32_t kExpectedImageBase = 0x00400000;
constexpr std::uint32_t kCandidateRva = 0x000c3250;
constexpr std::uint32_t kFeederCallRvaA = 0x0006240d;
constexpr std::uint32_t kFeederCallRvaB = 0x001427a7;
constexpr std::size_t kRetScanBytes = 0x5000;
constexpr std::size_t kLocalXrefScanBytes = 0x7000;
constexpr std::size_t kDispatchScanBytes = 0x1000;
constexpr char kUnknownMessageString[] = "Received unknown message #%i (0x%x) (%s)!";

Result g_result = {};

struct ImageView {
    const std::uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD section_count = 0;
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
    if (kCandidateRva >= section_end) {
        return false;
    }

    const std::size_t available = section_end - kCandidateRva;
    const std::size_t scan_size = std::min(kRetScanBytes, available);
    const std::uint8_t pattern[] = {0xc2, 0x10, 0x00};
    return SearchBytes(image.base + kCandidateRva, scan_size, pattern, sizeof(pattern)) != nullptr;
}

bool HasLocalXrefTo(
    const ImageView& image,
    const IMAGE_SECTION_HEADER& code_section,
    const std::uint8_t* target) noexcept {
    const std::uint32_t section_end = code_section.VirtualAddress + SectionSpan(code_section);
    if (kCandidateRva >= section_end) {
        return false;
    }

    const std::size_t available = section_end - kCandidateRva;
    const std::size_t scan_size = std::min(kLocalXrefScanBytes, available);
    const std::uint32_t target_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(target));
    const auto* needle = reinterpret_cast<const std::uint8_t*>(&target_address);
    return SearchBytes(image.base + kCandidateRva, scan_size, needle, sizeof(target_address)) != nullptr;
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
    const IMAGE_SECTION_HEADER& code_section) noexcept {
    const std::uint32_t section_end = code_section.VirtualAddress + SectionSpan(code_section);
    if (kCandidateRva >= section_end) {
        return false;
    }

    const std::size_t available = section_end - kCandidateRva;
    const std::size_t scan_size = std::min(kDispatchScanBytes, available);
    const std::uint8_t* code = image.base + kCandidateRva;
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

Result MakeFailure(const wchar_t* reason) {
    Result result = {};
    result.state = State::kFailed;
    result.reason = reason;
    return result;
}

Result ValidateCandidate() noexcept {
    ImageView image = {};
    if (!BuildImageView(&image)) {
        return MakeFailure(L"host PE image unavailable");
    }

    if (image.nt->OptionalHeader.ImageBase != kExpectedImageBase) {
        return MakeFailure(L"unexpected ROF2 image base");
    }

    const auto* code_section = FindSection(image, kCandidateRva, 1, IMAGE_SCN_MEM_EXECUTE);
    if (code_section == nullptr) {
        return MakeFailure(L"candidate RVA is not in executable image section");
    }

    Result result = {};
    result.state = State::kCandidateFound;

    const std::uintptr_t candidate_address =
        reinterpret_cast<std::uintptr_t>(image.base + kCandidateRva);

    if (!HasRet10Epilogue(image, *code_section)) {
        return MakeFailure(L"candidate missing ret 0x10 epilogue evidence");
    }

    const std::uint8_t* unknown_message = FindUnknownMessageString(image);
    if (unknown_message == nullptr) {
        return MakeFailure(L"unknown-message string not found in readable image section");
    }

    if (!HasLocalXrefTo(image, *code_section, unknown_message)) {
        return MakeFailure(L"candidate missing local xref to unknown-message string");
    }

    if (!HasDispatchLikeLocalShape(image, *code_section)) {
        return MakeFailure(L"candidate missing local dispatch-like compare/branch evidence");
    }

    if (!ValidateDirectCall(image, kFeederCallRvaA, candidate_address) ||
        !ValidateDirectCall(image, kFeederCallRvaB, candidate_address)) {
        return MakeFailure(L"known direct feeder callsites do not target candidate");
    }

    result.state = State::kValidated;
    result.validated = true;
    result.candidate_rva = kCandidateRva;
    result.candidate_address = candidate_address;
    result.reason =
        L"validated static ROF2 dispatcher structure: rva, ret10, unknown-string-xref, dispatch shape, feeder calls";
    return result;
}

}  // namespace

void Initialize() noexcept {
    g_result = {};
    g_result.state = State::kUnavailable;
    g_result.reason = L"initialized";
}

Result Run(bool enhancement_discovery_allowed) noexcept {
    if (!enhancement_discovery_allowed) {
        g_result = {};
        g_result.state = State::kSkippedByCapability;
        g_result.reason = L"skipped because capability manifest denied enhancement discovery";
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
    g_result.candidate_rva = 0;
    g_result.candidate_address = 0;
    g_result.reason = L"shutdown";
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
    if (result.validated) {
        message += L" candidate_rva=";
        message += Hex32(result.candidate_rva);
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
    monomyth::logger::Log(message);
}

}  // namespace monomyth::receive_dispatch_discovery
