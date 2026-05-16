# Monomyth Client Bootstrap

This repository contains a fresh minimal `dinput8.dll` bootstrap for the EverQuest ROF2 client used by Monomyth. This slice is intentionally narrow: it proxies the system `dinput8.dll`, records low-noise startup diagnostics, applies a fail-closed ROF2 fingerprint guard, centralizes runtime capability state in one internal manifest, and exposes a future hook lifecycle with no active hooks.

Project history is tracked in [CHANGELOG.md](CHANGELOG.md).

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
- Builds a single internal runtime capability manifest that centralizes proxy, host, fingerprint, and future enhancement state.
- Checks the host process name and, when version resources are present, looks for ROF2 markers `May 10 2013` and `23:30:08`.
- Exposes a no-op `HookManager` lifecycle and emits one inert post-guard heartbeat when hooks would be allowed.
- Initializes an inert `PacketObserver` scaffold that is disabled by capability manifest (`packet_hooks_allowed=false`) and emits one startup state log line.

## Safety model

- DirectInput proxying is always the primary responsibility.
- Fingerprint failure never blocks normal DirectInput behavior.
- Hook capability is fail-closed and computed in the runtime capability manifest before any future hook install point.
- Packet and UI hook capabilities remain intentionally disabled in this slice.
- The `PacketObserver` scaffold is inert: it does not install packet hooks, intercept, decode, mutate, or log any packet bytes. It exists only to provide a safe, capability-gated lifecycle boundary for future receive-only work.
- This slice does not patch memory, install detours, or implement gameplay/UI behavior.

## Non-goals in this slice

- No MQ2 runtime.
- No THJ patch bundle.
- No packet hooks or `OP_ServerAuthStats`. The `PacketObserver` module is scaffolded but entirely inert — no ROF2 offsets, no detours, no opcode handling.
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

## CI

GitHub Actions builds this project on the repository's Windows runner group for both `Debug` and `Release` using the same 32-bit CMake configuration documented above. The workflow initializes the MSVC x86 developer environment and expects `cmake` to already be installed on the runner and available on `PATH`. Each successful run uploads the built `dinput8.dll` as a workflow artifact.

## Install

Place the built `dinput8.dll` beside the ROF2 `eqgame.exe`. The DLL will forward DirectInput calls to the real system `dinput8.dll`.

## Logging

The bootstrap attempts to write `monomyth-client.log` beside the DLL. If that fails, it falls back to the process temp directory. Logging failure is non-fatal.

Startup logs include:

- DLL attach and detach
- Real `dinput8.dll` load result
- Export resolution result
- One structured `CapabilityManifest ...` summary line with proxy, host, fingerprint, hook, packet, and UI capability state plus the reason string
- Inert post-guard heartbeat when hooks are allowed
- One `PacketObserver scaffold state=...` line indicating current observer state (currently always `disabled_by_capability`)

The capability manifest is currently internal and log-only. This slice does not emit any external JSON or config artifact.

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

## Packet observer scaffold

The `PacketObserver` module (`src/packet_observer.h` / `src/packet_observer.cpp`) provides a lifecycle boundary for future receive-only packet work. In this slice it is completely inert:

- It does **not** install packet hooks, detours, or callbacks into the ROF2 client.
- It does **not** intercept, read, decode, mutate, or log any packet bytes.
- It does **not** define ROF2 offsets or opcode-specific behavior.
- `packet_hooks_allowed` remains `false` in the capability manifest; the observer reports `disabled_by_capability` on every startup.

Future receive-only observation must be routed through this scaffold, gated on `packet_hooks_allowed` becoming `true` in the manifest, and must remain strictly non-mutating.

## Future slices

Future work can add versioned Monomyth client/server projection and guarded hook installation behind the internal capability manifest. Packet and UI capabilities should remain disabled until a future slice explicitly enables them. That work should remain explicit, versionable, and separate from server-authoritative gameplay logic.
