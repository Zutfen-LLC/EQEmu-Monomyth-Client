# Codex Task Brief

Task ID: task-001-multiclass-identity-api
Author: PM Hermes
Date: 2026-05-18
Suggested Worktree: `worktrees/task-001-multiclass-identity-api`
Suggested Branch: `codex/task-001-multiclass-identity-api`
Primary Spec:
- `docs/specs/2026-05-18-multiclass-identity-api-spec.md`
Related Mission:
- `.agents/state/mission.md`

## Objective

Implement the small reusable multiclass identity helper API described in the spec, wire `spell_level_selection` to use it, add focused unit tests, and stop there.

## Files Likely Involved

- `src/multiclass_identity.h`
- `src/multiclass_identity.cpp`
- `src/spell_level_selection.h`
- `src/spell_level_selection.cpp`
- `tests/multiclass_identity_tests.cpp`
- `CMakeLists.txt`

## Required Work

1. Add the new helper module with narrow identity-focused helpers for class-id validity, class-bit mapping, playable-mask validation, and membership checks.
2. Refactor `spell_level_selection` to use that helper instead of its private class-mask helpers while preserving current behavior.
3. Add a new unit test binary for the helper module and register it in `CMakeLists.txt`.
4. Keep the change scoped to these files unless the build forces a tiny include-only adjustment elsewhere.

## Acceptance Criteria

- existing spell-level-selection semantics stay the same
- new helper tests cover both valid and invalid inputs
- no changes to hook behavior, packet observation, or spellbook tracing
- no edits to currently dirty files such as `src/hook_manager.cpp`, `src/runtime_capabilities.*`, `src/spell_usability_discovery.*`, or `docs/handoff.md`

## Tests to Run

- build the unit test targets that cover this slice
- run `spell_level_selection_tests`
- run `multiclass_identity_tests`

If the local Linux cross-build environment supports it cleanly, also run the normal build target needed to ensure the new source file is wired into the DLL target.

## Constraints

- do not broaden scope beyond this brief
- do not alter logging output
- do not change behavior under `MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY`
- if you discover another desirable cleanup, report it but do not implement it in this task

## Inputs Allowed

- this brief
- `docs/specs/2026-05-18-multiclass-identity-api-spec.md`
- repository files in this worktree

## Inputs Prohibited

- recon artifacts not explicitly listed above
- ghidra notes or decompilation excerpts
- unrelated mission notes not referenced here

## Output Required From Codex

Before exiting, provide:
1. changed files list
2. tests run and results
3. assumptions made
4. unresolved risks
5. whether the brief was fully completed
