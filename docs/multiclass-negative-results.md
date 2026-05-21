# Multiclass Negative Results

Purpose: record seams and approaches that were tried for clean-room multiclass client work and did not solve the target problem, so we do not keep re-running the same experiments.

Status meanings:
- `disproven`: runtime or disassembly evidence shows this seam was the wrong layer or did not affect the target surface.
- `inconclusive`: tried, but the evidence was weak or a known precondition made the result non-final.
- `stale`: older result that may no longer apply after later hook changes; do not treat it as current proof.

## Current Ledger

| Surface | Seam / approach tried | Status | Evidence | What we learned | Preferred replacement direction |
| --- | --- | --- | --- | --- | --- |
| `/who` row class text | Internal `WhoClassName` string-lookup callsite patch set inside `0x536310`: `0x5364e7`, `0x5365c2`, `0x536601` | `disproven` | Live runs around `2026-05-21 17:27` and `17:37` installed the callsite hooks, but produced no `UiClassDisplayTrace` for `WhoClassName`; `/who` still decoded as native `class_id=3` | Patching internal lookup sites inside the broader builder was not enough to rewrite visible `/who` class text | Track real `WhoClassName` subject context at wrapper entry and/or move closer to the actual `EQ_WhoClassName` producer seam THJ detoured |
| `/who` row class text | Narrowed single-callsite patch on `WhoClassNameClassLookupCallsiteA` at `0x5364e7` | `disproven` | Live runs around `2026-05-21 17:47` and `17:53` still showed no `UiClassDisplayTrace` for `WhoClassName`; `/who` still decoded as native `class_id=3` | Even the class-id-fed branch alone was still the wrong rewrite layer when patched as a raw callsite | Keep the newer wrapper-context plus filtered-lookup model; if that still fails, pivot again toward the real `EQ_WhoClassName` function-pointer seam |
| Inventory title / local full-name class text | Direct UI-text interception: guessed inventory refresh path, `CXWnd::SetWindowTextA`, and `CXStr::Assign` at `0x405d90` | `disproven` | These hooks were retired after repeated failures to change the visible inventory class title; THJ evidence also points to producer detours, not final-control text writes | The formatting was not the issue; this was the wrong layer | Resume only when a real inventory-adjacent producer seam is pinned, likely the clean-room equivalent of THJ `EQ_CharSelectClassNameFunc` |
| Inventory / char-select-adjacent class text | Treating the current `char_select_class_name_func` discovery slot as proven THJ `EQ_CharSelectClassNameFunc` | `inconclusive` | The currently recovered seam behaves like a progression-selection writer and is useful there, but it is not proven to be THJ's real inventory/title producer seam | Do not generalize the progression-selection seam into proof for other UI surfaces | Keep it scoped to progression selection until a distinct inventory/title producer is recovered |
| Local full-name class display | `0x514dc0` caller `0x18e554` early full-name path, before `OP_ServerAuthStats` is available | `inconclusive` | Runs around `2026-05-21 17:37`, `17:47`, and `17:53` showed `caller_rva=0x18e554` firing before assigned multiclass data arrived, so override could not apply | Early fallback here does not prove the formatter or semantic override is wrong; timing was bad | Re-test only after authoritative class-mask data is present, or find a later producer seam for the same surface |

## Confirmed Useful Seams

These are not negative results; they are listed here so future work has a short record of what actually solved a visible surface.

| Surface | Confirmed seam | Evidence | What it means |
| --- | --- | --- | --- |
| `/who` row class text | Shared lookup target `0x7d0660`, visible class-label caller `0x477e6`, gated to the post-`OP_WhoAllResponse` correlation window | Live run around `2026-05-21 18:47` showed `WhoAllClassDisplayTrace ... caller_rva=0x477e6 ... override_applied=true ... formatted="Paladin/Monk/Magician"` and the user confirmed `/who` is visually correct | `/who` is solved through the real live class-label seam; do not reopen older `0x536310` callsite-patch ideas unless fresh contrary evidence appears |

## Working Rules

- Before trying a new hook, check whether it is just a close cousin of an already `disproven` seam.
- When adding a new row, capture the exact RVA or function, the target surface, and one concrete runtime/disassembly reason it failed.
- Prefer upgrading `inconclusive` rows to `disproven` or `confirmed-useful`; avoid leaving ambiguous dead-end notes scattered across commits and handoff text.
