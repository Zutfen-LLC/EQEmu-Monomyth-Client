# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project currently tracks changes under an `Unreleased` section until versioned releases begin.

## [Unreleased]

### Changed

- Spell/UI discovery no longer uses live `eqgame.exe` export lookup as the final path for the five known ROF2 non-export targets; it now resolves `GetSpellLevelNeeded` from fingerprint-gated cleanroom RVA `0x000af700` plus exact byte validation, resolves trace-only `CanStartMemming` from cleanroom RVA `0x0035bd40` plus exact entry-byte and callsite validation, hard-denies unresolved sibling targets with `missing_cleanroom_target`, emits per-target receipts with `enabled`/`evidence_source`/`module_base`/`rva`/`address`/`validation`/`failure_reason`, and logs a visible `CLIENT-SPELL-UI-DISCOVERY-FIX-V3` startup marker plus resolver version.
- Win32 MSVC builds now statically link the C/C++ runtime for `dinput8.dll` (`/MT` in `Release`, `/MTd` in `Debug`) so the client DLL no longer depends on `MSVCP140.dll` or `VCRUNTIME140.dll` at loader startup.
- Receive dispatcher discovery now resolves the validated ROF2 dispatcher candidate as runtime `module_base + 0x000c3250`, logs the runtime module base and resolved candidate address, and still fails closed if the resolved address falls outside the loaded image or structural validation does not match.
- Receive dispatcher discovery now validates the large ROF2 dispatcher with layered named checks: mandatory range, entry/compare-tree, unknown-message path, and feeder-call evidence remain fail-closed, while `ret 0x10` epilogue evidence is logged as bounded advisory diagnostics near the known epilogue RVA instead of being required in a small entry-adjacent scan window.
- The default receive-introspection allowlist is now narrowed to opcode `0x7dfc`; opcode `0x2958` is no longer introspected by default and remains available only through the existing local override environment variable for targeted experiments.
- Receive introspection allowlist overrides now accept exact ROF2 opcode names from the local reference table as well as numeric opcode ids, while keeping the default allowlist at `0x7dfc` / `OP_ClientUpdate`.
- Memorize-send discovery failure evidence now includes the live wrapper entry bytes and the pinned caller's live opcode/resolved target alongside expected values, so `unsupported_prologue` failures are directly diagnosable without weakening validation.
- Memorize-send discovery now pins `MemorizeSendPacketWrapper` to cleanroom RVA `0x004c51f0`, aligning the checked-in cleanroom notes with the live caller-resolved target and removing the stale `0x004c41f0` typo.

### Added

- Dev-gated trace-only `OP_MemorizeSpell` send observation behind `MONOMYTH_ENABLE_MEMORIZE_SEND_TRACE=1`, using a validated cleanroom `MemorizeSendPacketWrapper` target, correlated `CanStartMemming` pending/send-absent receipts, a dedicated memorize-send startup marker, bounded `not_decoded` handling for null/short/faulting packet reads, and richer `PacketObserverSend` logs without packet mutation or send-order changes.
- Correlated post-`CanStartMemming` mismatch receipts now include the observed opcode name plus caller return RVA and a bounded caller-byte window, making downstream post-spellbook flow identification possible without adding a second speculative hook.
- Post-`CanStartMemming` correlation no longer fails closed on the first non-`OP_MemorizeSpell` wrapper send; it now keeps a bounded watch window across several subsequent wrapper sends, logs each intermediate send with correlation metadata, and only emits `not_observed` once the bounded window is exhausted or another gap condition closes the correlation.
- Correlated post-`CanStartMemming` intermediate-send receipts now include a bounded outgoing packet prefix plus exact-match labels for the two observed downstream caller branches (`0x13e1cd` and `0x18caba`), so later logs can distinguish stable branch reuse from caller-shape drift without installing another hook.
- Correlated post-`CanStartMemming` intermediate-send receipts now also resolve and log up to two relative `e8` call targets from the bounded caller-byte window, labeling the wrapper call separately from the follow-up call so the next downstream function behind `0x18cab5` / `0x13e1c8` is visible without a second detour.
- Correlated post-`CanStartMemming` call-target logging now follows bounded hotpatch jump-thunk chains, so thunked follow-up targets such as `0x8db151` also report their terminal target and hop count instead of stopping at the first stub.
- Memorize-send tracing now validates and installs a second trace-only downstream hook at the real post-`CanStartMemming` follow-up gate body `0x8dc0f5` / RVA `0x4dc0f5`, reached through the `0x18cabd -> 0x8db151` thunk chain, and logs its argument pointer, caller return site, and original result without changing control flow.
- Post-`CanStartMemming` follow-up gate validation now accepts the bounded proven two-hop hotpatch thunk chain `0x8db151 -> 0x8db146 -> 0x8dc0f5`, following the second hop by hotpatch-thunk shape instead of requiring byte-identical thunk bodies or a direct first-hop landing on the real body.
- Memorize-send tracing now also validates and installs a trace-only hook on the real spellbook-side `MemSpellCommitPath` at `0x75e620` / RVA `0x35e620`, the function that contains the known `OP_MemorizeSpell` packet construction and wrapper call. Correlated logs now show whether that path is reached at all after `CanStartMemming`.
- `MemSpellCommitPath` logging now uses guarded pointer copies for spellbook-state offsets and the live memorize-context pointer instead of raw dereferences, keeping spell-bar removal and other non-mem contexts fail-closed rather than crashing inside the trace path.
- Cross-platform unit coverage for spell/UI discovery decision policy, including fingerprint-backed validation without runtime exports, missing-cleanroom-target denial, fingerprint mismatch denial, diagnostic-string gating, and independent target outcomes.
- Checked-in cleanroom Ghidra notes for pinned `eqgame.exe` spell/UI locator candidates in `docs/cleanroom-dll-research/eqgame-spell-ui-ghidra-notes.md`.
- Dev-gated scroll-scribe trace instrumentation behind `MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE=1`, with fail-closed discovery/validation for `CInvSlot::HandleRButtonUp` and `EQ_Character::IsClassUsablePredicate`, exact ROF2 SHA-256 gating plus entry-byte validation for both pinned RVAs, and correlation-friendly trace logs that do not alter client behavior. `CanEquip` remains intentionally unpinned and disabled.
- Dev-gated multiclass spell usability behavior behind `MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY=1`, using validated `EQ_Spell::GetSpellLevelNeeded` discovery and the latest `OP_ServerAuthStats` class mask to select the lowest valid original client-required level across assigned classes.
- Helper-level coverage for class-mask spell-level selection, including no/empty/invalid masks, shared Magician+Paladin selection, unassigned-class ignoring, sentinel filtering, and no-valid-class fallback.
- Read-only receive handler for THJ `OP_ServerAuthStats` (`0x1338`) that parses the minimal server-authored stat payload, captures only `statClassesBitmask` in internal DLL state, and logs valid/malformed diagnostics without packet mutation, UI behavior, or client-memory writes.
- ROF2 opcode reference entry for THJ `OP_ServerAuthStats` (`0x1338`) from `C:\Code\THJ-Server-Original\utils\patches\patch_RoF2.conf`, enabling exact-name receive-introspection allowlist overrides and exact opcode routing for the read-only handler.
- Fail-closed ROF2 fingerprint byte-scan fallback for `eqgame.exe` that runs once at startup when version resources are unavailable or inconclusive and matches only when both known markers `May 10 2013` and `23:30:08` are present.
- Capability manifest fingerprint method reporting (`version_resource`, `byte_scan`, or `unavailable`) to make startup guard results grep-friendly.
- Fail-closed receive dispatcher discovery scaffold for the validated ROF2 candidate at VA `0x004c3250` / RVA `0x000c3250`, with layered static structural checks and one startup log line.
- Runtime capability manifest fields for receive dispatcher discovery state, validated candidate RVA/address, and PacketObserver state reporting while keeping `packet_hooks_allowed=false`.
- Dev-only receive dispatcher hook gated by `MONOMYTH_ENABLE_PACKET_HOOKS=1`, ROF2 fingerprint validation, and receive dispatcher discovery validation.
- Metadata-only `PacketObserverRecv` logging for opcode/message id, payload length, and source/context pointer, capped to the first 50 packets and every 500th packet after that.
- Dev-only bounded receive payload-prefix introspection gated separately by `MONOMYTH_ENABLE_RECV_INTROSPECTION=1`, layered on top of `MONOMYTH_ENABLE_PACKET_HOOKS=1`, limited to an allowlisted opcode set, capped at a 16-byte hex prefix, and rate-limited independently from metadata logging.
- Static ROF2 opcode-name reference enrichment derived from EQEmu's `patch_RoF2.conf`, adding `opcode_name` to receive metadata, introspection, and introspection-skip logs without changing hook gates or packet handling.
- GitHub Actions CI workflow for 32-bit Windows builds on pushes to `main` and pull requests.
- Build artifacts upload for `Debug` and `Release` `dinput8.dll` outputs.
- Rolling GitHub prerelease publishing on `main` so the newest `dinput8.dll` and `dinput8-Debug.dll` are always downloadable from Releases.
- Repository changelog for tracking notable changes over time.

### Documentation

- README documentation for the trace-only `MONOMYTH_ENABLE_MEMORIZE_SEND_TRACE=1` gate, the validated `OP_MemorizeSpell` wrapper seam, correlation semantics, and expected `PacketObserverSend` log format.
- README documentation for the new scroll-scribe trace gate, target set, correlation log format, and runtime interpretation matrix for right-click gate vs item usability gate vs spell-level gate vs spellbook gate.
- README documentation for the multiclass spell usability opt-in, expected `GetSpellLevelNeeded` selection behavior, and intentional absence of `CanStartMemming` behavior overrides or `CastSpell` hooks in v1.
- README now documents the read-only THJ `OP_ServerAuthStats` handler, including minimal `Stat_Struct` parsing, key-based `statClassesBitmask` extraction, malformed-packet rejection, and the no-mutation/no-UI safety boundary.
- README documentation for the ROF2 fingerprint byte-scan fallback, required dual-marker match, and `fingerprint_method` capability logging.
- README documentation for receive dispatcher discovery, layered fail-closed validation, advisory epilogue handling for the large dispatcher, and the no-hooks/no-packet-data safety boundary.
- README documentation for the unsafe local packet-hook opt-in, receive-only observation, no send interception, and rate-limited logging.
- README documentation for the separate receive-introspection opt-in, bounded 16-byte prefix logging, default opcode allowlist, and fail-closed payload safety checks.
- README documentation now reflects the hardened default receive-introspection allowlist and notes that `0x2958` can be re-added only through the existing local override for targeted experiments.
- README documentation now reflects exact-name receive-introspection allowlist overrides, mixed name/numeric examples, and invalid-token fail-closed behavior.
- README documentation for ROF2 `opcode_name` enrichment and unknown-opcode fallback behavior.
- README note describing the Windows CI workflow, artifact output, and rolling prerelease downloads.
- README build and troubleshooting documentation for static MSVC runtime linkage, `0xc0000142` startup failures, and `dumpbin` dependency verification.

### Fixed

- CI lint and AI review checks now exclude generated Graphify output artifacts.
