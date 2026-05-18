# Recon Brief

Task ID: recon-001-spellbook-click-archaeology
Author: PM Hermes
Date: 2026-05-18
Output Report:
- docs/recon/2026-05-18-spellbook-click-archaeology.md

## Objective

Identify or materially narrow the first real spellbook-side handler reached when the user manually clicks a PAL spell scroll into an empty spellbook slot on the MAG(primary)/PAL/MNK character. Prove whether the failure occurs at a class predicate, a raw usable-class getter, a spellbook-specific precheck, or another earlier UI gate.

## Scope of Inspection

- `docs/handoff.md`
- `docs/multiclass-identity-intervention-map.md`
- `/home/zutfen/everquest_rof2/monomyth-client.log`
- `/home/zutfen/monomyth_ghidra/out/right_click_chain_update_verified_new_exe.md`
- `docs/cleanroom-dll-research/eqgame-spell-ui-ghidra-notes.md`
- current repo spellbook-related tracing in `src/spell_usability_discovery.cpp` and `src/hook_manager.cpp`

## Questions To Answer

1. What is the earliest proven spellbook-side handler reached by the manual scroll-into-book-slot click path?
2. Does that path consult a class predicate, a raw usable-class source, a spell-level gate, a pre-scribe precheck, or an earlier UI affordance gate?
3. Which currently pinned runtime seam is closest upstream to that handler, and what new trace-only seam would best narrow the gap?

## Inputs Allowed

- ghidra notes
- local runtime logs
- repo docs and code
- additional clean-room recon if required

## Outputs Required

- observed facts
- inferred behavior
- confidence labels
- named candidate handler(s) with rationale
- open questions
- prohibited assumptions for builder
- recommendation for the next trace-only seam

## Rules

- do not write production code
- separate facts from inference
- do not propose a behavior-changing patch before the blocking seam is proven
- keep the report builder-safe: behavioral descriptions, no source-like reproduction
