#include <windows.h>

#include "dinput_proxy.h"
#include "logger.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        monomyth::logger::Log(L"dllmain: DLL_PROCESS_ATTACH");
        break;
    case DLL_PROCESS_DETACH:
        if (reserved == nullptr) {
            monomyth::logger::Log(L"dllmain: DLL_PROCESS_DETACH");
            monomyth::proxy::Shutdown();
        }
        break;
    default:
        break;
    }

    return TRUE;
}

HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD version,
    REFIID riidltf,
    LPVOID* out,
    LPUNKNOWN punkouter) {
    const HRESULT init_result = monomyth::proxy::EnsureInitialized();
    auto* real = monomyth::proxy::GetDirectInput8Create();
    if (FAILED(init_result) || real == nullptr) {
        return FAILED(init_result) ? init_result : E_FAIL;
    }

    return real(hinst, version, riidltf, out, punkouter);
}

STDAPI DllCanUnloadNow(void) {
    const HRESULT init_result = monomyth::proxy::EnsureInitialized();
    auto* real = monomyth::proxy::GetDllCanUnloadNow();
    if (FAILED(init_result) || real == nullptr) {
        return FAILED(init_result) ? init_result : E_FAIL;
    }

    return real();
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* out) {
    const HRESULT init_result = monomyth::proxy::EnsureInitialized();
    auto* real = monomyth::proxy::GetDllGetClassObject();
    if (FAILED(init_result) || real == nullptr) {
        return FAILED(init_result) ? init_result : CLASS_E_CLASSNOTAVAILABLE;
    }

    return real(rclsid, riid, out);
}

extern "C" HRESULT STDAPICALLTYPE DllRegisterServer(void) {
    const HRESULT init_result = monomyth::proxy::EnsureInitialized();
    auto* real = monomyth::proxy::GetDllRegisterServer();
    if (FAILED(init_result) || real == nullptr) {
        return FAILED(init_result) ? init_result : E_FAIL;
    }

    return real();
}

extern "C" HRESULT STDAPICALLTYPE DllUnregisterServer(void) {
    const HRESULT init_result = monomyth::proxy::EnsureInitialized();
    auto* real = monomyth::proxy::GetDllUnregisterServer();
    if (FAILED(init_result) || real == nullptr) {
        return FAILED(init_result) ? init_result : E_FAIL;
    }

    return real();
}

LPCDIDATAFORMAT WINAPI GetdfDIJoystick(void) {
    const HRESULT init_result = monomyth::proxy::EnsureInitialized();
    auto* real = monomyth::proxy::GetdfDIJoystickExport();
    if (FAILED(init_result) || real == nullptr) {
        return nullptr;
    }

    return real();
}
