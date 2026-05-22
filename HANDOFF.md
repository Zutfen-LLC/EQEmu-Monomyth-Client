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

Current trace hooks in `src/hook_manager.cpp`:
- `CInventoryWindow::WndNotification` at `rva=0x00293490`
- inventory summary candidate A at `rva=0x00290100`
- inventory summary candidate B at `rva=0x002905f0`
- inventory summary candidate C at `rva=0x00312b00`

Candidate C is the most promising current seam.
- `0x712b00` is a binary-backed multi-label refresh function
- it contains:
  - `GetClassDesc` callsite at `0x712c50`
  - `GetRaceDesc` callsite at `0x712ca2`
- this is why it was promoted into the live trace set

## Current Code State

Recent changes already in the repo:
- logger writes UTF-8
- item-display tracing is retired as the active blocker
- inventory trace install bug behind an early return was fixed
- inventory summary candidate C was added

Files to inspect first:
- `src/hook_manager.cpp`
- `docs/multiclass-negative-results.md`

Notable current rough edge:
- `GetDeityDesc` hook shape is still wrong / incomplete in `src/hook_manager.cpp`
- current typedef/hook still uses a `CDECL` model even though `eqlib` treats it as a `CEverQuest` member function
- live install had previously failed with `prologue unsupported` / `install_failed`
- do not assume fixing the calling convention alone will make inline detouring work, because `0x515f70` is a jump-table style function

## Latest Practical Direction

The next useful validation step is to repro the inventory summary pane and inspect the log for:
- `hook_manager: inventory summary candidate trace installed target=InventorySummaryRefreshCandidateC`
- `InventorySummaryCandidateEntryTrace candidate=InventorySummaryRefreshCandidateC`
- any following:
  - `InventoryClassDisplayCorrelationWindow`
  - `InventoryClassDisplayTrace`
  - `UiClassHelperTrace`

Expected live log path:
- `/home/zutfen/everquest_rof2/monomyth-client.log`

If candidate C still does not fire:
- use more focused binary/Ghidra work
- bias toward the actual summary refresh/writer path, not more UI callback guesses
- use `eqlib`, `MacroQuest`, and prior clean-room Ghidra outputs as steering references

## Working Rules

- Stay clean-room.
- Do not import THJ code.
- Use THJ decomp outputs and prior Ghidra reports only to inform seam selection.
- Verify the exact in-game UX before choosing a seam.
- Prefer producer/update seams over final text-write interception.
- Keep logs and traces bounded and decision-oriented.
- If a seam is disproven, record it in `docs/multiclass-negative-results.md`.

