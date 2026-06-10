# Handoff

## Active Task

Fix the ammo slot (slot 17) equip for multiclass characters. A WIZ(primary)/SHD/RNG character cannot equip arrows to the ammo slot because the client's Slot17Gate rejects based on primary class.

## What Is Done

### Ammo slot override implemented

Added multiclass override to `InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook` at `src/hook_manager.cpp:25610`:

- When `original_result == 0` (client rejected) and `g_multiclass_item_usability_enabled`:
  - Tries multiple item resolution strategies to find the item wrapper pointer:
    1. `manager_before.field_244` as wrapper pointer
    2. Saved `item_id_like` from `ItemRangeGate` as wrapper pointer
    3. `manager_before.field_248` as wrapper pointer
    4. `g_invslot_handle_lbutton_core_last_late_lookup_item_pointer` fallback
    5. Raw pointer reads on `field_244` and range gate item ID
  - If item resolved, reads class mask and checks `HasAnyAuthoritativeClientItemClass`
  - If class mask intersects authoritative multiclass mask, overrides to return 1

### Supporting changes

- `g_ammo_slot_override_count` counter added and reset in both enable/disable paths
- `g_invslot_handle_lbutton_core_last_range_gate_item_id` global saved from `ItemRangeGate` hook
- Diagnostic logging: `AmmoSlotEquipOverride` on success, `AmmoSlotEquipResolveFailed` if item resolution fails
- Unit tests for WIZ/SHD/RNG + arrow class mask added to `tests/multiclass_identity_tests.cpp`
- All 5 focused tests pass

### Build state

- DLL built, copied to `/home/zutfen/everquest_rof2/dinput8.dll`
- SHA-256 `8be9e19608ceb420fc9ccf838d32fff2948dcfce69bd2905ce9f1f7f68814aa5`

## What Needs Live Validation

1. Start the client, log in as WIZ/SHD/RNG character
2. Drag an arrow to the ammo slot
3. Check `/home/zutfen/everquest_rof2/monomyth-client.log` for:
   - `AmmoSlotEquipOverride` → override fired, arrow should be equipped
   - `AmmoSlotEquipResolveFailed` → item resolution failed, need to adjust which candidate works
   - `InvSlotHandleLButtonCoreLateSlot17Gate` trace → shows original_result and manager context fields
4. If `AmmoSlotEquipResolveFailed` appears, the log shows the raw values of all candidates (field_244, field_248, range_gate_item_id, last_late_lookup). Use these to determine which field holds the item wrapper.

## Key Risk

The item resolution is multi-strategy because it's unknown which context field contains the item wrapper pointer on the Slot17 path. The diagnostic log will reveal the correct source. If none of the candidates contain valid item pointers, a new resolution strategy will be needed (e.g., reading from the drag context directly).

## Previous Task (Paused)

The Disciplines window fix is paused. The dispatcher seam at VA `0x4d8497` remains the next target for that work. See git history for the previous HANDOFF.md content.

## Validation

Focused tests:
- `ctest --test-dir build-cross-i686 --output-on-failure -R "runtime_capabilities_tests|class_display_discovery_tests|multiclass_identity_tests|spell_level_selection_tests"`

Build and stage:
- `cmake --build build-cross-i686 --target dinput8 -j4`
- copy `build-cross-i686/dinput8.dll` to `/home/zutfen/everquest_rof2/dinput8.dll`

Live proof source:
- `/home/zutfen/everquest_rof2/monomyth-client.log`
