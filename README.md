# Monomyth Client Bootstrap

This repository contains a fresh minimal `dinput8.dll` bootstrap for the EverQuest ROF2 client used by Monomyth. This slice is intentionally narrow: it proxies the system `dinput8.dll`, records low-noise startup diagnostics, applies a fail-closed ROF2 fingerprint guard, and exposes a future hook lifecycle with no active hooks.

## Current behavior

- Loads as `dinput8.dll` next to `eqgame.exe`.
- Resolves the real system `dinput8.dll` from the Windows system directory.
- Proxies the required `dinput8` exports:
  - `DirectInput8Create`
  - `DllCanUnloadNow`
  - `DllGetClassObject`
  - `DllRegisterServer`
  - `DllUnregisterServer`
  - `GetdfDIJoystick`
- Writes a small log with DLL load/proxy/fingerprint status.
- Checks the host process name and, when version resources are present, looks for ROF2 markers `May 10 2013` and `23:30:08`.
- Exposes a no-op `HookManager` lifecycle and emits one inert post-guard heartbeat when hooks would be allowed.

## Safety model

- DirectInput proxying is always the primary responsibility.
- Fingerprint failure never blocks normal DirectInput behavior.
- Hook capability is fail-closed and computed before any future hook install point.
- This slice does not patch memory, install detours, or implement gameplay/UI behavior.

## Non-goals in this slice

- No MQ2 runtime.
- No THJ patch bundle.
- No packet hooks or `OP_ServerAuthStats`.
- No labels, overlays, class displays, `/notify`, pet controls, or UI automation.
- No copied THJ decompiled code and no wholesale fork of other DLL projects.

## Build

This project is configured with CMake for a 32-bit Windows build:

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Expected output:

- `build/Release/dinput8.dll`

## Install

Place the built `dinput8.dll` beside the ROF2 `eqgame.exe`. The DLL will forward DirectInput calls to the real system `dinput8.dll`.

## Logging

The bootstrap attempts to write `monomyth-client.log` beside the DLL. If that fails, it falls back to the process temp directory. Logging failure is non-fatal.

Startup logs include:

- DLL attach and detach
- Real `dinput8.dll` load result
- Export resolution result
- Fingerprint result and reason
- Inert post-guard heartbeat when hooks are allowed

## Test checklist

1. Build the DLL on Windows.
2. Verify exports:

   ```powershell
   dumpbin /exports build\Release\dinput8.dll
   ```

3. Place the DLL next to the ROF2 client executable.
4. Launch the client and confirm:
   - The game starts normally.
   - `DirectInput8Create` proxying succeeds.
   - `monomyth-client.log` shows startup state.
   - Fingerprint failure, if any, reports `hooks_allowed=false` without breaking input.

## Future slices

Future work can add versioned Monomyth client/server projection and guarded hook installation behind the existing capability gate. That work should remain explicit, versionable, and separate from server-authoritative gameplay logic.
