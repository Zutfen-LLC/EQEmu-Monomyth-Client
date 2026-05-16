#pragma once

#include <dinput.h>
#include <windows.h>

#include "fingerprint.h"
#include "runtime_capabilities.h"

namespace monomyth::proxy {

HRESULT EnsureInitialized() noexcept;
void Shutdown() noexcept;
const monomyth::fingerprint::Result& GetFingerprintResult() noexcept;
const monomyth::runtime::Manifest& GetCapabilityManifest() noexcept;

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DllCanUnloadNowFn = HRESULT(STDAPICALLTYPE*)();
using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
using DllRegisterServerFn = HRESULT(STDAPICALLTYPE*)();
using DllUnregisterServerFn = HRESULT(STDAPICALLTYPE*)();
using GetdfDIJoystickFn = const DIDATAFORMAT*(WINAPI*)();

DirectInput8CreateFn GetDirectInput8Create() noexcept;
DllCanUnloadNowFn GetDllCanUnloadNow() noexcept;
DllGetClassObjectFn GetDllGetClassObject() noexcept;
DllRegisterServerFn GetDllRegisterServer() noexcept;
DllUnregisterServerFn GetDllUnregisterServer() noexcept;
GetdfDIJoystickFn GetdfDIJoystickExport() noexcept;

}  // namespace monomyth::proxy
