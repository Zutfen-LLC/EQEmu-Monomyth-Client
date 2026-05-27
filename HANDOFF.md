# Handoff

## Read This First

Start here before making changes:
- `HANDOFF.md`
- `docs/multiclass-negative-results.md`
- `src/hook_manager.cpp`

Also useful for seam selection and validation:
- `/home/zutfen/code/eqlib`
- `/home/zutfen/code/MacroQuest`
- `docs/cleanroom-dll-research/`
- live log: `/home/zutfen/everquest_rof2/monomyth-client.log`

## Current Status

Authoritative multiclass data is working.
- `OP_ServerAuthStats` is received and parsed correctly.
- actual mana math is working for pure-melee-base characters with caster classes in the authoritative mask
- `/who` is solved and visually correct

The active blocker in this thread is the local player mana bar/text for a warrior-primary style repro with caster secondaries.

Current user-visible state before the newest unvalidated patch:
- actual mana exists and updates correctly
- mana controls are visible and no longer flap on the old 6-second cadence
- the visible mana gauge and mana label still render nothing
- the percent-label path has valid state, but the gauge and label do not

## Current Verdict

The remaining problem is not:
- `Max_Mana`
- `Cur_Mana`
- `GetManaRegen`
- generic mana visibility
- the direct player-window mana state object at `player_window + 0x24c`

The remaining problem is:
- the stock player-window mana selection/binding path is not producing valid state for the real mana gauge and mana label on the warrior-primary repro

Proven steady-state facts from the last stable no-crash log at `2026-05-27 16:57`:
- `MulticlassManaTrace kind="cur_mana"` reports `authoritative_result=40`
- `PlayerManaUiStateSeed` writes valid state
- `PlayerManaCompareSnapshot` shows:
  - `direct_state` valid with `41/40`
  - `percent_label_state` valid
  - `gauge_state=0x0`
  - `label_state=0x0`
- `PlayerManaFullRefreshDeferred` repeats with:
  - `selected_entry_before=0x0`
  - `last_selected_entry_before=0x0`
  - `current_index_before=-1`

That means the real missing layer is the stock selection/binding path feeding `+0x220` and `+0x254`, not the raw mana values.

## Important Corrections

Do not reopen these wrong assumptions:
- `+0x254` is not “current mana”; forcing `40` into it was wrong
- replaying the whole `0x41bd20` selection-refresh family from hot mana code is not safe
- replaying the whole `0x41bc40` selection writer directly from hot `Cur_Mana` is not safe
- the percent-label helper path is not enough proof for the gauge/label path

What disassembly now strongly suggests:
- `0x41bc40` derives `+0x254` via `0x853430`, then derives `+0x220` via `0x853680`
- `0x41d6a0` also writes `+0x254`, but only to small values `1` or `2` in a UI event path, so `+0x254` behaves like a small selector/mode slot, not a direct mana amount
- the hot recurring helper at `0x41b6fd -> 0x86a790 -> 0x419500` is working on the percent-label path, not creating the missing gauge/label state

## Live Seams In Play

Installed and proved hot:
- `CharacterZoneClient::Max_Mana` at `rva=0x00181e60`
- `CharacterZoneClient::GetManaRegen` at `rva=0x00052df0`
- `Cur_Mana` callsite set only, not entry detour
- player mana visibility callsites to `0x866610`
- player mana refresh helper callsite at `0x41b6fd`
- selection-refresh callsites:
  - `0x41c003 -> 0x41bd20`
  - `0x41c44c -> 0x41bd20`
- selection-writer callsite:
  - `0x41c8d8 -> 0x41bc40`

Known important producer family:
- `0x41b3e0`
- `0x41b670`
- `0x41b7f0`
- `0x41b920`
- `0x41ba60`
- `0x41bc40`

## Current Code State

Relevant areas in `src/hook_manager.cpp`:
- mana state seeding: `TrySeedPlayerManaUiState`
- player-window comparison logging: `LogPlayerManaComparisonSnapshot`
- refresh replay path: `TryReplayPlayerManaFullRefreshProducer`
- refresh-worker replay: `TryReplayPlayerManaRefreshWorker`
- current experimental narrow repair: `TryRepairPlayerManaSelectionState`

As of the newest build now deployed to `/home/zutfen/everquest_rof2/dinput8.dll`:
- the old crashy direct call to `0x41bc40` from hot `Cur_Mana` is gone
- `TryRepairPlayerManaSelectionState` now uses the two stock direct-state helpers behind `0x41bc40` instead:
  - `0x853430` to derive the selection index
  - `0x853680` to derive the selected entry pointer
- it then writes only:
  - `player_window + 0x254`
  - `player_window + 0x220`
- this new helper-based repair is compiled, tested, and copied, but not yet validated by a fresh full repro log

## Most Recent Stable Log Evidence

From the last full no-crash repro before the newest helper-based repair attempt:
- install summary:
  - `player_mana_selection_refresh_callsites_installed=2`
  - `player_mana_selection_writer_callsite_installed=true`
- first in-world mana tick:
  - `PlayerManaFullRefreshDeferred ... reason="invalid_current_index"`
  - `PlayerManaCompareSnapshot ... gauge_state=0x0 label_state=0x0 percent_label_state valid`
  - `MulticlassManaTrace kind="cur_mana" ... authoritative_result=40`
- second tick repeats the same failure shape unchanged

From the newest log after deploying the helper-based repair build:
- only the startup install line is present so far at `2026-05-27 17:03`
- no fresh `PlayerManaSelectionRepair` evidence has been captured yet

## Disproven Seams

See `docs/multiclass-negative-results.md` for the full ledger.

The key mana-specific dead ends are:
- whole refresh-worker entry detour at `0x41b670`
- authoritative replay of `0x41bd20` selection-refresh family during invalid pre-zone state
- direct replay of `0x41bc40` from hot `Cur_Mana`

## Next Concrete Step

Repro with the newest helper-based repair build and inspect the log for:
- `PlayerManaSelectionRepair`
- `PlayerManaFullRefreshDeferred`
- `PlayerManaFullRefreshReplay`
- `PlayerManaCompareSnapshot`

Success signals:
- `PlayerManaSelectionRepair`
  - `index_invoked=true`
  - `entry_invoked=true`
  - `wrote_index=true`
  - `wrote_entry=true`
- then either:
  - `PlayerManaFullRefreshDeferred` disappears, or
  - it shows `selection_repaired=true` and `current_index_after_selection_repair` is no longer `-1`
- and ideally later:
  - `gauge_created_after=true`
  - `label_created_after=true`

Failure signals:
- crash before any `PlayerManaSelectionRepair` line
- `PlayerManaSelectionRepair` runs but still leaves `selected_entry_after_repair=0x0` or `current_index_after_repair=-1`
- no change in `PlayerManaCompareSnapshot`, with `gauge_state=0x0` and `label_state=0x0`

## Working Rules

- Stay clean-room.
- Do not import THJ code.
- Prefer runtime proof over guesswork.
- Prefer narrow producer/state seams over generic UI hooks.
- Keep traces bounded.
- If a seam is disproven, update `docs/multiclass-negative-results.md`.
