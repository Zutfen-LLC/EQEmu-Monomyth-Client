# Multiclass Identity Intervention Map

## Purpose

This document reframes the current work from a single spell-scribe bug hunt into
a broader clean-room plan for making the ROF2 client understand and present
authoritative multiclass identity.

The target outcome is not only "secondary-class PAL spell scribing works" but:

- gameplay gates consult authoritative multiclass state where required
- local UI surfaces can present more than the base class when appropriate
- packet-backed or roster-backed views can eventually reflect multiclass truth
- the implementation stays clean-room, bounded, and verification-driven

## Current Proven Facts

### Authoritative state already exists in the DLL

- The receive hook already parses `OP_ServerAuthStats` (`0x1338`) read-only.
- It stores only `statClassesBitmask` in internal DLL state.
- The bitmask is authoritative server data, not inferred local state.

Current implementation paths:

- `src/server_auth_stats_observer.cpp`
- `src/hook_manager.cpp`
- `src/spell_level_selection.cpp`

### One narrow "client sees both classes" behavior already exists

- `EQ_Spell::GetSpellLevelNeeded` can already be overridden as part of the
  native validated ROF2 multiclass spell usability behavior.
- That hook queries the original client spell level for each assigned class in
  `statClassesBitmask` and returns the lowest valid required level.
- This is a targeted multiclass identity seam, not global class spoofing.

### The current PAL spellbook-click failure is earlier than that seam

Fresh trace evidence showed that the manual spellbook-click scribe attempt did
not reach:

- `GetSpellLevelNeeded`
- `CanStartMemming`
- `StartSpellMemorizationPath`
- `MemSpellCommitPath`
- outbound `OP_DeleteSpell`
- outbound `OP_MemorizeSpell`

That means the current failure is upstream of the existing spell-level seam.

### Ordinary memorize has a different proven blocker

For the separate ordinary memorize path, prior traces showed:

- `CanStartMemming` can succeed
- `MemSpellCommitPath` can be reached
- the path can fast-exit with `state_240=0xffffffff`

That is a separate problem from spellbook click-to-scribe.

## Design Direction

## Core thesis

We should treat multiclass identity as an authoritative internal model with
surface-specific adapters, not as one monolithic "pretend the player profile
class changed everywhere" hack.

That means:

- keep one authoritative multiclass snapshot source
- map each gameplay or UI surface to the narrowest safe intervention
- prefer local adapters over broad profile-memory spoofing
- only broaden when the next surface is proven to need it

## Identity model

The practical identity model should be:

- `primary_class`
  - the original client/base profile class
- `assigned_class_mask`
  - the authoritative multiclass set from `statClassesBitmask`
- `has_class(class_id)`
  - helper for class membership checks
- `lowest_valid_spell_level(spell, requested_class)`
  - already implemented via `GetSpellLevelNeeded`
- `display_class_set`
  - eventual UI-facing class list or formatted representation

This allows us to distinguish:

- surfaces that need only membership checks
- surfaces that need spell-level selection
- surfaces that need a UI display of multiple classes
- surfaces that are packet-backed and may require different handling

## Research posture

- Additional clean-room Ghidra recon against `eqgame.exe` is still available and
  should be treated as an active tool, not a last resort.
- The currently pinned spell and usability seams are only the surfaces mapped so
  far.
- When runtime evidence dies upstream of those seams, the correct move is often
  more client archaeology rather than forcing a broader speculative patch.

## Surface Inventory

| Surface | Current evidence | Likely dependency | Intervention style | Priority |
| --- | --- | --- | --- | --- |
| `EQ_Spell::GetSpellLevelNeeded` | Proven and already implemented | Base-class spell requirement lookup | Keep existing narrow override using `statClassesBitmask` | P0 done |
| Spellbook click-to-scribe drop handler | Not yet identified; fresh log proves failure occurs before current seams | Likely pre-level spellbook UI or class-usability gate | Trace and identify exact spellbook drop/click handler before patching | P0 |
| `EQ_Character::IsClassUsablePredicate` | Trace-safe target already pinned | Class membership predicate | If proven in spellbook path, add narrow context-scoped override using `has_class` | P0 candidate |
| `EQ_Character::GetUsableClasses` | Mentioned in research only; not pinned | Raw class mask lookup for items or scrolls | Research and pin only if upstream spellbook path proves it is needed | P1 candidate |
| `EQ_Character::CanEquip` | Mentioned in research only; intentionally unpinned | Item/usability gate with wider blast radius | Defer until a concrete failing call path proves it is required | P1 defer |
| `CSpellBookWnd::CanStartMemming` | Trace-safe and already instrumented | Spellbook mem gate after level check | Keep trace-only until a real block is proven after scribe-path work | P1 |
| `StartSpellMemorizationPath` / `MemSpellCommitPath` | Proven for ordinary memorize path | Spellbook pending-state initialization and outbound memorize send | Keep separate from scribe work; patch only when that specific path is resumed | P1 separate |
| Inventory / player self UI | Research artifacts expose `ppInventoryWnd` and `ppPlayerWnd`, but no class-specific seam is mapped yet | Likely reads base profile class for labels or filters | Add local display adapters after behavioral surfaces are stable | P2 |
| Character select UI | No mapped seam yet | Likely packet/profile-backed single-class display | Research after local in-game gameplay gates stabilize | P2 |
| `/who` and remote who-list display | No mapped seam yet; likely packet/rendered list data | Likely remote display or formatted response path, not local self-only state | Treat as remote-display project, not as a first gameplay fix | P2 |
| Guild manager / guild roster | Research artifacts expose `ppGuildMgmtWnd`, `CGuild`, and guild list symbols, but no class-display path is mapped | Likely roster/list entry formatting backed by guild/member data | Research as a separate roster-display slice | P2 |
| Spawn/name overlays and similar formatted displays | Research artifacts show many display-format strings, but no class-specific seam is mapped | Likely formatter-driven display surface | Defer until explicit product need and concrete path proof | P3 |

## Surface Categories

### 1. Authoritative identity source

These should have exactly one source of truth:

- `statClassesBitmask` captured from `OP_ServerAuthStats`
- the original base class already present in client/profile state

### 2. Gameplay gates

These decide what the player can do:

- spell level eligibility
- spellbook click-to-scribe acceptance
- item or class usability predicates
- equipability checks
- memorize/cast path gates

These should be fixed first.

### 3. Local UI display surfaces

These present the local character to the local player:

- player window
- inventory-related windows
- spellbook-related labels or affordances
- character select

These are important for truthfulness, but should follow once gameplay gates have
stable identity primitives.

### 4. Packet-backed or roster-backed displays

These likely depend on external or structured list data:

- `/who`
- guild manager
- guild roster
- remote member/spawn listings

These may require a different technique from local self UI and should not be
collapsed into the first gameplay slice.

## Recommended Execution Order

### Slice 1: Formalize multiclass identity as an internal API

Goal:

- make the current `statClassesBitmask` logic explicit and reusable

Deliverables:

- small helper API for `has_class`, `primary_class`, `assigned_mask`,
  and future display formatting
- no broad behavior changes yet

Why first:

- it gives later hooks one shared contract instead of ad hoc bitmask logic

### Slice 2: Spellbook click-to-scribe archaeology

Goal:

- identify the exact spellbook drop/click handler that decides whether a scroll
  can be scribed into an empty book slot

Deliverables:

- trace-only hook on the true spellbook UI handler
- proof of whether it reaches a class predicate, a raw mask getter, or an
  earlier spellbook-specific gate
- additional `eqgame.exe` Ghidra recon as needed to map the upstream spellbook
  UI path once runtime evidence narrows the candidate region

Why second:

- the fresh log already proved the failure occurs before current spell seams

### Slice 3: Narrow scribe-enable patch

Goal:

- make manual spellbook click-to-scribe consult authoritative multiclass state
  on the proven upstream gate

Deliverables:

- smallest possible override on the actual blocking seam
- preserve clean-room, fail-closed behavior

### Slice 4: Item/class usability surfaces

Goal:

- extend authoritative multiclass membership to adjacent class-gated item or
  scroll paths that still depend on base class

Candidates:

- `IsClassUsablePredicate`
- `GetUsableClasses`
- `CanEquip`

Rule:

- move in this order from narrowest to broadest blast radius

### Slice 5: Local UI truthfulness

Goal:

- show multiclass identity in self-facing windows without destabilizing gameplay

Candidates:

- player window
- inventory window
- spellbook-facing labels
- character select

Rule:

- prefer additive or formatter-style displays over rewriting unrelated client
  structures globally

### Slice 6: Remote and roster displays

Goal:

- make packet-backed or list-backed displays reflect multiclass truth where
  feasible

Candidates:

- `/who`
- guild manager
- guild roster

Rule:

- handle these as their own evidence-driven project, because they may depend on
  list-entry renderers or received member/spawn data rather than local self
  state

## Decision Rules

- Do not jump straight to global class spoofing unless multiple surfaces prove
  that local adapters are insufficient.
- Prefer context-scoped overrides over global class-return overrides.
- Keep server-authored multiclass state authoritative.
- Keep trace-first discipline on unmapped surfaces.
- Separate gameplay correctness from UI truthfulness when sequencing work.
- Separate local-self display from remote/roster display when choosing hooks.

## Verification Strategy

### Identity core

- confirm `ServerAuthStats valid=true ... statClassesBitmask=...`
- verify helper outputs against known MAG/PAL/MNK assignments

### Spellbook scribe path

- prove the manual click handler was reached
- prove which downstream class or spell gate it consults
- prove the first successful PAL scroll click reaches the expected send or next
  spellbook seam

### Item/class usability

- verify PAL-eligible secondary-class items or scrolls on the exact target path
- ensure unrelated base-class-only items do not gain unintended access

### Local UI

- capture window-specific before/after evidence
- confirm display truthfulness without changing unrelated interactions

### Remote/roster displays

- verify whether the display is local-formatting driven or packet/list-data
  driven before patching

## Immediate Next Action

The next implementation slice should be:

1. keep the current `GetSpellLevelNeeded` multiclass seam as-is
2. pivot trace work to the true spellbook click-to-scribe handler
3. only after that proof, choose between:
   - a narrow class-predicate override
   - a raw mask getter intervention
   - a spellbook-specific acceptance gate patch

That sequence still supports the broader end goal of multiclass-aware UI and
identity, but it avoids widening blindly before the first real spellbook block
is proven.
