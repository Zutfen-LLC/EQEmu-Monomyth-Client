# Monomyth Client Bootstrap

This repository contains a fresh minimal `dinput8.dll` bootstrap for the EverQuest ROF2 client used by Monomyth. This slice is intentionally narrow: it proxies the system `dinput8.dll`, records low-noise startup diagnostics, applies a fail-closed ROF2 fingerprint guard, centralizes runtime capability state in one internal manifest, performs fail-closed receive dispatcher discovery for the known ROF2 candidate, and can install one dev-gated receive-only metadata hook when every safety gate passes.

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
- Checks the host process name and first evaluates version-resource markers for `May 10 2013` and `23:30:08`.
- Falls back once at startup to a fail-closed `eqgame.exe` byte scan when version resources are unavailable or inconclusive; both markers must be present or the fingerprint remains false.
- Runs a fail-closed receive dispatcher discovery pass only after the fingerprint/capability path allows enhancement discovery.
- Exposes a `HookManager` lifecycle that installs no active hook by default.
- Keeps `packet_hooks_allowed=false` unless all of these gates pass:
  - DirectInput proxy bootstrap is ready.
  - ROF2 fingerprint guard passes.
  - Receive dispatcher discovery validates the known ROF2 dispatcher.
  - The local developer explicitly sets `MONOMYTH_ENABLE_PACKET_HOOKS=1`.
- When enabled, installs exactly one receive dispatcher hook at the validated candidate and routes metadata to `PacketObserver`.
- A second explicit developer opt-in can enable bounded receive payload-prefix introspection for an allowlisted opcode set only.
- Leaves `ui_hooks_allowed=false`.

## Safety model

- DirectInput proxying is always the primary responsibility.
- Fingerprint failure never blocks normal DirectInput behavior.
- Fingerprint fallback still applies only to enhancement capability gating; DirectInput proxying continues whether the fallback matches or fails.
- Hook capability is fail-closed and computed in the runtime capability manifest before any hook install point.
- Packet hook capability is disabled by default and requires the scary local-only environment variable `MONOMYTH_ENABLE_PACKET_HOOKS=1`.
- Receive payload introspection is also disabled by default and additionally requires `MONOMYTH_ENABLE_RECV_INTROSPECTION=1` after packet hook gating has already passed.
- UI hook capability remains intentionally disabled.
- Receive dispatcher discovery validates only static ROF2 executable-image structure and records success or failure in the internal runtime capability manifest.
- The receive hook is receive-only and metadata-only. It observes opcode/message id, payload length, and source/context pointer value.
- Without the second opt-in, the hook does **not** read, copy, decode, log, retain, or mutate packet payload bytes.
- With both opt-ins enabled, payload access is still fail-closed: only allowlisted opcodes are considered, reads are bounded to at most 16 bytes and never past `payload_length`, suspicious lengths are skipped conservatively, and bytes are logged only as a compact one-line hex prefix.
- The hook always calls through to the original dispatcher.
- Hook uninstall is idempotent and runs before `PacketObserver` shutdown.
- This slice does not implement gameplay/UI behavior or any send interception.

## Non-goals in this slice

- No MQ2 runtime.
- No THJ patch bundle.
- No `OP_ServerAuthStats`, Monomyth projection protocol, opcode decoding, or projection state cache.
- No send interception.
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
- MSVC `Release` builds statically link the C/C++ runtime (`/MT`) so the client DLL does not require `MSVCP140.dll` or `VCRUNTIME140.dll` beside `eqgame.exe`.
- MSVC `Debug` builds use the static debug runtime (`/MTd`).

Post-build validation:

```powershell
dumpbin /headers build\Release\dinput8.dll | findstr machine
dumpbin /dependents build\Release\dinput8.dll
dumpbin /exports build\Release\dinput8.dll
```

Expected checks:

- `dumpbin /headers` reports `14C machine (x86)`.
- `dumpbin /dependents` does not list `MSVCP140.dll` or `VCRUNTIME140.dll`.
- `dumpbin /exports` matches the exports declared in `dinput8.def`.

## CI

GitHub Actions builds this project on the repository's Windows runner group for both `Debug` and `Release` using the same 32-bit CMake configuration documented above. The workflow initializes the MSVC x86 developer environment and expects `cmake` to already be installed on the runner and available on `PATH`. Each successful run uploads the built `dinput8.dll` as a workflow artifact. Pushes to `main` also update a rolling prerelease tagged `latest-build` so the newest release build is available as `dinput8.dll` and the debug build as `dinput8-Debug.dll` from the repository Releases page.

## Install

Place the built `dinput8.dll` beside the ROF2 `eqgame.exe`. The DLL will forward DirectInput calls to the real system `dinput8.dll`.

## Logging

The bootstrap attempts to write `monomyth-client.log` beside the DLL. If that fails, it falls back to the process temp directory. Logging failure is non-fatal.

Startup logs include:

- DLL attach and detach
- Real `dinput8.dll` load result
- Export resolution result
- One structured `CapabilityManifest ...` summary line with proxy, host, fingerprint, hook, packet, and UI capability state plus the reason string
- The `CapabilityManifest` line includes `fingerprint_method=version_resource`, `fingerprint_method=byte_scan`, or `fingerprint_method=unavailable` for grep-friendly startup diagnosis
- The `CapabilityManifest` line includes `receive_introspection_dev_opt_in` and `receive_introspection_allowed` so payload-prefix access is explicit in startup logs
- The `CapabilityManifest` line includes `runtime_module_base`, `receive_dispatch_rva`, and `receive_dispatch_address` whenever discovery resolves the validated dispatcher candidate at runtime
- One `ReceiveDispatchDiscovery ...` line with the discovery state, runtime module base, validated candidate RVA/resolved address when available, a concise reason, and per-check pass/fail/advisory diagnostics
- Post-guard heartbeat when hooks are allowed
- One `PacketObserver state=...` line indicating current observer state
- When the dev hook is enabled, rate-limited metadata lines beginning with `PacketObserverRecv`
- When both dev opt-ins are enabled, separate rate-limited allowlisted prefix lines begin with `PacketObserverRecvIntrospection` and skip diagnostics begin with `PacketObserverRecvIntrospectionSkip`
- One `PacketObserver state=shutdown observed_receive_count=...` line on shutdown

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
   - When version resources are unavailable or inconclusive, fingerprint success requires both ROF2 byte markers `May 10 2013` and `23:30:08`.

## Troubleshooting

If the ROF2 client fails at startup with `0xc0000142` after placing the custom `dinput8.dll` beside `eqgame.exe`, check the loader-facing basics before changing any hook logic:

- Confirm the DLL is still Win32/x86:

  ```powershell
  dumpbin /headers build\Release\dinput8.dll | findstr machine
  ```

- Confirm the `Release` DLL no longer depends on the VC++ redistributable DLLs:

  ```powershell
  dumpbin /dependents build\Release\dinput8.dll
  ```

- Confirm the proxy exports are unchanged:

  ```powershell
  dumpbin /exports build\Release\dinput8.dll
  ```

For this repository's expected `Release` output, `dumpbin /dependents build\Release\dinput8.dll` should not list `MSVCP140.dll` or `VCRUNTIME140.dll`. If either appears, the build is not using the intended static MSVC runtime and the loader environment may still be missing required x86 VC++ runtime files.

## Packet observer scaffold

The `PacketObserver` module (`src/packet_observer.h` / `src/packet_observer.cpp`) provides the lifecycle boundary for receive-only packet observation.

- It is disabled by default because `packet_hooks_allowed=false`.
- It can initialize only when the manifest says packet hooks are allowed.
- It receives immutable metadata only: opcode/message id, payload length, source/context pointer value, and an internal observed packet counter.
- By default it receives immutable metadata only: opcode/message id, payload length, source/context pointer value, and an internal observed packet counter.
- It reads packet bytes only when both packet-hook and receive-introspection dev opt-ins are enabled, and even then only for allowlisted opcodes, only up to a 16-byte prefix, only within `payload_length`, and only for compact hex logging.
- It does **not** define opcode-specific behavior.
- It logs the first 50 observed receive packets, then every 500th packet, plus the final observed count on shutdown.
- Log lines start with `PacketObserverRecv` and include opcode/message id and payload length.
- Introspection logs default to the allowlisted opcodes `0x7dfc`, `0x213f`, and `0x2958`, and the allowlist can be overridden locally with `MONOMYTH_RECV_INTROSPECT_OPCODES=0x7dfc,0x213f`.
- High-volume opcode `0x5089` is intentionally not in the default allowlist, so it remains metadata-only unless a developer explicitly overrides the local allowlist.

Future receive-only observation must continue to route through this module, remain gated on `packet_hooks_allowed`, and stay strictly non-mutating.

## Receive dispatcher discovery

The `ReceiveDispatchDiscovery` module (`src/receive_dispatch_discovery.h` / `src/receive_dispatch_discovery.cpp`) is a fail-closed static discovery scaffold for the validated ROF2 receive dispatcher candidate at preferred VA `0x004c3250` / RVA `0x000c3250`.

Discovery runs only after the existing fingerprint/capability manifest path says enhancement discovery is allowed. It resolves the validated candidate relative to the loaded `eqgame.exe` module base at runtime, verifies that the resolved address still falls inside the loaded image, and only then applies layered executable-image checks. Mandatory checks remain fail-closed and include candidate range, executable-section membership, entry-adjacent dispatch-like compare/branch evidence, compare-tree evidence near the known dispatcher case-tree RVA, bounded unknown-message path evidence near the known fallback path RVA, and the two known direct feeder callsites. The `ret 0x10` epilogue is logged as advisory evidence using a bounded scan near the known epilogue RVA rather than a small entry-adjacent window because this dispatcher is large and its epilogue sits far from the entry. If runtime module discovery fails, if the resolved RVA falls outside the loaded image, or if any required structural check fails or cannot be evaluated confidently, the result is `failed` or `skipped_by_capability` and packet hooks remain disabled.

Discovery is not a pure RVA lookup. The validated runtime target is still `module_base + 0x000c3250`, but that address is accepted only when the layered structural checks agree. Startup logging now records the final state plus named check results such as `pass`, `fail`, `advisory_pass`, `advisory_fail`, or `skipped`.

This module does **not** install hooks, detours, callbacks, or memory patches. It does **not** observe, read, copy, decode, log, mutate, or retain any live packet data. A validated discovery result is only one prerequisite for the explicit dev-gated receive-only hook; by itself it does not activate `PacketObserver`, and `packet_hooks_allowed` remains `false`.

## Dev receive hook

The receive dispatcher hook is for local development only. Normal launches must not set the opt-in and will keep `packet_hooks_allowed=false`.

To opt in for local metadata testing, set this environment variable before launching the validated ROF2 client:

```powershell
$env:MONOMYTH_ENABLE_PACKET_HOOKS = "1"
```

To opt in to bounded payload-prefix introspection on top of the metadata hook, set the second local developer variable as well:

```powershell
$env:MONOMYTH_ENABLE_RECV_INTROSPECTION = "1"
```

`MONOMYTH_ENABLE_RECV_INTROSPECTION=1` is optional and separate. Without it, the hook remains metadata-only and does not touch packet bytes. With it, bounded payload-prefix reads are still fail-closed and limited to the allowlisted opcode set. The allowlist defaults to `0x7dfc,0x213f,0x2958` and can be overridden locally with `MONOMYTH_RECV_INTROSPECT_OPCODES=0x7dfc,0x213f`.

The packet-hook opt-in is not sufficient by itself. The hook installs only when DirectInput proxy bootstrap is ready, the ROF2 fingerprint guard passes, receive dispatcher discovery validates the known candidate, and `MONOMYTH_ENABLE_PACKET_HOOKS=1` is present. If any gate fails, if the detour cannot be installed cleanly, or if the dispatcher prologue is ambiguous, packet observation is disabled and the DLL continues proxy-only behavior where possible.

The hook boundary is the validated receive dispatcher candidate at runtime `module_base + 0x000c3250` (preferred VA `0x004c3250`). It preserves the validated `this`/ECX plus four stack argument shape: source/context pointer, opcode/message id, payload pointer, and payload length. Outside the optional bounded introspection mode, the hook never reads from the payload pointer and never retains it. There is no send hook path in this repository.

## Future slices

Future work can add versioned Monomyth client/server projection and guarded hook installation behind the internal capability manifest. Packet and UI capabilities should remain disabled until a future slice explicitly enables them. That work should remain explicit, versionable, and separate from server-authoritative gameplay logic.
