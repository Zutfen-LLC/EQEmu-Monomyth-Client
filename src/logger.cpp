#include "logger.h"

#include <windows.h>

#include <mutex>
#include <string>

namespace monomyth::logger {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

std::mutex g_log_mutex;
HANDLE g_log_file = INVALID_HANDLE_VALUE;
bool g_log_path_attempted = false;

std::wstring BuildLocalLogPath() noexcept {
    wchar_t module_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), module_path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring path(module_path, module_path + length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"monomyth-client.log";
    }

    path.resize(slash + 1);
    path += L"monomyth-client.log";
    return path;
}

std::wstring BuildTempLogPath() noexcept {
    wchar_t temp_path[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, temp_path);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring path(temp_path, temp_path + length);
    path += L"monomyth-client.log";
    return path;
}

HANDLE OpenLogFileAt(const std::wstring& path) noexcept {
    if (path.empty()) {
        return INVALID_HANDLE_VALUE;
    }

    return CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

void EnsureLogFile() noexcept {
    if (g_log_path_attempted) {
        return;
    }

    g_log_path_attempted = true;
    g_log_file = OpenLogFileAt(BuildLocalLogPath());
    if (g_log_file != INVALID_HANDLE_VALUE) {
        return;
    }

    g_log_file = OpenLogFileAt(BuildTempLogPath());
}

std::wstring TimestampPrefix() noexcept {
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    wchar_t buffer[64] = {};
    wsprintfW(
        buffer,
        L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds);
    return buffer;
}

std::string WideToUtf8(std::wstring_view value) noexcept {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        utf8.data(),
        required,
        nullptr,
        nullptr);
    if (written <= 0) {
        return {};
    }

    if (written < required) {
        utf8.resize(static_cast<std::size_t>(written));
    }
    return utf8;
}

void WriteLine(std::wstring_view line) noexcept {
    if (g_log_file == INVALID_HANDLE_VALUE) {
        return;
    }

    std::wstring output = TimestampPrefix();
    output.append(line.begin(), line.end());
    output.append(L"\r\n");

    const std::string utf8_output = WideToUtf8(output);
    if (utf8_output.empty()) {
        return;
    }

    DWORD bytes_to_write = static_cast<DWORD>(utf8_output.size());
    DWORD bytes_written = 0;
    WriteFile(g_log_file, utf8_output.data(), bytes_to_write, &bytes_written, nullptr);
}

}  // namespace
void Log(std::wstring_view message) noexcept {
    std::lock_guard<std::mutex> guard(g_log_mutex);
    EnsureLogFile();
    WriteLine(message);
}

void Log(const wchar_t* message) noexcept {
    Log(message == nullptr ? std::wstring_view{} : std::wstring_view(message));
}

void Flush() noexcept {
    std::lock_guard<std::mutex> guard(g_log_mutex);
    if (g_log_file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_log_file);
    }
}

}  // namespace monomyth::logger
