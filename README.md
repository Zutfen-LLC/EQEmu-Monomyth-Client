# Monomyth Client Bootstrap

This repository contains a fresh minimal `dinput8.dll` bootstrap for the EverQuest ROF2 client used by Monomyth. This slice is intentionally narrow: it proxies the system `dinput8.dll`, records low-noise startup diagnostics, applies a fail-closed ROF2 fingerprint guard, centralizes runtime capability state in one internal manifest, performs fail-closed receive dispatcher discovery for the known ROF2 candidate, performs fail-closed read-only spell usability discovery for two ROF2 candidates when explicitly enabled, and can install dev-gated observation or multiclass spell usability hooks only when every safety gate passes.

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
- Keeps spell usability discovery disabled by default unless the local developer explicitly sets `MONOMYTH_ENABLE_SPELL_USABILITY_DISCOVERY=1`.
- When spell usability discovery is enabled on a supported ROF2 client, it uses fingerprint-gated cleanroom evidence for the known non-export spell/UI targets and fails closed with per-target reason codes when a checked-in locator is not available.
- Keeps passive spell usability tracing disabled by default unless the local developer additionally sets `MONOMYTH_ENABLE_SPELL_USABILITY_TRACE=1` and a target reaches validated trace-safe state.
- Keeps scroll-scribe trace discovery and instrumentation disabled by default unless the local developer explicitly sets `MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE=1` and both pinned pre-scribe targets validate cleanly.
- Keeps trace-only `OP_MemorizeSpell` send observation disabled by default unless the local developer explicitly sets `MONOMYTH_ENABLE_MEMORIZE_SEND_TRACE=1` and the pinned opcode-aware send wrapper validates cleanly.
- Keeps active multiclass spell usability disabled by default unless the local developer additionally sets `MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY=1` after `EQ_Spell::GetSpellLevelNeeded` validates.
- Exposes a `HookManager` lifecycle that installs no active hook by default.
- Keeps `packet_hooks_allowed=false` unless all of these gates pass:
  - DirectInput proxy bootstrap is ready.
  - ROF2 fingerprint guard passes.
  - Receive dispatcher discovery validates the known ROF2 dispatcher.
  - The local developer explicitly sets `MONOMYTH_ENABLE_PACKET_HOOKS=1`.
- When enabled, installs exactly one receive dispatcher hook at the validated candidate and routes metadata to `PacketObserver`.
- When that receive hook observes THJ `OP_ServerAuthStats` (`0x1338`), parses the server-authored stat payload read-only, extracts only `statClassesBitmask`, stores the latest valid bitmask in internal DLL state, and logs concise diagnostics.
- A second explicit developer opt-in can enable bounded receive payload-prefix introspection for an allowlisted opcode set only.
- Leaves `ui_hooks_allowed=false`.

## Safety model

- DirectInput proxying is always the primary responsibility.
- Fingerprint failure never blocks normal DirectInput behavior.
- Fingerprint fallback still applies only to enhancement capability gating; DirectInput proxying continues whether the fallback matches or fails.
- Hook capability is fail-closed and computed in the runtime capability manifest before any hook install point.
- Packet hook capability is disabled by default and requires the scary local-only environment variable `MONOMYTH_ENABLE_PACKET_HOOKS=1`.
- Receive payload introspection is also disabled by default and additionally requires `MONOMYTH_ENABLE_RECV_INTROSPECTION=1` after packet hook gating has already passed.
- Spell usability discovery is disabled by default and separately requires `MONOMYTH_ENABLE_SPELL_USABILITY_DISCOVERY=1` after the same ROF2 fingerprint/host guard has passed.
- Passive spell usability tracing is disabled by default and additionally requires `MONOMYTH_ENABLE_SPELL_USABILITY_TRACE=1` plus a validated target address/signature.
- Scroll-scribe trace instrumentation is disabled by default and additionally requires `MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE=1` plus validated `CInvSlot::HandleRButtonUp` and `EQ_Character::IsClassUsablePredicate` targets.
- Trace-only `OP_MemorizeSpell` send observation is disabled by default and additionally requires `MONOMYTH_ENABLE_MEMORIZE_SEND_TRACE=1` plus a validated `MemorizeSendPacketWrapper` target. It logs send attempts only, never mutates packet bytes, and never changes send ordering or return flow.
- Active multiclass spell usability is disabled by default and additionally requires `MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY=1` plus a validated `GetSpellLevelNeeded` target. Discovery or tracing alone never enables active behavior.
- UI hook capability remains intentionally disabled.
- Receive dispatcher discovery validates only static ROF2 executable-image structure and records success or failure in the internal runtime capability manifest.
- The receive hook is receive-only and non-mutating. It observes opcode/message id, payload length, and source/context pointer value, with one read-only `OP_ServerAuthStats` parser for server-authored class-bitmask capture.
- Known ROF2 opcode ids are enriched with a reference-only `opcode_name` field derived from the local EQEmu RoF2 opcode config plus custom THJ RoF2 opcode mappings; unknown ids log `opcode_name=unknown`.
- THJ `OP_ServerAuthStats` is recognized as ROF2 opcode `0x1338`, imported from `C:\Code\THJ-Server-Original\utils\patches\patch_RoF2.conf`. The receive observer has a read-only handler for this opcode that parses the minimal `Stat_Struct` wire layout and stores only `statClassesBitmask`; it does not write client memory, mutate packet bytes, update UI, or decode unrelated stat keys.
- Without the second opt-in, the hook does **not** perform generic payload-prefix introspection. The only payload decode outside that generic introspection mode is the read-only `OP_ServerAuthStats` handler.
- With both opt-ins enabled, payload access is still fail-closed: only allowlisted opcodes are considered, reads are bounded to at most 16 bytes and never past `payload_length`, suspicious lengths are skipped conservatively, and bytes are logged only as a compact one-line hex prefix.
- The hook always calls through to the original dispatcher.
- Hook uninstall is idempotent and runs before `PacketObserver` shutdown.
- This slice does not implement gameplay/UI behavior or any packet mutation. The send-side addition is observation-only.
- Multiclass spell usability changes only the validated `EQ_Spell::GetSpellLevelNeeded` return value when explicitly enabled. It uses the latest read-only `OP_ServerAuthStats` class mask, calls the original client function for each assigned class, selects the lowest valid required level, and falls back to the original requested-class result if the mask is absent, empty, invalid, or yields no valid class level.
- `CSpellBookWnd::CanStartMemming` remains trace-only. `CInvSlot::HandleRButtonUp` and `EQ_Character::IsClassUsablePredicate` can now be traced behind a separate dev gate, but they still call through to the original client functions and do not mutate arguments, return values, spell records, player profile class data, spellbook UI state, packets, or database state. `CanEquip` remains intentionally unpinned and disabled in this slice.

## Non-goals in this slice

- No MQ2 runtime.
- No THJ patch bundle.
- No Monomyth projection protocol, opcode decoding, or projection state cache.
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
- The `CapabilityManifest` line includes `spell_usability_discovery_dev_opt_in`, `spell_usability_discovery_allowed`, `spell_usability_trace_dev_opt_in`, and `spell_usability_trace_allowed`
- The `CapabilityManifest` line includes `scroll_scribe_trace_dev_opt_in`, `scroll_scribe_trace_allowed`, `scroll_scribe_trace_reason`, and per-target states/RVAs/addresses for `handle_rbutton_up` and `is_class_usable_predicate`
- The `CapabilityManifest` line includes `memorize_send_trace_dev_opt_in`, `memorize_send_trace_allowed`, and `memorize_send_trace_reason`
- The `CapabilityManifest` line includes `multiclass_spell_usability_dev_opt_in`, `multiclass_spell_usability_allowed`, and `multiclass_spell_usability_reason`
- The `CapabilityManifest` line includes per-target spell usability fields `handle_rbutton_up_state`, `handle_rbutton_up_rva`, `handle_rbutton_up_address`, `get_spell_level_needed_state`, `get_spell_level_needed_rva`, `get_spell_level_needed_address`, `is_class_usable_predicate_state`, `is_class_usable_predicate_rva`, `is_class_usable_predicate_address`, `can_start_memming_state`, `can_start_memming_rva`, `can_start_memming_address`, `memorize_send_packet_wrapper_state`, `memorize_send_packet_wrapper_rva`, and `memorize_send_packet_wrapper_address` when available
- One `ReceiveDispatchDiscovery ...` line with the discovery state, runtime module base, validated candidate RVA/resolved address when available, a concise reason, and per-check pass/fail/advisory diagnostics
- One `SpellUsabilityDiscovery ...` line per target with the target name, discovery state, runtime module base, candidate RVA/resolved address when available, `evidence_source`, `validation`, `failure_reason`, whether passive tracing is considered safe, the discovery method, a concise reason, and validation evidence
- Post-guard heartbeat when hooks are allowed
- One `PacketObserver state=...` line indicating current observer state
- When the dev hook is enabled, rate-limited metadata lines beginning with `PacketObserverRecv`
- When passive spell usability tracing is enabled, rate-limited lines begin with `SpellUsabilityTrace` and return the original function result unchanged
- When the dev hook observes `OP_ServerAuthStats`, handler lines beginning with `ServerAuthStats` log whether the packet parsed, the entry count, whether `statClassesBitmask` was present, and the bitmask in hex with recognized class names when available
- When both dev opt-ins are enabled, separate rate-limited allowlisted prefix lines begin with `PacketObserverRecvIntrospection` and skip diagnostics begin with `PacketObserverRecvIntrospectionSkip`
- Receive metadata and introspection log lines include `opcode_name=<name>` for known ROF2 ids and `opcode_name=unknown` otherwise
- When memorize-send tracing is enabled, rate-limited lines begin with `PacketObserverSend`
- One `PacketObserver state=shutdown observed_receive_count=... observed_send_count=...` line on shutdown

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

The `PacketObserver` module (`src/packet_observer.h` / `src/packet_observer.cpp`) provides the lifecycle boundary for receive metadata plus the narrow trace-only `OP_MemorizeSpell` send observation path.

- It is disabled by default because both receive metadata hooks and memorize-send tracing are opt-in.
- It can initialize when the manifest allows either receive metadata hooks or the trace-only memorize-send seam.
- It receives immutable metadata: opcode/message id, payload length, source/context pointer value, and an internal observed packet counter.
- By default it receives immutable metadata plus the read-only `OP_ServerAuthStats` class-bitmask capture when that exact opcode is observed.
- It adds ROF2 `opcode_name` as reference-only metadata using a static table derived from EQEmu's `patch_RoF2.conf` plus custom THJ RoF2 opcode mappings; unknown ids stay numeric and log as `opcode_name=unknown`.
- It recognizes THJ `OP_ServerAuthStats` as ROF2 opcode `0x1338` for exact-name lookup, receive metadata logging, and one read-only handler.
- The `OP_ServerAuthStats` handler parses `uint32 count` followed by `count` entries of `uint32 statKey` and `uint64 statValue`, scans entries by key, extracts only `statKey == 1`, rejects truncated or malformed packets, ignores key `0` and unknown keys, and stores the latest valid `uint32` class bitmask internally when present.
- It reads packet bytes only when both packet-hook and receive-introspection dev opt-ins are enabled, and even then only for allowlisted opcodes, only up to a 16-byte prefix, only within `payload_length`, and only for compact hex logging.
- Aside from the read-only `OP_ServerAuthStats` class-bitmask capture, it does **not** define opcode-specific behavior.
- It logs the first 50 observed receive packets, then every 500th packet, plus the final observed count on shutdown.
- Log lines start with `PacketObserverRecv` and include opcode/message id, `opcode_name`, and payload length.
- When `MONOMYTH_ENABLE_MEMORIZE_SEND_TRACE=1` is present and the validated send wrapper target installs, send lines start with `PacketObserverSend` and include `opcode=8572`, `opcode_hex=0x217c`, `opcode_name=OP_MemorizeSpell`, the payload length derived from the wrapper's total buffer length minus the 2-byte opcode header, and `memorize_send_correlation=<n>` when a prior `CanStartMemming` success was observed.
- Introspection logs default to the allowlisted opcode `0x7dfc` / `OP_ClientUpdate`, and the allowlist can be overridden locally with exact opcode names and/or numeric values such as `MONOMYTH_RECV_INTROSPECT_OPCODES=OP_ClientUpdate,OP_ServerAuthStats` or `MONOMYTH_RECV_INTROSPECT_OPCODES=0x7dfc,0x1338` for targeted experiments.
- High-volume opcode `0x5089` is intentionally not in the default allowlist, so it remains metadata-only unless a developer explicitly overrides the local allowlist.

Future packet observation must continue to route through this module and stay strictly non-mutating.

## Receive dispatcher discovery

The `ReceiveDispatchDiscovery` module (`src/receive_dispatch_discovery.h` / `src/receive_dispatch_discovery.cpp`) is a fail-closed static discovery scaffold for the validated ROF2 receive dispatcher candidate at preferred VA `0x004c3250` / RVA `0x000c3250`.

Discovery runs only after the existing fingerprint/capability manifest path says enhancement discovery is allowed. It resolves the validated candidate relative to the loaded `eqgame.exe` module base at runtime, verifies that the resolved address still falls inside the loaded image, and only then applies layered executable-image checks. Mandatory checks remain fail-closed and include candidate range, executable-section membership, entry-adjacent dispatch-like compare/branch evidence, compare-tree evidence near the known dispatcher case-tree RVA, bounded unknown-message path evidence near the known fallback path RVA, and the two known direct feeder callsites. The `ret 0x10` epilogue is logged as advisory evidence using a bounded scan near the known epilogue RVA rather than a small entry-adjacent window because this dispatcher is large and its epilogue sits far from the entry. If runtime module discovery fails, if the resolved RVA falls outside the loaded image, or if any required structural check fails or cannot be evaluated confidently, the result is `failed` or `skipped_by_capability` and packet hooks remain disabled.

Discovery is not a pure RVA lookup. The validated runtime target is still `module_base + 0x000c3250`, but that address is accepted only when the layered structural checks agree. Startup logging now records the final state plus named check results such as `pass`, `fail`, `advisory_pass`, `advisory_fail`, or `skipped`.

This module does **not** install hooks, detours, callbacks, or memory patches. It does **not** observe, read, copy, decode, log, mutate, or retain any live packet data. A validated discovery result is only one prerequisite for the explicit dev-gated receive-only hook; by itself it does not activate `PacketObserver`, and `packet_hooks_allowed` remains `false`.

## Spell usability discovery

The `SpellUsabilityDiscovery` module (`src/spell_usability_discovery.h` / `src/spell_usability_discovery.cpp`) is a fail-closed read-only discovery scaffold for five ROF2 spell usability and memorize-send candidates:

- `CInvSlot::HandleRButtonUp`
- `EQ_Spell::GetSpellLevelNeeded`
- `EQ_Character::IsClassUsablePredicate`
- `CSpellBookWnd::CanStartMemming`
- `MemorizeSendPacketWrapper`

This discovery path is disabled by default and runs only when the existing ROF2 fingerprint/host guard passes and the local developer explicitly sets:

```powershell
$env:MONOMYTH_ENABLE_SPELL_USABILITY_DISCOVERY = "1"
```

The implementation does not guess RVAs. It keeps the existing ROF2 fingerprint gate, skips live `eqgame.exe` export-table discovery for the four known non-export targets in this slice, resolves pinned targets only from checked-in cleanroom RVAs plus runtime byte/call-shape validation, and otherwise fails closed with `missing_cleanroom_target` until a checked-in cleanroom locator exists for the target:

- `GetSpellLevelNeeded` resolves from fingerprint-gated cleanroom RVA `0x000af700` and exact byte-shape validation.
- `CInvSlot::HandleRButtonUp` resolves from cleanroom RVA `0x00297250`, requires the checked-in ROF2 SHA-256 `2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e`, and validates its pinned entry-byte prefix before any trace hook install.
- `EQ_Character::IsClassUsablePredicate` resolves from cleanroom RVA `0x000a1f50`, requires that same checked-in ROF2 SHA-256, and validates its exact entry bytes before any trace hook install.
- `CSpellBookWnd::CanStartMemming` resolves from fingerprint-gated cleanroom RVA `0x0035bd40`, exact cleanroom entry-byte validation, and validation that its known callsite still targets `GetSpellLevelNeeded`.
- `MemorizeSendPacketWrapper` resolves from cleanroom RVA `0x004c51f0`, requires that same checked-in ROF2 SHA-256, validates its exact entry bytes, and validates that the `StartSpellScribe`-like callsite at `0x0035e6fb` still targets the wrapper.
- `CanEquip` remains intentionally unpinned and disabled.

Per-target discovery states are `not_attempted`, `found_unvalidated`, `validated`, or `failed`. The logs now also include `enabled`, `evidence_source`, `module_base`, `rva`, `address`, `validation`, `failure_reason`, `resolver`, and `packet_id`. Expected `evidence_source` values include `fingerprint_rva`, `runtime_export`, `cleanroom_rva`, and `unavailable`. Discovery only logs evidence. It does not patch spell records, change return values, mutate UI state, hook `CastSpell`, or change packets. The checked-in locator notes for the current pinned spell targets live in `docs/cleanroom-dll-research/eqgame-spell-ui-ghidra-notes.md`.

## Passive spell usability trace

If a target reaches validated trace-safe state, the local developer can additionally enable passive call observation with:

```powershell
$env:MONOMYTH_ENABLE_SPELL_USABILITY_TRACE = "1"
```

Passive spell usability tracing is separate from packet hooks. It installs only for validated targets, calls the original function, logs the original arguments/result in a rate-limited way, and returns the original value unchanged. If validation is incomplete or detour installation is ambiguous, tracing stays disabled for that target.

## Memorize-send trace

To determine whether a spellbook memorize attempt actually reaches the outbound `OP_MemorizeSpell` send wrapper after `GetSpellLevelNeeded` and `CanStartMemming`, set:

```powershell
$env:MONOMYTH_ENABLE_MEMORIZE_SEND_TRACE = "1"
```

This trace is separate from `MONOMYTH_ENABLE_PACKET_HOOKS=1` and separate from `MONOMYTH_ENABLE_SPELL_USABILITY_TRACE=1`. When enabled, it installs exactly one trace-only detour on the validated opcode-aware send wrapper at runtime `module_base + 0x004c51f0` (preferred VA `0x008c51f0` when the image base is `0x00400000`), safely attempts to decode only the first 2 bytes of the outgoing buffer as the opcode, derives `payload_length` from the wrapper's total-length argument when decode succeeds, and logs lines such as:

```text
PacketObserverSend seq=1 target=MemorizeSendPacketWrapper wrapper_address=0x008c51f0 source_context=0x00dd25ac packet_pointer=0x01a2f1d0 total_length=18 decode_status=decoded opcode=8572 opcode_hex=0x217c opcode_name=OP_MemorizeSpell payload_length=16 memorize_opcode_match=true original_result=true memorize_send_correlation=1
```

This slice does not rewrite the packet buffer, does not alter the total length, does not alter the call ordering, and still calls the original client wrapper unchanged exactly once. Existing `SpellUsabilityTrace target=CanStartMemming ... memorize_send_correlation=<n> send_observation=pending` lines can be paired with `PacketObserverSend ... memorize_send_correlation=<n>` to show that the wrapper was reached. If the packet pointer is null, too short to contain an opcode, or faults during the bounded opcode read, the log stays bounded with `decode_status=not_decoded` plus a concrete `not_decoded_reason=...` and the companion trace emits `SpellUsabilityTrace target=MemorizeSend status=not_decoded ...`. If `CanStartMemming` succeeds and the next observed event is another `CanStartMemming` or shutdown with no matching send observation, the DLL emits `SpellUsabilityTrace target=MemorizeSend status=not_observed ...` so the absence is grep-friendly. Non-matching correlated sends now additionally log a bounded `packet_prefix_hex`, `caller_site_label` / `caller_site_validation` when the immediate wrapper caller matches one of the currently pinned downstream branches at return RVAs `0x13e1cd` or `0x18caba`, and up to two resolved local relative-call targets from that caller window as `caller_call_1_*` / `caller_call_2_*`. When a resolved target is only a hotpatch jump thunk, the same log line also includes `caller_call_*_terminal_target`, `caller_call_*_terminal_target_rva`, and `caller_call_*_terminal_hop_count`.

## Scroll-scribe trace

To investigate why extra-class spell scrolls stop before `GetSpellLevelNeeded`, set:

```powershell
$env:MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE = "1"
```

This trace slice is separate from `MONOMYTH_ENABLE_SPELL_USABILITY_TRACE=1`. When enabled, it installs trace-only detours for validated `CInvSlot::HandleRButtonUp` and `EQ_Character::IsClassUsablePredicate`, always calls the original client functions, and logs grep-friendly lines beginning with:

- `ScrollScribeTrace target=CInvSlot::HandleRButtonUp`
- `ScrollScribeTrace target=IsClassUsablePredicate`

Each log line includes a bounded monotonic `correlation` value, raw pointer-safe context only, the current assigned class-mask snapshot, the target discovery `evidence_source`, and `scroll_hint=unknown` when safe item/spell identification is not proven. Existing `SpellUsabilityTrace` logs for `GetSpellLevelNeeded` and `CanStartMemming` remain independently available and now append `scroll_scribe_correlation=<n>` when they occur inside a traced right-click flow.

Runtime interpretation for the trace:

- `ScrollScribeTrace target=CInvSlot::HandleRButtonUp` with no downstream `IsClassUsablePredicate` or `SpellUsabilityTrace` on the same correlation means the observed right-click did not traverse the expected downstream gate path.
- `HandleRButtonUp` plus `IsClassUsablePredicate`, but no `GetSpellLevelNeeded`, indicates a pre-level class-usability gate blocked the flow or that later archaeology should stay inside `HandleRButtonUp`.
- `GetSpellLevelNeeded` with a correlated extra-class level selection, but no later successful scribe behavior, indicates a later gate after the spell-level check.
- If a Paladin right-click logs `HandleRButtonUp` but not `IsClassUsablePredicate` or `GetSpellLevelNeeded`, continue archaeology inside `HandleRButtonUp`.
- If a Paladin right-click logs `IsClassUsablePredicate` rejecting the Paladin class, the next behavior slice should be a narrow predicate override scoped by call context if that context can be proven safely.
- If a Paladin right-click reaches `GetSpellLevelNeeded`, selects Paladin level, and still fails to scribe, investigate the later send/scribe gate rather than the class predicate.
- `CanStartMemming` is only expected after the spell reaches the spellbook path; it should not appear for scrolls that never scribe.

## Multiclass spell usability

Active multiclass spell usability requires the spell usability discovery gate plus a separate explicit opt-in:

```powershell
$env:MONOMYTH_ENABLE_PACKET_HOOKS = "1"
$env:MONOMYTH_ENABLE_SPELL_USABILITY_DISCOVERY = "1"
$env:MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY = "1"
```

The packet hook opt-in is needed for live `OP_ServerAuthStats` capture. When enabled, the `GetSpellLevelNeeded` hook reads the latest captured `statClassesBitmask`, calls the original client function for each assigned class bit 1-16, and returns the lowest valid required level. Missing, empty, invalid, or unusable masks fall back to the original requested-class result. This v1 intentionally does not enable `CanStartMemming` behavior overrides or any `CastSpell` hook.

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

`MONOMYTH_ENABLE_RECV_INTROSPECTION=1` is optional and separate. Without it, the hook skips generic payload-prefix introspection; the read-only `OP_ServerAuthStats` handler remains the only opcode-specific payload decode. With it, bounded payload-prefix reads are still fail-closed and limited to the allowlisted opcode set. The allowlist defaults to `0x7dfc` / `OP_ClientUpdate` and can be overridden locally with exact opcode names and/or numeric values such as `MONOMYTH_RECV_INTROSPECT_OPCODES=OP_ClientUpdate`, `MONOMYTH_RECV_INTROSPECT_OPCODES=OP_ServerAuthStats,OP_ManaUpdate,0x7dfc`, or `MONOMYTH_RECV_INTROSPECT_OPCODES=0x7dfc,0x1338`. Invalid tokens are ignored and logged during initialization; if every configured token is invalid, introspection stays enabled but the configured allowlist is empty.

The packet-hook opt-in is not sufficient by itself. The hook installs only when DirectInput proxy bootstrap is ready, the ROF2 fingerprint guard passes, receive dispatcher discovery validates the known candidate, and `MONOMYTH_ENABLE_PACKET_HOOKS=1` is present. If any gate fails, if the detour cannot be installed cleanly, or if the dispatcher prologue is ambiguous, packet observation is disabled and the DLL continues proxy-only behavior where possible.

The receive hook boundary is the validated dispatcher candidate at runtime `module_base + 0x000c3250` (preferred VA `0x004c3250`). It preserves the validated `this`/ECX plus four stack argument shape: source/context pointer, opcode/message id, payload pointer, and payload length. Outside the optional bounded introspection mode, the receive hook never reads from the payload pointer and never retains it. The separate memorize-send trace hook is limited to the validated `OP_MemorizeSpell` wrapper described above and remains non-mutating.

## Future slices

Future work can add versioned Monomyth client/server projection and guarded hook installation behind the internal capability manifest. Packet and UI capabilities should remain disabled until a future slice explicitly enables them. That work should remain explicit, versionable, and separate from server-authoritative gameplay logic.
