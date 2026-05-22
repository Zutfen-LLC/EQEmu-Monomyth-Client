# Handoff

## Read This First

This file is the current handoff for the repo root.

Start here before making changes:
- `HANDOFF.md`
- `docs/multiclass-negative-results.md`
- `src/hook_manager.cpp`

Also useful when tracing the remaining UI issue:
- `/home/zutfen/code/eqlib`
- `/home/zutfen/code/MacroQuest`
- prior clean-room Ghidra outputs under `docs/cleanroom-dll-research/`

## Current Status

Authoritative multiclass data is working.
- `OP_ServerAuthStats` is received and parsed correctly.
- `/who` is solved and visually correct.
- The known local/self abbreviated surface still works and renders `PAL/MNK/MAG`.

The remaining blocker is the inventory window summary pane in the top-left of the main inventory window.
- Target text block:
  - character name
  - level
  - class
  - deity
- User-visible example:
  - `Driton`
  - `1`
  - `Paladin`
  - `Tunare`
- Desired change:
  - class line should become multiclass-aware, either abbreviated or full-name style

This is not the item display / item inspection window.

## Important Corrections

Do not reopen these assumptions:
- `ItemDisplayWindow` is not the active blocker for the user’s screenshot.
- `InventoryWindow + 0x2cc/+0x2d0` is not the class-title path in this client.
- direct final-text interception (`CXStr::Assign`, `CXWnd::SetWindowTextA`) was the wrong layer for the inventory summary blocker

Relevant confirmed facts:
- `/who` was solved through shared lookup target `0x7d0660` with visible caller `0x477e6`
- `0x514dc0` is `GetClassDesc`
- `0x5153c0` is `GetClassThreeLetterCode`
- `0x515ce0` is `GetRaceDesc`
- `0x515f70` is `GetDeityDesc`
- `0x536310` is not a `/who` seam; it was a mislabeled surrogate and should not guide new inventory work
- the currently hooked progression-selection callsite at `0x3212b6` is cold in the active character-select repro and should not be treated as the live class-list seam
- THJ client-folder archaeology shows non-stock inventory UI XML with an added `IW_ClassAbbr` label using custom `EQType 6666`, so THJ inventory visuals are not clean proof of the stock ROF2 top-left class-label path

## Inventory Summary Findings

The inventory UI XML confirms the top-left summary block is label-driven:
- `/home/zutfen/everquest_rof2/uifiles/default/EQUI_Inventory.xml`
- relevant labels:
  - `IW_Name`
  - `IW_Level`
  - `IW_Class`
  - `IW_Deity`
- relevant `ScreenID`s:
  - `NameLabel`
  - `LevelClassLabel`
  - `DeityLabel`

This suggests a generic summary refresh path, not a dedicated item-display path.

Useful THJ contrast:
- `/home/zutfen/Desktop/thj/uifiles/default/EQUI_Inventory.xml` adds `IW_ClassAbbr` with `EQType 6666`
- multiple THJ UI themes repeat that same pattern
- quick text search found no obvious multiclass literals in `/home/zutfen/Desktop/thj/eqstr_us.txt` or `dbstr_us.txt`
- treat THJ as evidence for a custom UI-plus-hook route, not as proof that stock `EQType 3` inventory class rendering behaves the same way

Current trace hooks in `src/hook_manager.cpp`:
- `CInventoryWindow::WndNotification` at `rva=0x00293490`
- inventory summary candidate A at `rva=0x00290100`
- inventory summary candidate B at `rva=0x002905f0`
- inventory summary candidate C at `rva=0x00312b00`
- inventory summary candidate D / `CInventoryWnd::OnProcessFrame` at `rva=0x00292c90`
- late full-name `GetClassDesc` candidate callsites at `rva=0x003249ac`, `0x00324d84`, and `0x003252ab`

Candidate C is now disproven for the active repro.
- `0x712b00` is still a real binary-backed multi-label refresh function
- it contains:
  - `GetClassDesc` callsite at `0x712c50`
  - `GetRaceDesc` callsite at `0x712ca2`
- but repeated live inventory repros still produced no `InventorySummaryCandidateEntryTrace`
- treat it as a dead seam for the top-left inventory summary pane unless fresh contrary evidence appears

## Current Code State

Recent changes already in the repo:
- logger writes UTF-8
- item-display tracing is retired as the active blocker
- inventory trace install bug behind an early return was fixed
- inventory summary candidate C was added and later disproven in live repro
- shared inventory correlation tracing now preserves budget instead of spending it on unrelated `WhoClassNameClassLookup` traffic
- char-select now has a pragmatic display-only fallback: authoritative `statClassesBitmask` is cached per character under `Resources/monomyth-multiclass-cache.txt` when auth lands in-game, and `CXWnd::SetWindowTextA` can rewrite `Name [Level NativeClass]` captions to short-code multiclass text from that cache

Files to inspect first:
- `src/hook_manager.cpp`
- `docs/multiclass-negative-results.md`

Notable current rough edge:
- `GetDeityDesc` hook shape is still wrong / incomplete in `src/hook_manager.cpp`
- current typedef/hook still uses a `CDECL` model even though `eqlib` treats it as a `CEverQuest` member function
- live install had previously failed with `prologue unsupported` / `install_failed`
- do not assume fixing the calling convention alone will make inline detouring work, because `0x515f70` is a jump-table style function

## Latest Practical Direction

Current verdict:
- character-select full-name rendering still happens on the early pre-auth `GetClassDesc` caller `0x18e554`
- `OP_ServerAuthStats` arrives later with the correct mask `0x1044`, but no later full-name refresh seam has been observed
- the progression-selection callsite hook at `0x3212b6` is installed but stays cold in the active repro
- the validated `0x321210` `CharSelectClassNameFunc` entry trace now also stays completely cold in live repros, even after clean install logging confirmed `entry_trace=true callsite_patch=true`
- the later full-name helper callsite trio at `0x3249ac` / `0x324d84` / `0x3252ab` also installed cleanly at `2026-05-22 09:55`, but produced zero `CharSelectLateFullNameTrace` lines
- inventory summary candidate C at `0x712b00` is not the live seam for the active top-left summary repro
- `eqlib` plus direct vtable recovery pin `CInventoryWnd::OnProcessFrame` to `0x692c90` / `rva=0x292c90`
- the neighboring `Activate` slot resolves to the generic show wrapper pattern, so `OnProcessFrame` is the better inventory self-refresh seam
- current attempt to hook `OnProcessFrame` did not install: live startup at `2026-05-22 09:55:23` logged `InventorySummaryRefreshCandidateD trace prologue unsupported; hook disabled`

The next useful validation step is to repro the inventory summary pane and inspect the log for:
- `InventoryClassDisplayCorrelationWindow`
- `InventoryOnProcessFrameTrace`
- whether any later `UiClassHelperTrace` now survives after the shared-string budget-preservation change
- any fresh entry traces from the next inventory self-refresh seam once instrumented

For character select, the next useful step is:
- prefer validating the cache-backed caption fallback before reopening more seam archaeology
- if the cache fallback fails, inspect `CharSelectCachedClassCaptionTrace` and `MulticlassCachePersist` first
- otherwise keep deeper char-select seam work deprioritized behind the inventory summary blocker

Expected live log path:
- `/home/zutfen/everquest_rof2/monomyth-client.log`

Next seam direction:
- for inventory, bias toward `CInventoryWindow` self-refresh child seams inside the concrete `OnProcessFrame` flow at `0x292c90`, since the parent prologue is not patchable with the current inline detour helper
- prefer binary-backed summary producers over more generic UI callback guesses
- use `eqlib`, `MacroQuest`, and prior clean-room Ghidra outputs as steering references

## Working Rules

- Stay clean-room.
- Do not import THJ code.
- Use THJ decomp outputs and prior Ghidra reports only to inform seam selection.
- Verify the exact in-game UX before choosing a seam.
- Prefer producer/update seams over final text-write interception.
- Keep logs and traces bounded and decision-oriented.
- If a seam is disproven, record it in `docs/multiclass-negative-results.md`.
