# Multiclass UI Display Handoff

Updated: 2026-05-21

## Goal

Show all assigned classes for the local player anywhere class text is displayed in the ROF2 client, starting with local/self surfaces only.

Authoritative source:
- `OP_ServerAuthStats`
- `statClassesBitmask`
- current confirmed test mask: `0x1044`
- current confirmed assigned classes for test character: `Paladin / Monk / Magician`

## Current Status

What is confirmed working:
- The server is sending `OP_ServerAuthStats`, and the client is receiving it.
- The client parses the authoritative multiclass mask correctly.
- Shared multiclass formatting works.
- At least one live UI surface already overrides correctly through the `GetClassThreeLetterCode` producer path.

What is still not working visibly:
- `/who` row output still shows the single native class.
- Inventory class title still shows the single native class.

## Important Evidence

### Server send is correct

Live server logs confirm the send path is working:
- `ServerAuthStatsSend char_name="Driton" ... class_mask=0x1044 ... wire_opcode=0x1338 ... client_version=RoF2`

This rules out stale zone binaries and server opcode mapping as the current blocker.

### Client receive is correct

The client log confirms receipt and decoding of who data and multiclass state.

Relevant 17:00 run lines:
- `hook_manager: WhoClassNameClassLookupCallsiteA callsite hook installed address=0x5364e7 ...`
- `hook_manager: multiclass UI display hook installed target=WhoClassName/GetClassDesc/GetClassThreeLetterCode local_self_only=true`
- `UiClassHelperTrace ... helper=GetClassThreeLetterCode caller_rva=0x2781a6 ... override_applied=true formatted="PAL/MNK/MAG"`
- `PacketObserverWhoAllResponseEntry ... class_id=3 ... name="Driton"`

Interpretation:
- multiclass state is present
- helper detour is live
- one 3-letter-code surface is proven
- `/who` still arrives as a normal single-class packet row and must be rewritten in a later producer/UI step

### THJ DLL findings changed the strategy

See [thj-decompile-results.md](/home/zutfen/code/EQEmu-Monomyth-Client/docs/thj-decompile-results.md).

Main conclusion:
- THJ does not appear to solve this by writing final UI controls directly.
- THJ centrally detours producer functions instead:
  - `/who` through `EQ_WhoClassName`
  - inventory / char-select-adjacent surfaces through `EQ_CharSelectClassNameFunc`

Recovered THJ replacements:
- `/who`: `FUN_10025510`
- char-select / inventory-adjacent: `FUN_10024920`

Important THJ clue:
- `FUN_10024920` has a special caller discriminator for client return RVA `0x6843ff`
- that branch returns abbreviated slash-joined class codes
- the general branch can return full slash-joined class names

## What We Have Implemented

### Shared formatter layer

Implemented in:
- [src/multiclass_identity.h](/home/zutfen/code/EQEmu-Monomyth-Client/src/multiclass_identity.h)
- [src/multiclass_identity.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/multiclass_identity.cpp)

Includes:
- primary-first ordering
- duplicate suppression
- full-name formatting
- 3-letter formatting
- ASCII output helpers

### Discovery / capability layer

Implemented in:
- [src/class_display_discovery.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/class_display_discovery.cpp)
- [src/runtime_capabilities.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/runtime_capabilities.cpp)

Current validated targets:
- `WhoClassName`
- `GetClassDesc`
- `GetClassThreeLetterCode`
- progression-selection writer seam currently stored in the `char_select_class_name_func` slot

Important caveat:
- the current `char_select_class_name_func` discovery slot is not proven to be THJ’s real `EQ_CharSelectClassNameFunc` producer seam
- it is still the previously recovered progression-selection writer seam

### Live hook layer

Implemented in:
- [src/hook_manager.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/hook_manager.cpp)

Current UI-related hook behavior:
- `GetClassDesc` inline detour
- `GetClassThreeLetterCode` inline detour
- `WhoClassName` internal class-lookup callsite patches
- progression selection `ClassValueLabel` callsite patch
- several inventory experiments, currently not producing visible change

## Live Binary Findings

These live-client call sites have now been confirmed:

### `0x514dc0` caller at `0x2781a6`

Confirmed by both logs and disassembly.

Behavior:
- currently reaches our `GetClassThreeLetterCode` hook
- successfully overrides to `PAL/MNK/MAG`

This is the one proven working display surface so far.

### `0x514dc0` caller at `0x6843ff`

Confirmed in the live `eqgame.exe` disassembly.

This exactly matches the THJ `EQ_CharSelectClassNameFunc` caller discriminator.

Disassembly pattern:
- `call 0x514dc0`
- return address `0x6843ff`

Interpretation:
- Hermes’ THJ result is real and maps to this client
- this is a concrete candidate for the abbreviated branch of the inventory / char-select-adjacent producer path

### `0x514dc0` caller at `0x18e554`

Confirmed in the live `eqgame.exe` disassembly and in logs:
- `UiClassHelperTrace ... caller_rva=0x18e554 ...`

Observed behavior in logs before the latest patch:
- `requested_class_id=1` or `3`
- `local_class_id=2`
- `has_assigned_mask=false`
- `override_applied=false`

Interpretation:
- this is a real full-name-ish local/self UI surface
- the old “only override when requested class == local primary” rule was too narrow here
- caller-specific semantic override was added to address this

## Latest Code Direction

The latest patch changes the `0x514dc0` producer hook from a naive class-id match rule to caller-based semantic rules.

Current caller-specific override mapping in [src/hook_manager.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/hook_manager.cpp):
- caller `0x002843ff` -> force `three_letter`
- caller `0x002781a6` -> force `three_letter`
- caller `0x0018e554` -> force `full_name`

Intent:
- treat `0x514dc0` as a semantic producer seam, like THJ did
- stop relying only on `requested_class_id == local_primary_class`
- allow the full-name local/self surface at `0x18e554` to override even when the transient caller-side class id is not stable

This change was source-updated but not compile-verified in this Linux environment.

## Dead Ends / Ruled-Out Approaches

These have been tried and did not produce the visible inventory class title:
- guessed inventory refresh function
- `CXWnd::SetWindowTextA`
- `CXStr` assign helper at `0x405d90`

The strongest current read is that these were the wrong layer, not that multiclass formatting was wrong.

## Most Recent 17:00 Run Summary

From `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `WhoClassName` callsite hooks installed
- `GetClassDesc` and `GetClassThreeLetterCode` UI hook set installed
- progression selection hook installed
- inventory class title hook installed
- only `GetClassThreeLetterCode` produced live helper traces
- `/who` still decoded as `class_id=3`

Most important lines:
- `UiClassHelperTrace count=1 helper=GetClassThreeLetterCode caller_rva=0x18e554 ... override_applied=false ...`
- `UiClassHelperTrace count=4 helper=GetClassThreeLetterCode caller_rva=0x2781a6 ... override_applied=true formatted="PAL/MNK/MAG"`
- `PacketObserverWhoAllResponseEntry ... class_id=3 ... name="Driton"`

## Best Next Steps

### 1. Compile and test the latest caller-based producer override

Expected success signal:
- `UiClassHelperTrace` for `caller_rva=0x18e554`
- `override_applied=true`
- `reason="known_local_self_full_name_caller_rva"`
- `formatted="Paladin/Monk/Magician"`

If that appears and the UI changes:
- inventory-adjacent full-name producer path is finally pinned

If that appears and the UI still does not change:
- a later overwrite exists after the producer return

If it does not appear:
- that surface is not using the currently detoured producer in the tested flow

### 2. Re-evaluate `/who` through the real producer seam, not only internal lookup callsites

Current suspicion:
- our `WhoClassName` callsite patch shape may still be too low-level or semantically wrong
- THJ detoured the producer function pointer directly, not an internal string lookup callsite

Evidence for this:
- the 17:00 run installed all `WhoClassName` callsite hooks
- but produced no `UiClassDisplayTrace` for `WhoClassName`
- `/who` still showed native single class

### 3. Stop conflating progression-selection writer with real `EQ_CharSelectClassNameFunc`

The current discovery slot for `char_select_class_name_func` should be treated cautiously.

It is currently useful as:
- a separate verified selected-entry class-value writer seam

It is not yet proven to be:
- the same producer seam THJ called `EQ_CharSelectClassNameFunc`

## Key Files

- [src/hook_manager.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/hook_manager.cpp)
- [src/class_display_discovery.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/class_display_discovery.cpp)
- [src/runtime_capabilities.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/runtime_capabilities.cpp)
- [src/multiclass_identity.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/multiclass_identity.cpp)
- [docs/thj-decompile-results.md](/home/zutfen/code/EQEmu-Monomyth-Client/docs/thj-decompile-results.md)

## Short Version

We have proven the multiclass state, formatter, and one 3-letter producer override. The remaining problem is surface coverage and seam choice, not authoritative data. THJ’s DLL strongly suggests the real solution is producer-level detours on `EQ_WhoClassName` and `EQ_CharSelectClassNameFunc`, and the live client confirms at least one exact THJ caller discriminator at `0x6843ff`. The newest code now pivots the `0x514dc0` hook toward caller-based semantic overrides to match that model.
