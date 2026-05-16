#include "fingerprint.h"

#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <vector>

namespace monomyth::fingerprint {
namespace {

constexpr wchar_t kExpectedProcessName[] = L"eqgame.exe";
constexpr wchar_t kExpectedDate[] = L"May 10 2013";
constexpr wchar_t kExpectedTime[] = L"23:30:08";
constexpr std::string_view kExpectedDateAscii = "May 10 2013";
constexpr std::string_view kExpectedTimeAscii = "23:30:08";
constexpr LONGLONG kMaxExecutableScanBytes = 512LL * 1024LL * 1024LL;

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring GetProcessPath() noexcept {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    return std::wstring(path, path + length);
}

std::wstring Basename(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }

    return path.substr(slash + 1);
}

bool ReadVersionStrings(const std::wstring& path, std::vector<std::wstring>* strings) noexcept {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return false;
    }

    std::vector<std::byte> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) {
        return false;
    }

    struct LangAndCodePage {
        WORD language;
        WORD code_page;
    };

    LangAndCodePage* translations = nullptr;
    UINT translation_bytes = 0;
    if (!VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translations), &translation_bytes) ||
        translations == nullptr ||
        translation_bytes < sizeof(LangAndCodePage)) {
        return false;
    }

    static constexpr const wchar_t* kKeys[] = {
        L"Comments",
        L"FileDescription",
        L"FileVersion",
        L"InternalName",
        L"OriginalFilename",
        L"ProductName",
        L"ProductVersion"
    };

    for (UINT i = 0; i < translation_bytes / sizeof(LangAndCodePage); ++i) {
        for (const auto* key : kKeys) {
            wchar_t query[128] = {};
            wsprintfW(
                query,
                L"\\StringFileInfo\\%04x%04x\\%s",
                translations[i].language,
                translations[i].code_page,
                key);

            wchar_t* value = nullptr;
            UINT value_chars = 0;
            if (VerQueryValueW(buffer.data(), query, reinterpret_cast<LPVOID*>(&value), &value_chars) &&
                value != nullptr &&
                value_chars > 1) {
                strings->emplace_back(value, value + value_chars - 1);
            }
        }
    }

    return !strings->empty();
}

bool ContainsValue(const std::vector<std::wstring>& strings, const std::wstring& needle) {
    const std::wstring needle_lower = ToLower(needle);
    for (const auto& value : strings) {
        if (ToLower(value).find(needle_lower) != std::wstring::npos) {
            return true;
        }
    }

    return false;
}

bool ReadFileBytes(const std::wstring& path, std::vector<std::byte>* bytes) noexcept {
    if (bytes == nullptr || path.empty()) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > kMaxExecutableScanBytes) {
        CloseHandle(file);
        return false;
    }

    bytes->resize(static_cast<std::size_t>(size.QuadPart));
    DWORD total_read = 0;
    while (total_read < bytes->size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            bytes->size() - total_read,
            static_cast<std::size_t>(0x7ffff000)));
        DWORD read = 0;
        if (!ReadFile(
                file,
                bytes->data() + total_read,
                chunk,
                &read,
                nullptr) ||
            read == 0) {
            CloseHandle(file);
            bytes->clear();
            return false;
        }
        total_read += read;
    }

    CloseHandle(file);
    return total_read == bytes->size();
}

std::wstring ByteScanFailureReason(
    bool version_strings_checked,
    const MarkerPresence& markers) {
    std::wstring reason = version_strings_checked
        ? L"version resources inconclusive; byte scan failed"
        : L"version resources unavailable; byte scan failed";
    reason += L" date_found=";
    reason += markers.date_found ? L"true" : L"false";
    reason += L" time_found=";
    reason += markers.time_found ? L"true" : L"false";
    return reason;
}

}  // namespace

MarkerPresence FindKnownRof2Markers(std::string_view bytes) noexcept {
    MarkerPresence presence = {};
    presence.date_found = bytes.find(kExpectedDateAscii) != std::string_view::npos;
    presence.time_found = bytes.find(kExpectedTimeAscii) != std::string_view::npos;
    return presence;
}

const wchar_t* MethodName(Method method) noexcept {
    switch (method) {
    case Method::kVersionResource:
        return L"version_resource";
    case Method::kByteScan:
        return L"byte_scan";
    case Method::kUnavailable:
    default:
        return L"unavailable";
    }
}

Result Evaluate() noexcept {
    Result result = {};
    result.reason = L"unknown";

    const std::wstring process_path = GetProcessPath();
    if (process_path.empty()) {
        result.reason = L"process path unavailable";
        return result;
    }

    const std::wstring process_name = ToLower(Basename(process_path));
    result.process_name_match = (process_name == kExpectedProcessName);
    if (!result.process_name_match) {
        result.reason = L"process name mismatch";
        return result;
    }

    std::vector<std::wstring> version_strings;
    result.version_strings_checked = ReadVersionStrings(process_path, &version_strings);
    if (result.version_strings_checked) {
        const bool date_match = ContainsValue(version_strings, kExpectedDate);
        const bool time_match = ContainsValue(version_strings, kExpectedTime);
        result.version_strings_match = date_match && time_match;
        if (result.version_strings_match) {
            result.matched = true;
            result.method = Method::kVersionResource;
            result.file_hash_placeholder = false;
            result.text_hash_placeholder = false;
            result.prepatch_placeholder = false;
            result.hooks_allowed = true;
            result.reason = L"version resources matched both ROF2 markers";
            return result;
        }
    }

    std::vector<std::byte> file_bytes;
    result.byte_scan_checked = ReadFileBytes(process_path, &file_bytes);
    if (!result.byte_scan_checked) {
        result.reason = result.version_strings_checked
            ? L"version resources inconclusive; byte scan unreadable"
            : L"version resources unavailable; byte scan unreadable";
        return result;
    }

    const std::string_view bytes_view(
        reinterpret_cast<const char*>(file_bytes.data()),
        file_bytes.size());
    const MarkerPresence markers = FindKnownRof2Markers(bytes_view);
    result.byte_scan_match = markers.date_found && markers.time_found;
    if (!result.byte_scan_match) {
        result.reason = ByteScanFailureReason(result.version_strings_checked, markers);
        return result;
    }

    result.matched = true;
    result.method = Method::kByteScan;
    result.file_hash_placeholder = false;
    result.text_hash_placeholder = false;
    result.prepatch_placeholder = false;
    result.hooks_allowed = true;
    result.reason = result.version_strings_checked
        ? L"version resources inconclusive; byte scan matched both ROF2 markers"
        : L"version resources unavailable; byte scan matched both ROF2 markers";
    return result;
}

}  // namespace monomyth::fingerprint
