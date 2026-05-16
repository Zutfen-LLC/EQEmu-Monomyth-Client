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

}  // namespace

Result Evaluate() noexcept {
    Result result = {};
    result.reason = L"unknown";

    const std::wstring process_path = GetProcessPath();
    const std::wstring process_name = ToLower(Basename(process_path));
    result.process_name_match = (process_name == kExpectedProcessName);
    if (!result.process_name_match) {
        result.reason = L"process name mismatch";
        return result;
    }

    std::vector<std::wstring> version_strings;
    result.version_strings_checked = ReadVersionStrings(process_path, &version_strings);
    if (!result.version_strings_checked) {
        result.reason = L"version resources unavailable";
        return result;
    }

    const bool date_match = ContainsValue(version_strings, kExpectedDate);
    const bool time_match = ContainsValue(version_strings, kExpectedTime);
    result.version_strings_match = date_match && time_match;
    if (!result.version_strings_match) {
        result.reason = L"ROF2 version string mismatch";
        return result;
    }

    result.file_hash_placeholder = false;
    result.text_hash_placeholder = false;
    result.prepatch_placeholder = false;
    result.hooks_allowed = true;
    result.reason = L"process and version strings matched; hash checks pending future implementation";
    return result;
}

}  // namespace monomyth::fingerprint
