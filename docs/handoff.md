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
- `/who` now visibly overrides correctly through the live class-label seam at `0x477e6`.

What is still not working visibly:
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
- progression-selection class-value writer seam currently still stored in the `char_select_class_name_func` slot in code/logging

Important caveat:
- the current `char_select_class_name_func` discovery slot is not proven to be THJ’s real `EQ_CharSelectClassNameFunc` producer seam
- it is still the previously recovered progression-selection writer seam
- the current known failed seams are now tracked separately in [docs/multiclass-negative-results.md](/home/zutfen/code/EQEmu-Monomyth-Client/docs/multiclass-negative-results.md)

### Live hook layer

Implemented in:
- [src/hook_manager.cpp](/home/zutfen/code/EQEmu-Monomyth-Client/src/hook_manager.cpp)

Current UI-related hook behavior:
- `GetClassDesc` inline detour
- `GetClassThreeLetterCode` inline detour
- `WhoClassName` now uses an entry/context inline detour plus a filtered `WhoClassNameClassLookup` inline detour
- progression selection `ClassValueLabel` callsite patch
- inventory title interception experiments were retired after THJ/local evidence showed they were the wrong layer

Important `/who` update:
- the original `0x536310` wrapper-context theory was not the visible row seam
- the shared lookup at `0x7d0660` is still involved, but the real visible `/who` class-label caller is `0x477e6`
- that caller now overrides correctly during the bounded post-`OP_WhoAllResponse` correlation window

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

### `0x7d0660` caller at `0x477e6`

Confirmed by the bounded `/who` correlation trace in the live client.

Observed behavior in the `2026-05-21 18:47` run:
- `WhoAllClassDisplayTrace ... caller_rva=0x477e6 ... string_id=0x5e6 ... override_applied=true ... formatted="Paladin/Monk/Magician"`

Interpretation:
- this is the live visible `/who` class-label seam for the current client path
- `/who` should now be treated as solved unless later runtime evidence shows a regression

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

This change is now locally unit-tested for discovery/capability behavior in this Linux environment, but the actual `dinput8` target still is not compile-verified here because this host lacks Windows headers/tooling.

The same cleanup pass also split the capability gate correctly:
- the core local/self producer hooks no longer depend on the unproven progression-selection surrogate seam
- the progression-selection seam is still separate and still not treated as proof of real `EQ_CharSelectClassNameFunc`
- the old `/who` single-callsite patch was retired after live runs showed it never produced `UiClassDisplayTrace` output
- the replacement hook model now tracks the active `WhoClassName` subject at wrapper entry and only overrides the shared string lookup when the caller return RVA is one of the three internal `WhoClassName` lookup branches (`0x1364ec`, `0x1365c7`, `0x136606`)
- the final `/who` fix now additionally treats `caller_rva=0x477e6` as the live class-label seam and overrides it only during the post-`OP_WhoAllResponse` correlation window

## Dead Ends / Ruled-Out Approaches

These have been tried and did not produce the visible inventory class title:
- guessed inventory refresh function
- `CXWnd::SetWindowTextA`
- `CXStr` assign helper at `0x405d90`

The strongest current read is that these were the wrong layer, not that multiclass formatting was wrong.

The runtime now reflects that conclusion:
- the startup path no longer attempts the `CXStr::Assign` / `CXWnd::SetWindowTextA` inventory-title hook
- inventory title work should resume only once a real producer seam is pinned

## Most Recent Solved `/who` Run Summary

From `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `PacketObserverWhoAllCorrelationWindow activation=1 ... budget=48`
- `PacketObserverWhoAllResponseEntry ... class_id=3 ... name="Driton"`
- `WhoAllClassDisplayTrace ... caller_rva=0x477e6 ... override_applied=true ... formatted="Paladin/Monk/Magician"`

Interpretation:
- the class packet still arrives natively as `class_id=3`
- the visible `/who` class label is now rewritten at the live producer seam after the packet decode
- `/who` is no longer the active blocker

## Best Next Steps

### 1. Treat `/who` as solved and avoid reopening it without fresh contrary evidence

The useful live seam is now known:
- shared lookup target `0x7d0660`
- visible class-label caller `0x477e6`

### 2. Pivot fully to the inventory-window full-name surface

Current strongest hypothesis:
- the remaining inventory class title still needs a producer seam equivalent to THJ `EQ_CharSelectClassNameFunc`
- direct UI-text interception remains disproven
- the early `0x18e554` full-name path is still only inconclusive, not proven useful for the visible inventory title

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

We have proven the multiclass state, formatter, one 3-letter producer override, and the live `/who` class-label seam at `0x477e6`. The remaining problem is inventory-window surface coverage and seam choice, not authoritative data. THJ’s DLL still suggests the real remaining solution is a producer-level detour equivalent to `EQ_CharSelectClassNameFunc`, and the live client still confirms one exact THJ caller discriminator at `0x6843ff`.
