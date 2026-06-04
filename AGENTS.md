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
- THJ DLL decompilation, Ghidra notes, `eqlib`, and `MacroQuest` may be used as references to choose seams and validate assumptions. If you need information and cannot find it, you may ask for the location, but it is generally found in /home/zutfen/ so try there (and associated subdirectories) first.
- Macroquest and eqlib code may be reused verbatim, as they are open source and trusted.
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

## RVA Discipline

- Do not hand-transcribe an RVA from a nearby VA without proving the subtraction against the live module base.
- When deriving an RVA from disassembly, write down the full EQ virtual address first, then compute `rva = va - module_base`, with ROF2 `eqgame.exe` usually loading at `0x400000`.
- Before shipping a new pinned seam, validate it in two ways:
  - static: confirm the exact bytes at the intended VA/RVA with `objdump` or equivalent
  - runtime: log the computed `callsite_address` or `target_address` and confirm it matches the expected live address
- Prefer `scripts/va_rva.py` for VA/RVA conversion instead of hand-doing `0x400000` subtraction in notes or patches.
- For callsite patches, always validate both:
  - the exact callsite bytes
  - the resolved original call target
- Prefer naming constants from the proved seam role plus the final RVA, not from an earlier guess.
- If a hook install fails because of byte mismatch, treat that as evidence the seam may be wrong before trying behavior changes.
- When disassembly shows both a full VA and an RVA in notes/logs, keep both in comments or diagnostic output until the seam is proven hot in live logs.
- Before asking the user to retest a new hook, verify the startup log contains the exact installed seam name and the expected RVA/address pair.

## Validation

When touching the current client tracing work, prefer:
- focused local test suite:
  - `ctest --test-dir build --output-on-failure -R "runtime_capabilities_tests|class_display_discovery_tests|multiclass_identity_tests|spell_level_selection_tests"`
- live client log review:
  - `/home/zutfen/everquest_rof2/monomyth-client.log`

# Build
You can cross-build the Windows DLL in build-cross-i686 - when you build, copy the resulting dinput8.dll into /home/zutfen/everquest_rof2/ as well, so it can be immediately tested.

## Communication

- Be concise.
- Prefer action over long summaries.
- State the current verdict and the next concrete step.
