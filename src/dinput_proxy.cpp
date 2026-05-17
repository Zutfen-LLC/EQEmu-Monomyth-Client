#include "dinput_proxy.h"

#include <windows.h>

#include <string>

#include "hook_manager.h"
#include "logger.h"
#include "packet_observer.h"
#include "receive_dispatch_discovery.h"
#include "runtime_capabilities.h"
#include "spell_usability_discovery.h"

namespace monomyth::proxy {
namespace {

INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;
HMODULE g_real_module = nullptr;
HRESULT g_init_result = E_FAIL;
bool g_proxy_loaded = false;
bool g_proxy_ready = false;
bool g_hooks_started = false;
bool g_fingerprint_checked = false;
monomyth::fingerprint::Result g_fingerprint = {};
monomyth::runtime::Manifest g_capabilities = monomyth::runtime::BuildDisabledCapabilityManifest(
    false,
    false,
    L"initialization pending");

DirectInput8CreateFn g_direct_input8_create = nullptr;
DllCanUnloadNowFn g_dll_can_unload_now = nullptr;
DllGetClassObjectFn g_dll_get_class_object = nullptr;
DllRegisterServerFn g_dll_register_server = nullptr;
DllUnregisterServerFn g_dll_unregister_server = nullptr;
GetdfDIJoystickFn g_getdf_dijoystick = nullptr;

void PublishCapabilitiesWithDiscovery() noexcept {
    monomyth::receive_dispatch_discovery::Initialize();
    const monomyth::receive_dispatch_discovery::Result discovery =
        monomyth::receive_dispatch_discovery::Run(g_capabilities.hooks_allowed);
    monomyth::runtime::ApplyReceiveDispatchDiscovery(&g_capabilities, discovery);
    monomyth::spell_usability_discovery::Initialize();
    const monomyth::spell_usability_discovery::Result spell_discovery =
        monomyth::spell_usability_discovery::Run(
            g_capabilities.spell_usability_discovery_allowed,
            g_capabilities.fingerprint_matched);
    monomyth::runtime::ApplySpellUsabilityDiscovery(&g_capabilities, spell_discovery);
    monomyth::runtime::LogCapabilityManifest(g_capabilities);
    monomyth::receive_dispatch_discovery::LogResult(discovery);
    monomyth::spell_usability_discovery::LogResult(spell_discovery);
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
        g_dll_unregister_server != nullptr;

    monomyth::logger::Log(ok ? L"proxy: required export resolution succeeded" : L"proxy: required export resolution failed");
    if (g_getdf_dijoystick == nullptr) {
        monomyth::logger::Log(L"proxy: optional GetdfDIJoystick export missing from real dinput8.dll");
    }
    return ok;
}

BOOL CALLBACK InitializeOnce(PINIT_ONCE, PVOID, PVOID*) {
    monomyth::logger::Log(L"bootstrap: initialization started");

    const std::wstring system_path = BuildSystemDinputPath();
    if (system_path.empty()) {
        monomyth::logger::Log(L"proxy: failed to build system dinput8 path");
        g_capabilities = monomyth::runtime::BuildDisabledCapabilityManifest(
            false,
            false,
            L"failed to build system dinput8 path");
        PublishCapabilitiesWithDiscovery();
        g_init_result = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        return TRUE;
    }

    g_real_module = LoadLibraryW(system_path.c_str());
    if (g_real_module == nullptr) {
        monomyth::logger::Log(L"proxy: failed to load real dinput8.dll");
        g_capabilities = monomyth::runtime::BuildDisabledCapabilityManifest(
            false,
            false,
            L"failed to load real system dinput8.dll");
        PublishCapabilitiesWithDiscovery();
        g_init_result = HRESULT_FROM_WIN32(GetLastError());
        return TRUE;
    }

    monomyth::logger::Log(L"proxy: loaded real system dinput8.dll");
    g_proxy_loaded = true;
    if (!ResolveExports()) {
        g_capabilities = monomyth::runtime::BuildDisabledCapabilityManifest(
            g_proxy_loaded,
            false,
            L"failed to resolve required dinput8 exports");
        PublishCapabilitiesWithDiscovery();
        g_init_result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return TRUE;
    }

    g_proxy_ready = true;
    g_fingerprint = monomyth::fingerprint::Evaluate();
    g_fingerprint_checked = true;
    g_capabilities = monomyth::runtime::BuildCapabilityManifest(
        g_proxy_loaded,
        g_proxy_ready,
        g_fingerprint_checked,
        g_fingerprint);
    PublishCapabilitiesWithDiscovery();

    monomyth::packet_observer::Initialize(g_capabilities);

    if (g_capabilities.hooks_allowed) {
        g_hooks_started = monomyth::hooks::Initialize(g_capabilities);
        if (!g_hooks_started) {
            monomyth::logger::Log(
                L"hook_manager: initialization failed; continuing proxy-only behavior where possible");
        }
    } else {
        monomyth::logger::Log(L"hook_manager: skipped because capability manifest denied hook capability");
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

    monomyth::packet_observer::Shutdown();
    monomyth::receive_dispatch_discovery::Shutdown();
    monomyth::spell_usability_discovery::Shutdown();

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

const monomyth::runtime::Manifest& GetCapabilityManifest() noexcept {
    return g_capabilities;
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
