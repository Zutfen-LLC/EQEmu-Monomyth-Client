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
- direct final-text interception (`CXStr::Assign`, `CXWnd::SetWindowTextA`) was the wrong layer

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
- the validated `0x321210` `CharSelectClassNameFunc` entry is now trace-hooked directly so the next repro can prove whether the broader producer runs even when the inner `0x3212b6` class-label branch stays cold
- inventory summary candidate C at `0x712b00` is not the live seam for the active top-left summary repro

The next useful validation step is to repro the inventory summary pane and inspect the log for:
- `InventoryClassDisplayCorrelationWindow`
- whether any later `UiClassHelperTrace` now survives after the shared-string budget-preservation change
- any fresh entry traces from the next inventory self-refresh seam once instrumented

For character select, the next useful step is:
- bias toward the actual post-auth character-select refresh/repaint path instead of the cold progression-selection seam
- prefer later full-name producers or refreshers that run after `ServerAuthStats valid=true ... statClassesBitmask=0x00001044`
- inspect the next repro for `CharSelectClassNameFuncTrace phase=before/after` to see whether `field_248` or `field_24c` branches are active post-auth inside the validated `0x321210` producer

Expected live log path:
- `/home/zutfen/everquest_rof2/monomyth-client.log`

Next seam direction:
- bias toward `CInventoryWindow` self-refresh paths such as `Activate` / `OnProcessFrame`
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
