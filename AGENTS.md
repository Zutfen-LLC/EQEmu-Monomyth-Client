# AGENTS

## Start Here

Before doing anything substantial, read:
- `HANDOFF.md`
- `docs/multiclass-negative-results.md`

If the task is about multiclass client behavior, also inspect:
- `src/hook_manager.cpp`
- `docs/cleanroom-dll-research/`

## Repo Rules

- Stay clean-room.
- Do not copy or port THJ code into this repo.
- THJ DLL decompilation, Ghidra notes, `eqlib`, and `MacroQuest` may be used as references to choose seams and validate assumptions.
- Prefer runtime proof and binary-backed evidence over guesswork.

## Current Priority

The active blocker is the inventory window summary pane class display in the top-left character summary block.

It is not:
- `/who`
- item display / item inspection
- a final text-write interception problem

Treat `HANDOFF.md` as the current source of truth for the active seam, known dead ends, and next validation step.

## Working Style

- Use `rg`/`rg --files` for search.
- Use `apply_patch` for edits.
- Do not revert unrelated user changes.
- Keep traces bounded and specific.
- Prefer direct producer/update seams over generic UI hooks.
- When a seam is disproven, update `docs/multiclass-negative-results.md`.

## Validation

When touching the current client tracing work, prefer:
- focused local test suite:
  - `ctest --test-dir build --output-on-failure -R "runtime_capabilities_tests|class_display_discovery_tests|multiclass_identity_tests|spell_level_selection_tests"`
- live client log review:
  - `/home/zutfen/everquest_rof2/monomyth-client.log`

## Communication

- Be concise.
- Prefer action over long summaries.
- State the current verdict and the next concrete step.

