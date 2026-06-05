# Handoff

## Active Task

Fix the Disciplines window for multiclass characters, specifically `WIZ` primary with melee secondaries such as `WAR/PAL`.

Current state:
- The server-side login/profile discipline backfill bug was fixed in the server repo.
- The client now receives and applies nonzero `CombatAbilities[300]` data.
- The Disciplines window still does not open.

This is no longer a data-population bug. It is now a client window-open/visibility bug.

## What Is Proven

### Server/data path is working now

Latest good client evidence from `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `2026-06-05 09:53:16.269`
- `hook_manager: combat ability resync reason="applied"`
- `nonzero_entries=22`
- `classes_mask=0x805`
- sample: `4498,4499,4500,4501,4503,4514,4518,4585`

So the client is no longer empty. It has 22 populated combat ability entries for the `WIZ/WAR/PAL` repro.

### Hot rebuild seam is real, but not the right open seam

Installed and hot:
- `CombatAbilityWndRebuildPrimarySlotsGetAbilityCallsite`
  - RVA `0x0025a12a`
  - VA `0x65a12a`
- `CombatAbilityWndRebuildAbilityListGetAbilityCallsite`
  - RVA `0x0025aea7`
  - VA `0x65aea7`

Latest log proof:
- `2026-06-05 09:53:17.752+`
- repeated `hook_manager: combat ability lookup trace`
- `caller_rva=0x25aea7`
- trace begins at `ability_index=22`

Interpretation:
- indices `0..21` are likely already nonzero and therefore do not emit the negative-path trace
- the rebuild-list seam is hot only on the zero tail of the array
- this seam is good for population evidence, but not for window-open repair

## What Failed Most Recently

### Failed client-side force-show attempt

Code added in `src/hook_manager.cpp`:
- `TryReadCombatAbilityWindowPointer()` using `pinstCCombatAbilityWnd`
- `TryForceCombatAbilityWindowVisible(...)`
- pending show arming tied to successful `combat ability resync`

Latest staged DLL before handoff:
- `/home/zutfen/everquest_rof2/dinput8.dll`
- SHA-256 `476c1d5f2e164703f37d3668bddb5966e3a2a890ecf57fd2cbd1c9a24acbe0d6`

Why it failed:
- latest repro showed `combat ability resync reason="applied"` as expected
- but there were still **no** `hook_manager: combat ability window force show` lines
- so the show repair never fired

Conclusion:
- even after removing the earlier bad `ability_index == 0` gate, the rebuild-list seam still does not observe the nonzero entries where we need to force visibility
- this seam is too late / too partial for the actual open path

## Real Next Target

Patch the actual Disciplines dispatcher/open gate around:
- VA `0x4d8497`
- expected global: `pinstCCombatAbilityWnd` at `0x00d1fca0`

Relevant clean-room disassembly already recovered:

```asm
4d8497: mov ecx, ds:0xd1fca0
4d849d: cmp ecx, ebx
4d849f: je  0x4db2ed
4d84a5: cmp byte ptr [ecx+0x196],0
4d84ac: mov eax,[ecx]
4d84ae: mov edx,[eax+0xd8]
4d84b4: push ebp
4d84b5: push ebp
4d84b6: jne 0x4d84cd
4d84b8: push ebp
4d84b9: call edx
4d84bb: mov ecx, ds:0xd1fca0
4d84c1: call 0x85ce90
...
4d84cd: push ebx
4d84ce: call edx
4d84d0: mov ecx, ds:0xd1fca0
4d84d6: call 0x85ce90
```

This is the preferred next seam.

## Next Session Plan

1. Validate the dispatcher seam bytes at `0x4d8497`.
2. Add bounded trace there for:
   - window pointer null/non-null
   - `[window+0x196]`
   - which branch/path is taken before the `vfunc +0xd8` call
3. Confirm the seam is hot on `ALT-C` and GUI-menu Disciplines open attempts.
4. If hot, patch behavior there instead of in the rebuild loop.

Do not spend another iteration trying to force-show from the `0x25aea7` rebuild-list hook unless new evidence proves it sees the actual nonzero entry path.

## Useful References

Read first:
- `docs/multiclass-negative-results.md`
- `src/hook_manager.cpp`

External orientation allowed by repo policy:
- sibling `MacroQuest` / `eqlib` repos under `/home/zutfen/`
- confirmed useful MQ anchor:
  - `pinstCCombatAbilityWnd_x = 0xD1FCA0`

## Important Negative Results Already Proven

Do not reopen these without new contrary evidence:
- cold `EQ_PC::GetCombatAbility` callsites:
  - `0x259c9e`
  - `0x17f873`
  - `0x18083b`
- bogus early producer assumption via timer/recast setters:
  - `0x57937c`
  - `0x579398`
  - `0x5793b9`
- rebuild-list force-show attempt from `0x25aea7`

## Validation Loop

Focused tests:
- `ctest --test-dir build --output-on-failure -R "multiclass_combat_ability_tests|multiclass_skill_visibility_tests"`

Build and stage:
- `cmake --build build-cross-i686 --target dinput8 -j4`
- copy `build-cross-i686/dinput8.dll` to `/home/zutfen/everquest_rof2/dinput8.dll`
- verify hashes match

Live proof source:
- `/home/zutfen/everquest_rof2/monomyth-client.log`
