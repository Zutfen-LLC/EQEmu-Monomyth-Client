# Mission

Mission ID: multiclass-identity-slice-01
Owner: PM Hermes
Date: 2026-05-18
Branch Context: codex/client-memorize-send-trace-001

## Goal

Advance the ROF2 client toward authoritative multiclass identity using a clean-room, evidence-driven workflow. The near-term objective is to (1) keep recon focused on proving the true upstream spellbook click-to-scribe gate for secondary-class PAL scrolls, and (2) prepare a reusable internal multiclass identity API that later gameplay and UI hooks can share without broad profile spoofing.

## Scope

In scope:
- spellbook click-to-scribe clean-room archaeology for the PAL secondary-class failure
- extraction of a small reusable multiclass identity helper API from existing class-mask logic
- unit-test coverage for any new helper API
- PM-authored specs and narrow worker briefs

Out of scope:
- speculative gameplay fixes before the spellbook seam is proven
- raw decompilation or THJ gameplay code passed into Codex
- broad profile class spoofing or packet mutation
- remote display surfaces such as guild roster, /who, or character select

## Clean-Room Boundary

Mode: soft separation with strict builder input rules

Recon may see:
- `/home/zutfen/monomyth_ghidra/out/right_click_chain_update_verified_new_exe.md`
- `docs/cleanroom-dll-research/eqgame-spell-ui-ghidra-notes.md`
- `/home/zutfen/everquest_rof2/monomyth-client.log`
- repo code and docs
- other clean-room notes explicitly approved by PM

Codex may receive only:
- PM-authored specs and briefs
- repository source files in the assigned worktree
- approved tests and public repo docs

Codex may not receive:
- raw decompilation fragments
- proprietary source equivalents
- unfiltered ghidra dumps

## Source Materials

Allowed:
- `docs/handoff.md`
- `docs/multiclass-identity-intervention-map.md`
- `README.md`
- repo `src/` and `tests/`
- approved local logs and clean-room recon notes for recon-only tasks

Prohibited for builder:
- THJ gameplay code
- copied MQ2 runtime logic
- raw recon notes not distilled into PM specs

## Success Criteria

- recon identifies or materially narrows the first real spellbook-side click-to-scribe handler
- the repo gains a small reusable multiclass identity helper API with unit tests and no broader behavior change
- downstream tasks can reference stable artifacts instead of ad hoc chat context

## Stop Conditions / Escalation

Escalate to human if:
- more than 2 review loops fail on the same coding task
- recon confidence remains low on the critical spellbook gate after a focused pass
- the proposed helper API requires touching currently dirty spellbook instrumentation files and risks context collisions

## Active Tasks

- [ ] recon-001-spellbook-click-archaeology
- [ ] task-001-multiclass-identity-api
- [ ] review-001-task-001

## PM Rules

- PM is the only relay between workers
- do not forward raw recon artifacts to Codex
- every worker output must cite stable filenames
- keep spellbook archaeology and identity-API extraction as separate slices
