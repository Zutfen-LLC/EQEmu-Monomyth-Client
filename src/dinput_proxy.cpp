#include "dinput_proxy.h"

#include <windows.h>

#include <string>

#include "hook_manager.h"
#include "logger.h"

namespace monomyth::proxy {
namespace {

INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;
HMODULE g_real_module = nullptr;
HRESULT g_init_result = E_FAIL;
bool g_proxy_ready = false;
bool g_hooks_started = false;
monomyth::fingerprint::Result g_fingerprint = {};

DirectInput8CreateFn g_direct_input8_create = nullptr;
DllCanUnloadNowFn g_dll_can_unload_now = nullptr;
DllGetClassObjectFn g_dll_get_class_object = nullptr;
DllRegisterServerFn g_dll_register_server = nullptr;
DllUnregisterServerFn g_dll_unregister_server = nullptr;
GetdfDIJoystickFn g_getdf_dijoystick = nullptr;

void LogBoolField(std::wstring* message, const wchar_t* field, bool value) {
    message->append(field);
    message->append(value ? L"true" : L"false");
}

std::wstring BuildSystemDinputPath() noexcept {
    wchar_t system_dir[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring path(system_dir, system_dir + length);
    path += L"\\dinput8.dll";
    return path;
}

bool ResolveExports() noexcept {
    g_direct_input8_create = reinterpret_cast<DirectInput8CreateFn>(GetProcAddress(g_real_module, "DirectInput8Create"));
    g_dll_can_unload_now = reinterpret_cast<DllCanUnloadNowFn>(GetProcAddress(g_real_module, "DllCanUnloadNow"));
    g_dll_get_class_object = reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(g_real_module, "DllGetClassObject"));
    g_dll_register_server = reinterpret_cast<DllRegisterServerFn>(GetProcAddress(g_real_module, "DllRegisterServer"));
    g_dll_unregister_server = reinterpret_cast<DllUnregisterServerFn>(GetProcAddress(g_real_module, "DllUnregisterServer"));
    g_getdf_dijoystick = reinterpret_cast<GetdfDIJoystickFn>(GetProcAddress(g_real_module, "GetdfDIJoystick"));

    const bool ok =
        g_direct_input8_create != nullptr &&
        g_dll_can_unload_now != nullptr &&
        g_dll_get_class_object != nullptr &&
        g_dll_register_server != nullptr &&
        g_dll_unregister_server != nullptr &&
        g_getdf_dijoystick != nullptr;

    monomyth::logger::Log(ok ? L"proxy: export resolution succeeded" : L"proxy: export resolution failed");
    return ok;
}

BOOL CALLBACK InitializeOnce(PINIT_ONCE, PVOID, PVOID*) {
    monomyth::logger::Log(L"bootstrap: initialization started");

    const std::wstring system_path = BuildSystemDinputPath();
    if (system_path.empty()) {
        monomyth::logger::Log(L"proxy: failed to build system dinput8 path");
        g_init_result = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        return TRUE;
    }

    g_real_module = LoadLibraryW(system_path.c_str());
    if (g_real_module == nullptr) {
        monomyth::logger::Log(L"proxy: failed to load real dinput8.dll");
        g_init_result = HRESULT_FROM_WIN32(GetLastError());
        return TRUE;
    }

    monomyth::logger::Log(L"proxy: loaded real system dinput8.dll");
    if (!ResolveExports()) {
        g_init_result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return TRUE;
    }

    g_proxy_ready = true;
    g_fingerprint = monomyth::fingerprint::Evaluate();

    std::wstring fingerprint_message = L"fingerprint: ";
    LogBoolField(&fingerprint_message, L"hooks_allowed=", g_fingerprint.hooks_allowed);
    fingerprint_message += L" ";
    LogBoolField(&fingerprint_message, L"process_name_match=", g_fingerprint.process_name_match);
    fingerprint_message += L" ";
    LogBoolField(&fingerprint_message, L"version_strings_checked=", g_fingerprint.version_strings_checked);
    fingerprint_message += L" ";
    LogBoolField(&fingerprint_message, L"version_strings_match=", g_fingerprint.version_strings_match);
    fingerprint_message += L" reason=\"";
    fingerprint_message += g_fingerprint.reason;
    fingerprint_message += L"\"";
    monomyth::logger::Log(fingerprint_message);

    if (g_fingerprint.hooks_allowed) {
        g_hooks_started = monomyth::hooks::Initialize();
    } else {
        monomyth::logger::Log(L"hook_manager: skipped because fingerprint guard denied hook capability");
    }

    g_init_result = S_OK;
    return TRUE;
}

}  // namespace

HRESULT EnsureInitialized() noexcept {
    InitOnceExecuteOnce(&g_init_once, InitializeOnce, nullptr, nullptr);
    return g_init_result;
}

void Shutdown() noexcept {
    if (g_hooks_started) {
        monomyth::hooks::Shutdown();
        g_hooks_started = false;
    }

    if (g_real_module != nullptr) {
        FreeLibrary(g_real_module);
        g_real_module = nullptr;
    }

    monomyth::logger::Log(L"bootstrap: shutdown complete");
    monomyth::logger::Flush();
}

const monomyth::fingerprint::Result& GetFingerprintResult() noexcept {
    return g_fingerprint;
}

DirectInput8CreateFn GetDirectInput8Create() noexcept {
    return g_proxy_ready ? g_direct_input8_create : nullptr;
}

DllCanUnloadNowFn GetDllCanUnloadNow() noexcept {
    return g_proxy_ready ? g_dll_can_unload_now : nullptr;
}

DllGetClassObjectFn GetDllGetClassObject() noexcept {
    return g_proxy_ready ? g_dll_get_class_object : nullptr;
}

DllRegisterServerFn GetDllRegisterServer() noexcept {
    return g_proxy_ready ? g_dll_register_server : nullptr;
}

DllUnregisterServerFn GetDllUnregisterServer() noexcept {
    return g_proxy_ready ? g_dll_unregister_server : nullptr;
}

GetdfDIJoystickFn GetdfDIJoystickExport() noexcept {
    return g_proxy_ready ? g_getdf_dijoystick : nullptr;
}

}  // namespace monomyth::proxy
