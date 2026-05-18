# Implementation Spec

Spec ID: task-001-multiclass-identity-api
Author: PM Hermes
Date: 2026-05-18
Related Mission:
- `.agents/state/mission.md`
Source Context:
- `docs/multiclass-identity-intervention-map.md`
- `src/server_auth_stats_observer.h`
- `src/server_auth_stats_observer.cpp`
- `src/spell_level_selection.h`
- `src/spell_level_selection.cpp`
- `tests/spell_level_selection_tests.cpp`

## Goal

Introduce a small internal multiclass identity helper API so the repo stops scattering low-level class-mask rules across spell-level-selection code and future hooks can share one authoritative utility layer.

## Supported Inputs

- a `bool has_classes_bitmask` flag plus `std::uint32_t classes_bitmask`
- playable EverQuest class ids 1 through 16
- existing spell-level-selection call sites that need class-bit iteration or validation

## Required Outputs / Effects

- a new helper module under `src/` that models authoritative multiclass identity primitives
- unit tests for the helper module
- `spell_level_selection` updated to use the helper instead of its private class-mask helpers
- no runtime behavior change beyond the refactor

## Behavioral Rules

1. The helper API must treat the authoritative multiclass set as a bounded 16-class playable mask.
2. The helper API must expose at least these concepts: class-id validity, class-bit generation, playable-mask validation, and membership checks.
3. `spell_level_selection` must preserve all existing fallback behavior and selection behavior.
4. Any helper names should reflect identity semantics rather than spell-specific behavior.
5. This slice must not change `hook_manager` behavior, packet handling, logging formats, or spellbook trace flow.

## Files In Scope

Create:
- `src/multiclass_identity.h`
- `src/multiclass_identity.cpp`
- `tests/multiclass_identity_tests.cpp`

Modify:
- `src/spell_level_selection.h`
- `src/spell_level_selection.cpp`
- `CMakeLists.txt`

## Constraints

- performance: helper operations should remain trivial constexpr-or-near-constexpr value checks with no heap allocation
- compatibility: preserve current `spell_level_selection_tests` behavior exactly
- security: no new unsafe memory access patterns
- legal/process: builder receives only repo code and this PM-authored spec; no raw recon artifacts

## Non-Goals

- no spellbook click-to-scribe fix
- no hook installation changes
- no new logging categories
- no UI display formatting yet
- no server-auth snapshot storage redesign

## Acceptance Tests

1. Existing `spell_level_selection_tests` still pass unchanged in meaning.
2. New `multiclass_identity_tests` cover valid class ids, invalid class ids, class-bit mapping, mask validation, and membership checks.
3. The build configuration includes the new helper module in the DLL target and the new unit test target.

## Allowed Assumptions

- the playable class range remains 1..16 for this slice
- the authoritative mask remains the same `statClassesBitmask` semantic already used in the repo

## Unresolved Risks

- future UI-facing identity work may require additional helper functions not included in this slice
- if another file already duplicates class-mask logic, PM may schedule a later consolidation task rather than expanding this brief mid-flight

## Builder Inputs Allowed

- this spec
- `.agents/briefs/codex-task-001-multiclass-identity-api.md`
- repository files in the assigned worktree

## Builder Inputs Prohibited

- raw decompilation or ghidra note excerpts
- unrelated mission notes not referenced above
