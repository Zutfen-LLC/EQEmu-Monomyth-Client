# Plan: Arrow/Ammo Slot Equip for Multiclass Characters

## Problem

A Wizard(Primary)/Shadowknight/Ranger character cannot equip an arrow to the ammo slot (slot 17). The arrow's class mask includes RNG (class 4), which is in the character's authoritative multiclass mask, but the client rejects the equip because the primary class is WIZ.

## Root Cause Analysis

The ammo slot (slot 17) has a **specialized code path** in `CInvSlot::HandleLButtonUp` → `InvSlotHandleLButtonCore` that bypasses the generic equipment override hooks:

```
Slot17Message → ItemRangeGate → LateSlot17Gate → LateSlot17Apply
```

All four hooks are **trace-only** with no multiclass override:
- `InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook` (`src/hook_manager.cpp:25607`)
  - Callsite RVA: `0x00295a37`
  - Target RVA: `0x002f0180`
  - Currently returns `original_result` without override

The existing multiclass override chain doesn't catch this because:
1. The Slot17 path is a separate branch in the client that doesn't go through `LateBranchPrep`/`LateBranchDispatch`
2. `CanEquip` inline detour may not be called by this path (needs runtime confirmation)
3. The `EquipmentClassPolicySnapshot` generic fallback lives in `LateBranchDispatch`, which the Slot17 path bypasses

## Fix Pattern

This is structurally identical to the augment fix (`AugmentPlacementValidatorCallsiteHook`). The augment hooks override a class validator's rejection when the item's class mask intersects the authoritative multiclass mask. We apply the same pattern to the Slot17Gate.

## Implementation Steps

### Step 1: Runtime Confirmation (Trace Run)

Before adding the override, confirm the flow:

1. Build the DLL as-is (trace-only hooks already installed for Slot17Gate)
2. User attempts to equip an arrow to the ammo slot
3. Check log for:
   - `InvSlotHandleLButtonCoreLateSlot17Gate` trace with `original_result=0` → confirms Slot17Gate is the blocker
   - `CanEquipOverride` or `CanEquipObservation` traces → confirms whether CanEquip also fires
   - `InvSlotHandleLButtonCoreItemRangeGate` → confirms range gate passed (arrows have range)
4. Expected: Slot17Gate fires and returns 0; CanEquip may or may not fire

### Step 2: Add Multiclass Override to `LateSlot17GateCallsiteHook`

Modify `InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook` at `src/hook_manager.cpp:25607`:

```cpp
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer) noexcept {
    // ... existing trace code (keep) ...

    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_late_slot17_gate(...)
        // ... existing call ...

    // NEW: multiclass override for ammo slot
    if (g_multiclass_item_usability_enabled && original_result == 0) {
        // Read item from slot record or context
        const std::uintptr_t item_like = ResolveSlot17ItemLike(...);
        const monomyth::server_auth_stats::Snapshot snapshot =
            monomyth::server_auth_stats::GetSnapshot();
        const std::uint32_t item_class_mask = ReadClientItemClassMask(item_like);
        const bool item_matches =
            monomyth::multiclass_identity::HasAnyAuthoritativeClientItemClass(
                snapshot.has_classes_bitmask,
                snapshot.classes_bitmask,
                item_class_mask);
        if (item_matches) {
            LogAmmoSlotOverride(...);
            return 1;
        }
    }

    // ... existing log and return ...
    return original_result;
}
```

Key details:
- Follow the same pattern as `CaptureEquipmentClassPolicySnapshot` for reading the item's class mask
- The item data needs to be resolved from the `this_context` or `slot_record_pointer` (the exact resolution method depends on what's available in the Slot17Gate context — use the same `ResolveLateBranchItemLike` or read from the slot record's item pointer)
- Log the override for diagnostics (follow the `LogAugmentPlacementValidatorOverride` pattern)
- Gate behind `g_multiclass_item_usability_enabled`
- Only override when `original_result == 0` (client rejected)

### Step 3: Item Resolution for Slot17 Context

The slot_record in the Slot17Gate may contain an item pointer/index. We need to:

1. Read the item wrapper/pointer from `slot_record_pointer` or `this_context`
2. Use `TryReadClientItemClassMaskFromWrapper` (same as `CaptureEquipmentClassPolicySnapshot` uses at line 14348)
3. If item resolution fails, fail-closed (return `original_result`)

This is the riskiest part. The existing `LateBranchPrep`/`LateBranchDispatch` hooks resolve the item via `ResolveLateBranchItemLike(lookup_dword0_before, ...)`, but the Slot17Gate may not have the same `lookup_result_like` available. Options:
- **Option A**: Read item data directly from the `slot_record_pointer` fields
- **Option B**: Read from `this_context` (the slot manager) at the same offset used by other hooks
- **Option C**: Use the `CaptureInvSlotHandleLButtonCoreLateManagerContext` snapshot to find the item pointer

The exact approach depends on what the trace data from Step 1 reveals about the slot record contents.

### Step 4: Add Override Counter and Logging

Follow the augment pattern:
- Add `g_ammo_slot_override_count` counter
- Add `LogAmmoSlotEquipOverride` function (follow `LogAugmentPlacementValidatorOverride` pattern)
- Add `LogAmmoSlotEquipObservation` for trace when override conditions not met

### Step 5: Add Unit Test

Add test cases to `tests/multiclass_identity_tests.cpp`:
- WIZ/SHD/RNG mask + arrow class mask (includes RNG) → `HasAnyAuthoritativeClientItemClass` returns true
- WIZ/SHD mask + arrow class mask (no RNG) → returns false

These tests validate the class-mask intersection logic, not the hook wiring.

### Step 6: Build, Deploy, Validate

1. `cmake --build build-cross-i686 --target dinput8 -j4`
2. Copy `build-cross-i686/dinput8.dll` to `/home/zutfen/everquest_rof2/`
3. User tests: drag arrow to ammo slot → should succeed
4. Check log for `AmmoSlotEquipOverride` confirmation
5. Verify arrow appears in ammo slot and functions correctly (ranged attack with RNG class)

## Risk Assessment

- **Low risk**: The override only fires when `original_result == 0` AND the item's class mask intersects the authoritative multiclass mask. Non-multiclass characters and unrelated items are unaffected.
- **Medium risk**: Item resolution in the Slot17Gate context may need adjustment depending on what data is available. The trace run (Step 1) will inform the exact approach.
- **Crash risk**: Lower than previous inventory-scan CanEquip overrides (which were disproven) because the Slot17Gate is a focused, single-purpose gate for the ammo slot only.

## Confirmed Behavior

- **Equip method**: Drag-and-drop to the ammo slot
- **Failure mode**: Error message popup (client class restriction rejection)
- **Character**: WIZ(primary)/SHD/RNG — arrow class mask includes RNG but not WIZ/SHD
- **Flow**: `InvSlotHandleLButtonCore` → Slot17Gate path → class check rejects → error popup

## Execution Order

1. **Trace run first** — build current DLL, user drags arrow to ammo, check log for Slot17Gate trace
2. **Add override** — modify Slot17Gate hook with multiclass class-mask intersection check
3. **Test and validate** — rebuild, redeploy, user confirms arrow equips and ammo slot works
