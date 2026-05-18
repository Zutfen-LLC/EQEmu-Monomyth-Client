# Handoff: Multiclass Spell Usability Runtime-Proven End-to-End

## Scope

This work completed the clean-room PAL secondary-class spellbook scribe fix for the ROF2 client hook DLL and closed the follow-up memorize/cast investigation. Runtime proof now shows multiclass spell usability working end-to-end: secondary-class spells can be scribed, memorized, and cast successfully on live reproduces.

Repo:
- `/home/zutfen/code/EQEmu-Monomyth-Client`

Runtime log under test:
- `/home/zutfen/everquest_rof2/monomyth-client.log`

Related local references:
- MQ2 source: `/home/zutfen/Desktop/MQ2Emu_ROF2_Legacy_Source/`
- Ghidra notes: `/home/zutfen/monomyth_ghidra/out/right_click_chain_update_verified_new_exe.md`
- Repo spell UI notes: `docs/cleanroom-dll-research/eqgame-spell-ui-ghidra-notes.md`
- Multiclass intervention map: `docs/multiclass-identity-intervention-map.md`

Current branch:
- `main`

## What Is Now Proven

### 1. Server-side class-mask support was never the blocker

Earlier cross-repo audit already established the EQEmu server supports multiclass spell eligibility via class-mask-aware level selection. This branch stayed on the clean-room client side.

### 2. PAL secondary-class spellbook scribing was blocked by an early native class-mask gate

The spellbook click path was traced through these clean-room targets:
- `SpellbookDispatcher`
- `StartSpellScribePath`
- `StartSpellScribePrecheckModeGetter`
- `StartSpellScribePrecheckGate`
- `StartSpellScribePrecheckLookup`
- nested helper traces inside `0x44c430`

The failing PAL reproduce proved:
- `SpellbookDispatcher` was reached
- `StartSpellScribePath` was reached
- mode check returned `20` / `0x14`
- `StartSpellScribePrecheckGate` returned `false`
- the path died before `GetSpellLevelNeeded` and before any scribe packet activity

Deeper gate tracing then proved the exact reject:
- assigned spell mask was `0x6`
- resolved current class was `13`
- computed current-class bit was `0x1000`
- native class-mask test failed
- no later `4462c0 / 446190 / 446200 / 446380` helper had run yet

So the blocker was the early `test edx, eax` style native-class gate inside `0x44c430`, not a downstream spell level check and not packet send failure.

### 3. The clean-room fix is a narrow gate override, not a broad spoof

The fix lives in `StartSpellScribePrecheckGateHook` and only overrides the proven early reject when all of these are true:
- multiclass spell usability is enabled
- the original gate returned `false`
- `require_known_like != 0`
- none of the later `4462c0 / 446190 / 446200 / 446380` rule helpers ran
- the current class bit missed the spell's native class mask
- authoritative `server_auth_stats` mask still intersects the spell's native mask

That makes the behavior change fail-closed and specific to the proven PAL secondary-class scribe reject.

### 4. The PAL scribe fix is runtime-proven

On the successful PAL reproduce, the log showed:
- `StartSpellScribePrecheckAssignedMaskGetter` returned `0x6`
- `StartSpellScribePrecheckGate` logged:
  - `original_result=false`
  - `returned_result=true`
  - `behavior_override_applied=true`
  - `class_id=13`
  - `class_bit=0x1000`
  - `authoritative_mask=0x1044`
  - `authoritative_mask_intersection=0x4`
- `StartSpellScribePrecheckLookup` then ran
- `StartSpellScribePath` mutated state instead of stalling
- the client sent `OP_MemorizeSpell`

This proves the early class-mask reject was the blocker and the narrow override cleared the real path.

### 5. `GetSpellLevelNeeded` is already selecting the right secondary class downstream

Later logs show `GetSpellLevelNeeded` choosing the PAL class for the class-13 reproduce:
- `assigned_mask=0x1044`
- `original_level=255`
- `selected_class=3`
- `selected_level=42`

So downstream spell-level selection is already class-mask aware enough for this case once the earlier gate is bypassed.

### 6. Secondary-class memorize and cast are now runtime-proven, not still open

Later live gameplay proof closed the remaining question. After swapping from `MAG/PAL/MNK` to `MAG/PAL/SHD`, the client successfully:
- scribed secondary-class SHD spells
- memorized secondary-class SHD spells
- cast those SHD spells successfully

So there is no longer an active gameplay defect in the multiclass spell usability slice. The remaining earlier memorize investigation was a trace-model correction problem, not an unresolved player-facing failure.

### 7. `StartSpellMemorizationPath` is a controller-style wrapper, not the real spellbook-window sender

Ordinary memorize tracing now proves the long-standing object/signature suspicion was correct:
- `StartSpellMemorizationPath` (`0x262290`) does run
- but its `controller_this` does not match the real spellbook window
- the real spellbook window state does not mutate there

So `0x262290` is useful as an upstream wrapper/correlation seam, but it is not the final ordinary memorize sender.

### 8. `SpellbookMemorizeSendPath` is the real ordinary memorize send/reset path

The latest reproduce proved the real spellbook-window path:
- before the decisive send, `SpellbookMemorizeSendPath` saw real pending state on the spellbook window:
  - `state_234=0x6`
  - `state_238=0xd8`
  - `state_23c=0x2`
  - `state_240=0xffffffff`
- that first call returned `21`, which behaves like a remaining-delay/countdown style result rather than a final commit result
- the actual outbound `OP_MemorizeSpell` send was then observed from wrapper caller `0x35dd4f`
- the new `MemorizeSendObserved` trace labeled that caller as `SpellbookMemorizeSendPathWrapperCallsite`
- immediately after the send, `SpellbookMemorizeSendPath` ran again and reset:
  - `state_234=0xffffffff`
  - `state_238=0xffffffff`
  - `state_23c=0x0`

So the real ordinary memorize send/reset path is `0x35db20`, not `MemSpellCommitPath`.

### 9. Historical note: `MemSpellCommitPath` fast-exited, but it was not the blocker for successful ordinary memorize

Current proven behavior on the successful ordinary memorize reproduce:
- `CanStartMemming` succeeds
- `StartSpellMemorizationPath` runs as an upstream controller wrapper
- `SpellbookMemorizeSendPath` sees the real pending state and later drives the real `OP_MemorizeSpell` send
- `MemSpellCommitPath` still fast-exits with `inferred_exit_reason=state_240_unset_fast_exit`
- despite that fast-exit, the live packet send and recv echo still happen

That historical proof is preserved here for context, but the `MemSpellCommitPath` and `PostCanStartMemmingFollowupGate` traces were removed from the branch after they were proven non-authoritative and noisy.

## Instrumentation Present On This Branch

Existing important seams now on branch:
- `GetSpellLevelNeeded`
- `CanStartMemming` trace
- `StartSpellMemorizationPath` trace
- outbound `OP_MemorizeSpell` send observation
- `MemorizeSendObserved`
- `SpellbookMemorizeSendPath`
- dev-gated full packet trace mode
- `SpellbookDispatcher`
- `StartSpellScribePath`
- `StartSpellScribePrecheckModeGetter`
- `StartSpellScribePrecheckGate`
- `StartSpellScribePrecheckLookup`
- `StartSpellScribePrecheckFastAccept`
- `StartSpellScribePrecheckAssignedMaskGetter`
- nested precheck rule helpers:
  - `StartSpellScribePrecheckRule4462c0`
  - `StartSpellScribePrecheckRule446190`
  - `StartSpellScribePrecheckRule446200`
  - `StartSpellScribePrecheckRule446380`

Durable guardrails to keep in mind:
- the actual behavior hooks are still `GetSpellLevelNeeded` and `StartSpellScribePrecheckGateHook`
- the other seams in this list are investigative traces, not proof of an active open defect
- if a future cleanup wants a smaller runtime trace surface, these investigative seams are the right first candidates to retire

Important nuance:
- the direct `StartSpellScribePrecheckClassResolver` detour does not install cleanly on this executable because the prologue is unsupported
- class resolution is instead emulated inside `StartSpellScribePrecheckGateHook` with guarded reads
- that emulated path is what produced the successful `class_id=13` proof and the shipped override decision
- the old `PostCanStartMemmingFollowupGate` and `MemSpellCommitPath` traces were removed rather than left as dead or misleading code
- the real ordinary memorize send/reset proof on this branch lives at `SpellbookMemorizeSendPath` plus `MemorizeSendObserved`

Relevant runtime behavior:
- no spell-usability env flags are required anymore
- validated ROF2 multiclass spell usability is expected native behavior now
- packet and trace opt-ins were retired after the issue was proven and closed

## Files Touched For The PAL Scribe Fix

Primary code files:
- `src/hook_manager.cpp`
- `src/runtime_capabilities.cpp`
- `src/runtime_capabilities.h`
- `src/spell_usability_discovery.cpp`
- `src/spell_usability_discovery.h`

Docs:
- `docs/handoff.md`

## Current Worktree State

Current checkout:
- `main`

Untracked local items currently present but not part of this slice:
- `.agents/`
- `docs/multiclass-identity-intervention-map.md`
- `docs/specs/`
- `worktrees/`

Build status:
- `cmake --build build-cross-i686` passed for the current patch

Runtime deploy status:
- latest built `build-cross-i686/dinput8.dll` was copied to `/home/zutfen/everquest_rof2/dinput8.dll`
- successful PAL reproduce was done with that deployed DLL
- successful ordinary memorize reproduce was also done with the latest `MemorizeSendObserved` trace patch
- later live gameplay also proved successful SHD secondary-class scribe, memorize, and cast behavior

## Status

- Treat the multiclass spell usability fix as complete unless a fresh contradictory reproduce appears.
- Do not reopen the ordinary memorize investigation without a real gameplay failure.
- Treat the remaining trace-rich seams as optional diagnostics, not as evidence of unfinished behavior.

## Optional Follow-Ups

- If desired, reduce the investigative trace surface now that the issue is closed.
- If desired, relabel or better document `StartSpellMemorizationPath` to reflect its controller-wrapper role more explicitly.
- If a future gameplay regression appears, start from `SpellbookMemorizeSendPath` and `StartSpellScribePrecheckGateHook`, not from the removed `MemSpellCommitPath` theory.

## Constraints To Preserve

- Stay clean-room. Do not import THJ or classless DLL gameplay code.
- Keep future spell usability work trace-first unless a fresh real failure is reproduced.
- Do not broaden into `CastSpell`, profile spoofing, or spell-record mutation without direct runtime evidence.
- Preserve the PAL scribe fix's fail-closed behavior; do not generalize it into a wide class spoof or unconditional allow.
- Treat missing startup capability receipts as a deployment problem first, not a logic conclusion.
- Do not reintroduce `MemSpellCommitPath` or `PostCanStartMemmingFollowupGate` tracing without fresh contradictory evidence that the current proven send path is insufficient.

## Suggested Skills For The Next Session

- `handoff`
  - to refresh this document again only if a new spell usability issue appears
