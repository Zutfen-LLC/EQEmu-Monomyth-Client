# Multiclass Augmentation Override

## Problem

A multiclass character (e.g., Paladin/Monk/Rogue) can equip a Monk-only item via the existing `CanEquip` hook, but cannot add augmentations to that item. Augmentations work fine on items usable by the primary class (Paladin). The client-side augmentation validation rejects the combination because the augment's class mask and the target item's class mask have no overlap — producing what the client considers "an item unusable by anyone."

## Root Cause

The augmentation insertion path does NOT go through `EQ_Character::CanEquip` or `EQ_Character::IsClassUsablePredicate`. It has its own inline compatibility validation chain:

```
CItemDisplayWnd::InsertAugmentRequest (VA 0x6A8190, RVA 0x2A8190)
  → 0x7bcdb0 (augment placement validator)
    → 0x7bbec0 (cumulative compatibility computation)
      → 0x7b4ce0 (recursive Classes AND, reads item offset 0x170)
      → 0x7b4e30 (recursive Races AND, reads item offset 0x174)
      → 0x7b2210 (recursive Slots AND, reads item offset 0xF0)
```

`0x7bbec0` computes cumulative AND masks for **Classes** (offset `0x170`), **Races** (`0x174`), and **Deity** (`0x178`) across the target item and all its existing augments, then ANDs those with the cursor augment's own masks. If any cumulative mask reaches zero, the function returns a non-zero error code and `InsertAugmentRequest` shows an error:

| Error code | EQStr ID | Likely meaning | Message shown |
|---|---|---|---|
| 1 | 3365 | Cumulative Deity/Races AND = 0 | "The result of this combine would produce an item unusable by anyone." |
| 2 | 3366 | Cumulative Classes AND = 0 | "The selected slot already contains an augment." (semantically wrong, but that's the mapping) |
| 3 | 5476 | Cumulative Deity with cursor augment AND = 0 | "The selected slot was invalid." |
| 0xB | 5475 | Slot type mismatch | "You attempted to insert an item that is not an augment." |

User has confirmed: the rejection produces an error message in chat.

For a Monk-only item + Paladin-class augment:
- Target item Classes = Monk bit only
- Augment Classes = includes Paladin
- AND = 0 (no single class is both Monk and Paladin)
- Rejected even though the multiclass character has BOTH classes

**The server-side `Handle_OP_AugmentItem` handler does NOT perform class/race validation** — it only checks AugSlotType matching and wear slot overlap. The block is entirely client-side.

## Existing Hooks (insufficient)

| Hook | Covers augmentation flow? |
|---|---|
| `IsClassUsablePredicate` inline detour | No — aug path has its own inline class check |
| `CanEquip` inline detour | No — `InsertAugmentRequest` never calls `CanEquip` |
| `EquipClickCanEquipCallsiteHook` | No — different UI flow |
| `DragDropSilentPrecheckCallsiteHook` | No — diagnostic only, no override |

## Plan

### Phase 1: Diagnostic trace (bounded, no behavior change)

Add tracing to confirm the exact error code returned by the validation chain. This is cheap insurance — the disassembly is complex enough that a runtime confirmation is warranted before shipping the override.

1. **Add a callsite patch** on the `call 0x7bcdb0` inside `InsertAugmentRequest` (VA `0x6A8352`, RVA `0x2A8352`).
   - The callsite hook receives the same args as the original callsite context
   - Log: `MulticlassAugmentTrace target=AugmentPlacementValidator error_code=<eax> target_item=<ptr> augment=<ptr> slot=<ebp>`
   - If error_code is non-zero, also read and log the target item's class mask (offset `0x68`) and the augment's class mask (offset `0x68`)
   - No behavior change — always return the original result

2. **Build, deploy, test**: Attempt to augment a Monk-only item. Check `monomyth-client.log` for `MulticlassAugmentTrace` entries with non-zero error codes.

### Phase 2: Implement augmentation compatibility override

The cleanest seam is a **callsite patch** on the `call 0x7bcdb0` inside `InsertAugmentRequest`. This intercepts the error code with full context (item pointers, slot index) without touching the large recursive validation functions.

**Callsite RVA**: `0x2A8352` (the `call 0x7bcdb0` at VA `0x6A8352`)

**Hook signature**: `int __fastcall Hook(int original_result, void*, ...original_args...)` — receives eax (error code) and the same thiscall/stack context.

**Override logic**:

```
If original_result == 0:            // validation passed
  return 0                          // no override needed

If not multiclass_item_usability_enabled:
  return original_result            // feature disabled

snapshot = GetSnapshot()            // server auth stats
target_class_mask = ReadClientItemClassMask(target_item)    // offset 0x68
augment_class_mask = ReadClientItemClassMask(cursor_aug)    // offset 0x68

target_covered = HasAnyAuthoritativeClientItemClass(
    snapshot.classes_bitmask, target_class_mask)
augment_covered = HasAnyAuthoritativeClientItemClass(
    snapshot.classes_bitmask, augment_class_mask)

If target_covered AND augment_covered:
  Log "MulticlassAugmentOverride" with details
  return 0                          // override: allow the augmentation
Else:
  return original_result            // fail-closed
```

**Safety properties**:
- Both the target item AND the augment must independently have class overlap with the character's multiclass set
- A character that is Paladin/Monk/Rogue can combine a Monk-only item with a Paladin-only augment because they have BOTH classes
- A character that is Paladin/Monk cannot combine a Monk-only item with a Rogue-only augment — neither class overlaps Rogue
- Items/augments completely outside the character's class set are still rejected (fail-closed)
- Race and Deity incompatibility errors are also overridden by this hook, which is acceptable because the server doesn't enforce them either

### Phase 3: Validation

1. Build: `cmake --build build-cross-i686 -j4`
2. Deploy: `cp build-cross-i686/dinput8.dll /home/zutfen/everquest_rof2/`
3. Unit tests: `ctest --test-dir build --output-on-failure -R "runtime_capabilities_tests|class_display_discovery_tests|multiclass_identity_tests"`
4. Live test: augment a non-primary-class item on a multiclass character — should succeed
5. Live negative test: attempt to augment with an item/augment combo where NEITHER class is in the multiclass set — should still be rejected

## Alternative Approaches Considered

### Inline detour on `0x7bcdb0` or `0x7bbec0`
- Pros: catches all callers, not just `InsertAugmentRequest`
- Cons: these are large functions with complex calling conventions; much higher risk of instability. The `0x7bbec0` function is ~1700 bytes of x86 with recursive callbacks.

### Patch the cumulative AND checks inside `0x7bbec0`
- Pros: most surgical fix
- Cons: would require patching multiple comparison sites deep inside a large function; fragile to binary differences

### Hook at the `InsertAugmentRequest` entry level (pre-check bypass)
- Pros: simplest code
- Cons: bypasses ALL validation including legitimate checks (slot type matching, distiller requirements, etc.)

### Callsite patch on `0x7bcdb0` return (CHOSEN)
- Pros: clean single-point intercept, has full context (error code + item pointers), only overrides when class compatibility is the sole issue
- Cons: only covers `InsertAugmentRequest` path; if another path calls `0x7bcdb0` it wouldn't be covered. But `InsertAugmentRequest` is the only known caller for the direct augment UI flow.

## Key RVAs for Implementation

| Symbol | VA | RVA | Role |
|---|---|---|---|
| `CItemDisplayWnd::InsertAugmentRequest` | `0x6A8190` | `0x2A8190` | Main augment insertion entry |
| Augment validation call inside above | `0x6A8352` | `0x2A8352` | `call 0x7bcdb0` |
| Validation call return site | `0x6A8357` | `0x2A8357` | Where we intercept the return |
| Error display path | `0x6A83C7` | `0x2A83C7` | Where errors are shown |
| Packet send path | `0x6A84D9` | `0x2A84D9` | `push 0x661b` (OP_AugmentItem) |
| `0x7bcdb0` (placement validator) | `0x7BCDB0` | `0x3BCDB0` | Calls `0x7bbec0` |
| `0x7bbec0` (compatibility computer) | `0x7BBEC0` | `0x3BBEC0` | Cumulative AND of Classes/Races/Deity |
| `0x7b4ce0` (recursive Classes AND) | `0x7B4CE0` | `0x3B4CE0` | Reads item offset `0x170` (Classes) |
| `0x7b4e30` (recursive Races AND) | `0x7B4E30` | `0x3B4E30` | Reads item offset `0x174` (Races) |
| `0x7b2210` (recursive Slots AND) | `0x7B2210` | `0x3B2210` | Reads item offset `0xF0` (Slots/EquipSlots) |
| `0x7afe40` (error code → string) | `0x7AFE40` | `0x3AFE40` | Switch table for augment error messages |
