#include "multiclass_cache.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace monomyth::multiclass_cache {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr wchar_t kResourcesDirectoryName[] = L"Resources";
constexpr wchar_t kCacheFileName[] = L"monomyth-multiclass-cache.txt";
constexpr wchar_t kServerConfigFileName[] = L"eqlsPlayerData.ini";
constexpr char kCacheHeaderV1[] = "MONOMYTH_MULTICLASS_CACHE_V1";
constexpr char kCacheHeaderV2[] = "MONOMYTH_MULTICLASS_CACHE_V2";
constexpr char kCacheHeaderV3[] = "MONOMYTH_MULTICLASS_CACHE_V3";
constexpr char kLoginServerSection[] = "MISC";
constexpr char kLastServerNameKey[] = "LastServerName";
constexpr DWORD kInvalidFileSize = 0xffffffffu;
constexpr std::size_t kMaxCacheFileBytes = 64 * 1024;

struct CacheEntry {
    std::string name;
    std::string normalized_name;
    std::uint32_t classes_bitmask = 0;
    std::uint8_t native_class_id = 0;
    std::string server_name;
    std::string normalized_server_name;
};

std::mutex g_cache_mutex;

std::string NormalizeName(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        if (ch == '\0') {
            break;
        }

        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

std::wstring BuildResourcesDirectoryPath() noexcept {
    wchar_t module_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(&__ImageBase),
        module_path,
        MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring path(module_path, module_path + length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"";
    }

    path.resize(slash + 1);
    path += kResourcesDirectoryName;
    return path;
}

std::wstring BuildClientRootPath() noexcept {
    wchar_t module_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(&__ImageBase),
        module_path,
        MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring path(module_path, module_path + length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"";
    }

    path.resize(slash);
    return path;
}

std::wstring BuildCacheFilePath() noexcept {
    std::wstring path = BuildResourcesDirectoryPath();
    if (path.empty()) {
        return L"";
    }

    path += L"\\";
    path += kCacheFileName;
    return path;
}

std::wstring BuildServerConfigPath() noexcept {
    std::wstring path = BuildClientRootPath();
    if (path.empty()) {
        return L"";
    }

    path += L"\\";
    path += kServerConfigFileName;
    return path;
}

bool EnsureResourcesDirectory() noexcept {
    const std::wstring path = BuildResourcesDirectoryPath();
    if (path.empty()) {
        return false;
    }

    if (CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ParseHexU32(std::string_view value, std::uint32_t* parsed) noexcept {
    if (parsed == nullptr || value.size() < 3 || value[0] != '0' ||
        (value[1] != 'x' && value[1] != 'X')) {
        return false;
    }

    std::uint64_t result = 0;
    for (std::size_t i = 2; i < value.size(); ++i) {
        const char ch = value[i];
        std::uint32_t digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<std::uint32_t>(10 + (ch - 'a'));
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<std::uint32_t>(10 + (ch - 'A'));
        } else {
            return false;
        }

        result = (result << 4u) | digit;
        if (result > 0xffffffffu) {
            return false;
        }
    }

    *parsed = static_cast<std::uint32_t>(result);
    return true;
}

bool ParseCacheEntries(
    std::string_view bytes,
    std::vector<CacheEntry>* entries) {
    if (entries == nullptr) {
        return false;
    }

    entries->clear();
    if (bytes.empty()) {
        return false;
    }

    std::size_t line_start = 0;
    std::size_t line_end = bytes.find('\n');
    if (line_end == std::string_view::npos) {
        line_end = bytes.size();
    }

    std::string_view header = bytes.substr(line_start, line_end - line_start);
    if (!header.empty() && header.back() == '\r') {
        header.remove_suffix(1);
    }
    const bool version_1 = header == kCacheHeaderV1;
    const bool version_2 = header == kCacheHeaderV2;
    const bool version_3 = header == kCacheHeaderV3;
    if (!version_1 && !version_2 && !version_3) {
        return false;
    }

    line_start = line_end == bytes.size() ? bytes.size() : line_end + 1;
    while (line_start < bytes.size()) {
        line_end = bytes.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = bytes.size();
        }

        std::string_view line = bytes.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!line.empty()) {
            const std::size_t separator_a = line.find('|');
            if (separator_a != std::string_view::npos && separator_a != 0 &&
                separator_a + 1 < line.size()) {
                const std::size_t separator_b =
                    (version_2 || version_3) ? line.find('|', separator_a + 1)
                                             : std::string_view::npos;
                const std::size_t separator_c =
                    version_3 && separator_b != std::string_view::npos
                    ? line.find('|', separator_b + 1)
                    : std::string_view::npos;
                const std::string_view name = line.substr(0, separator_a);
                const std::string normalized_name = NormalizeName(name);
                std::uint32_t classes_bitmask = 0;
                std::uint8_t native_class_id = 0;
                std::string server_name;
                std::string normalized_server_name;
                const std::string_view mask_field =
                    (version_2 || version_3) && separator_b != std::string_view::npos
                    ? line.substr(separator_a + 1, separator_b - separator_a - 1)
                    : line.substr(separator_a + 1);
                if (!normalized_name.empty() &&
                    ParseHexU32(mask_field, &classes_bitmask)) {
                    if (version_2 || version_3) {
                        if (separator_b == std::string_view::npos || separator_b + 1 >= line.size()) {
                            line_start = line_end == bytes.size() ? bytes.size() : line_end + 1;
                            continue;
                        }

                        const std::string_view native_class_field_view =
                            version_3 && separator_c != std::string_view::npos
                            ? line.substr(separator_b + 1, separator_c - separator_b - 1)
                            : line.substr(separator_b + 1);
                        const std::string native_class_field(native_class_field_view);
                        const int parsed_native_class_id = std::atoi(native_class_field.c_str());
                        if (parsed_native_class_id <= 0 || parsed_native_class_id > 255) {
                            line_start = line_end == bytes.size() ? bytes.size() : line_end + 1;
                            continue;
                        }

                        native_class_id = static_cast<std::uint8_t>(parsed_native_class_id);
                        if (version_3) {
                            if (separator_c == std::string_view::npos || separator_c + 1 >= line.size()) {
                                line_start = line_end == bytes.size() ? bytes.size() : line_end + 1;
                                continue;
                            }

                            server_name.assign(line.substr(separator_c + 1));
                            normalized_server_name = NormalizeName(server_name);
                            if (normalized_server_name.empty()) {
                                line_start = line_end == bytes.size() ? bytes.size() : line_end + 1;
                                continue;
                            }
                        }
                    }

                    bool updated = false;
                    for (CacheEntry& entry : *entries) {
                        if (entry.normalized_name == normalized_name) {
                            entry.name.assign(name.begin(), name.end());
                            entry.classes_bitmask = classes_bitmask;
                            entry.native_class_id = native_class_id;
                            entry.server_name = server_name;
                            entry.normalized_server_name = normalized_server_name;
                            updated = true;
                            break;
                        }
                    }

                    if (!updated) {
                        CacheEntry entry = {};
                        entry.name.assign(name.begin(), name.end());
                        entry.normalized_name = normalized_name;
                        entry.classes_bitmask = classes_bitmask;
                        entry.native_class_id = native_class_id;
                        entry.server_name = server_name;
                        entry.normalized_server_name = normalized_server_name;
                        entries->push_back(std::move(entry));
                    }
                }
            }
        }

        line_start = line_end == bytes.size() ? bytes.size() : line_end + 1;
    }

    return true;
}

bool ReadCacheEntries(std::vector<CacheEntry>* entries) noexcept {
    if (entries == nullptr) {
        return false;
    }

    entries->clear();

    const std::wstring path = BuildCacheFilePath();
    if (path.empty()) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const DWORD file_size = GetFileSize(file, nullptr);
    if (file_size == kInvalidFileSize || file_size > kMaxCacheFileBytes) {
        CloseHandle(file);
        return false;
    }

    std::string buffer(static_cast<std::size_t>(file_size), '\0');
    DWORD bytes_read = 0;
    const bool read_ok =
        buffer.empty() ||
        ReadFile(file, buffer.data(), file_size, &bytes_read, nullptr);
    CloseHandle(file);

    if (!read_ok || bytes_read != file_size) {
        return false;
    }

    return ParseCacheEntries(buffer, entries);
}

std::string BuildCacheFileContents(std::vector<CacheEntry> entries) {
    std::sort(
        entries.begin(),
        entries.end(),
        [](const CacheEntry& left, const CacheEntry& right) {
            return left.normalized_name < right.normalized_name;
        });

    std::string contents = kCacheHeaderV3;
    contents += "\r\n";
    for (const CacheEntry& entry : entries) {
        if (entry.name.empty() || entry.normalized_name.empty()) {
            continue;
        }

        std::ostringstream line;
        line
            << entry.name
            << "|0x"
            << std::hex
            << std::nouppercase
            << std::setw(8)
            << std::setfill('0')
            << entry.classes_bitmask
            << "|"
            << std::dec
            << static_cast<unsigned int>(entry.native_class_id)
            << "|"
            << entry.server_name
            << "\r\n";
        contents += line.str();
    }

    return contents;
}

bool WriteCacheEntries(const std::vector<CacheEntry>& entries) noexcept {
    if (!EnsureResourcesDirectory()) {
        return false;
    }

    const std::wstring path = BuildCacheFilePath();
    if (path.empty()) {
        return false;
    }

    const std::wstring temp_path = path + L".tmp";
    const std::string contents = BuildCacheFileContents(entries);

    HANDLE file = CreateFileW(
        temp_path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytes_written = 0;
    const DWORD bytes_to_write = static_cast<DWORD>(contents.size());
    const bool write_ok =
        bytes_to_write == 0 ||
        WriteFile(file, contents.data(), bytes_to_write, &bytes_written, nullptr);
    CloseHandle(file);

    if (!write_ok || bytes_written != bytes_to_write) {
        DeleteFileW(temp_path.c_str());
        return false;
    }

    if (!MoveFileExW(
            temp_path.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_path.c_str());
        return false;
    }

    return true;
}

}  // namespace

std::wstring GetCacheFilePathForLogging() noexcept {
    return BuildCacheFilePath();
}

std::wstring GetServerConfigPathForLogging() noexcept {
    return BuildServerConfigPath();
}

bool TryGetCurrentServerName(std::string* server_name) noexcept {
    if (server_name == nullptr) {
        return false;
    }

    server_name->clear();
    const std::wstring config_path = BuildServerConfigPath();
    if (config_path.empty()) {
        return false;
    }

    char config_path_ascii[MAX_PATH] = {};
    const int copied_path = WideCharToMultiByte(
        CP_ACP,
        0,
        config_path.c_str(),
        -1,
        config_path_ascii,
        MAX_PATH,
        nullptr,
        nullptr);
    if (copied_path <= 0) {
        return false;
    }

    char buffer[256] = {};
    const DWORD copied = GetPrivateProfileStringA(
        kLoginServerSection,
        kLastServerNameKey,
        "",
        buffer,
        static_cast<DWORD>(sizeof(buffer)),
        config_path_ascii);
    if (copied == 0 || buffer[0] == '\0') {
        return false;
    }

    *server_name = buffer;
    return !NormalizeName(*server_name).empty();
}

bool TryLookupCharacterClassMask(
    const char* character_name,
    unsigned int expected_native_class_id,
    const char* expected_server_name,
    std::uint32_t* classes_bitmask) noexcept {
    if (character_name == nullptr || character_name[0] == '\0' ||
        classes_bitmask == nullptr) {
        return false;
    }

    const std::string normalized_name = NormalizeName(character_name);
    const std::string normalized_server_name =
        expected_server_name == nullptr ? std::string() : NormalizeName(expected_server_name);
    if (normalized_name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_cache_mutex);
    std::vector<CacheEntry> entries;
    if (!ReadCacheEntries(&entries)) {
        return false;
    }

    for (const CacheEntry& entry : entries) {
        if (entry.normalized_name == normalized_name) {
            if (!normalized_server_name.empty()) {
                if (entry.normalized_server_name.empty() ||
                    entry.normalized_server_name != normalized_server_name) {
                    return false;
                }
            }
            if (expected_native_class_id != 0 &&
                entry.native_class_id != 0 &&
                entry.native_class_id != expected_native_class_id) {
                return false;
            }
            *classes_bitmask = entry.classes_bitmask;
            return true;
        }
    }

    return false;
}

bool TryLookupUniqueClassMaskForNativeClassId(
    unsigned int native_class_id,
    const char* expected_server_name,
    std::uint32_t* classes_bitmask) noexcept {
    if (classes_bitmask == nullptr || native_class_id == 0 || native_class_id > 32) {
        return false;
    }

    const std::uint32_t class_bit = 1u << (native_class_id - 1u);
    const std::string normalized_server_name =
        expected_server_name == nullptr ? std::string() : NormalizeName(expected_server_name);
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    std::vector<CacheEntry> entries;
    if (!ReadCacheEntries(&entries)) {
        return false;
    }

    bool found = false;
    std::uint32_t matched_mask = 0;
    for (const CacheEntry& entry : entries) {
        if (!normalized_server_name.empty()) {
            if (entry.normalized_server_name.empty() ||
                entry.normalized_server_name != normalized_server_name) {
                continue;
            }
        }
        if (entry.native_class_id != 0 && entry.native_class_id != native_class_id) {
            continue;
        }
        if ((entry.classes_bitmask & class_bit) == 0) {
            continue;
        }

        if (found && matched_mask != entry.classes_bitmask) {
            return false;
        }

        found = true;
        matched_mask = entry.classes_bitmask;
    }

    if (!found) {
        return false;
    }

    *classes_bitmask = matched_mask;
    return true;
}

bool StoreCharacterClassMask(
    const char* character_name,
    std::uint32_t classes_bitmask,
    unsigned int native_class_id,
    const char* server_name) noexcept {
    if (character_name == nullptr || character_name[0] == '\0' ||
        server_name == nullptr || server_name[0] == '\0') {
        return false;
    }

    const std::string normalized_name = NormalizeName(character_name);
    const std::string normalized_server_name = NormalizeName(server_name);
    if (normalized_name.empty() || normalized_server_name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_cache_mutex);
    std::vector<CacheEntry> entries;
    ReadCacheEntries(&entries);

    bool updated = false;
    for (CacheEntry& entry : entries) {
        if (entry.normalized_name == normalized_name &&
            entry.normalized_server_name == normalized_server_name) {
            entry.name = character_name;
            entry.classes_bitmask = classes_bitmask;
            entry.native_class_id = static_cast<std::uint8_t>(native_class_id);
            entry.server_name = server_name;
            entry.normalized_server_name = normalized_server_name;
            updated = true;
            break;
        }
    }

    if (!updated) {
        CacheEntry entry = {};
        entry.name = character_name;
        entry.normalized_name = normalized_name;
        entry.classes_bitmask = classes_bitmask;
        entry.native_class_id = static_cast<std::uint8_t>(native_class_id);
        entry.server_name = server_name;
        entry.normalized_server_name = normalized_server_name;
        entries.push_back(std::move(entry));
    }

    return WriteCacheEntries(entries);
}

}  // namespace monomyth::multiclass_cache
