# Handoff

## Active Task

Finish the Monomyth multi-pet additions to the stock `PetInfoWindow`.

Current state:
- extra-pet gauges for `EQType 6670` and `6671` are created and visible
- gauge row text is updated live via direct `CXStr::AssignFromAscii` from `GetGaugeValueFromEQHook`
- the hot draw-time `text_cxstr` sink is also updated from `GetGaugeValueFromEQHook`
- live logs repeatedly show `MultiPetExtraPetGaugeTextUpdate ... draw_text_updated=true window_text_updated=true`
- the dead `OP_RemoteMultiPetStatus` client scaffolding has been removed
- a client-only fallback now derives other-pet names from the spawn/despawn stream plus focused-pet identity from `OP_ServerAuthStats`
- live visual confirmation now shows the two non-focused pet names instead of `No Pet`
- server-side `OP_ServerAuthStats` now emits `statExtraPet0Hp1000 = 100`, `statExtraPet1Hp1000 = 101`, and `statFocusedPetId = 102`
- PetInfoWindow click routing for the two auxiliary rows now uses sender-window identity plus local roster `spawn_id` mapping to call the stock `CEverQuest::LeftClickedOnPlayer` path
- direct `PlayerManagerClient::GetSpawnByID` from inside `PetInfoWindow::WndNotification` is disproven for this click path
- the current click path resolves the target spawn by bounded walk of `PlayerManagerClient::PlayerList` before calling `CEverQuest::LeftClickedOnPlayer`
- the non-MSVC `TryCopyBytes` path now uses `ReadProcessMemory(GetCurrentProcess(), ...)` instead of raw `memcpy`
- the spawn-list walker now emits bounded `PetInfoSpawnWalkTrace` steps so the next click retest can prove the exact unreadable pointer or loop state before a failure

The root cause of the visible `No Pet <100%>` text was that the stock EQ gauge control stores its display text in a `CXStr` at `CXWnd + 0xe8`. The XML `<Text>No Pet</Text>` initializes that field, and the stock client never calls `GetLabelFromEQ` for custom EQTypes `6670` and `6671`. The percentage suffix comes from the gauge renderer, so the text had to be written directly into the gauge-owned `CXStr`.

## What Is Done

### PetInfo gauge creation is real

Client runtime proof from `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `extra_pet_gauge0_eqtype_0x1d8=6670`
- `extra_pet_gauge1_eqtype_0x1d8=6671`
- both extra gauge windows are visible and active under `PetInfoWindow`

Conclusion:
- the custom XML controls are instantiated
- more SIDL archaeology is not needed for whether the gauges exist

### Gauge text update seam is live and visible

Current live evidence from `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `MultiPetExtraPetGaugeTextUpdate ... eq_type=6670 ... display_text="Biteyboi000" draw_text_updated=true window_text_updated=true`
- `MultiPetExtraPetGaugeTextUpdate ... eq_type=6671 ... display_text="BONEMAN000" draw_text_updated=true window_text_updated=true`

Conclusion:
- the visible text source includes the live draw `text_cxstr` sink passed through `GetGaugeValueFromEQ`
- updating the window-owned `CXStr` alone was not sufficient for visible text in this client build
- updating both sinks is safe in the hot `GetGaugeValueFromEQ` path
- the name-rendering blocker is resolved

### Auxiliary HP transport is now wired through `OP_ServerAuthStats`

Server-side evidence from `/home/zutfen/code/EQEmu-Monomyth`:
- `common/eq_packet_structs.h` defines `statExtraPet0Hp1000 = 100` and `statExtraPet1Hp1000 = 101`
- `zone/client.cpp` now sends the extra-pet HP values and focused pet id on `OP_ServerAuthStats`
- `Client::ConfigurePetWindow(...)` and `Mob::SendHPUpdate()` trigger refreshes on focus and HP changes

Conclusion:
- auxiliary bar HP is now on the stock auth-stat path
- the removed custom-name packet path is no longer part of the design

### New client-only fallback seam is installed

Code added:
- `src/multipet_spawn_observer.{h,cpp}`
- `tests/multipet_spawn_observer_tests.cpp`

What it does:
- parses single-spawn packets arriving as `OP_ZoneEntry` and `OP_NewSpawn`
- stores active pet `spawn_id -> { owner_id, name, first_seen_order }`
- reads the focused pet id from `OP_ServerAuthStats`
- removes pets on `OP_DeleteSpawn`
- derives the two "Other Pets" row labels from the focused pet's owner id plus stable first-seen order

### Auxiliary row target click seam is now wired

Code evidence:
- `src/hook_manager.cpp` matches `PetInfoWindow::WndNotification` sender pointers against the cached `MMPIW_ExtraPet0_Gauge` and `MMPIW_ExtraPet1_Gauge` windows
- `src/multipet_spawn_observer.{h,cpp}` exposes the two auxiliary `spawn_id` values alongside the rendered names
- the click handler now uses bounded spawn-walk resolution before routing through the stock `CEverQuest::LeftClickedOnPlayer` path

## Key Risk

### `CXWnd::SetWindowTextA` from inside `GetGaugeValueFromEQHook` causes zone-in crash

Calling `SetWindowTextA` from the gauge hook caused reentrancy into the UI refresh path and crashed on zone-in. The fix is to write directly to the `CXStr` at offset `0xe8` via `CXStr::AssignFromAscii`.

### `OP_PetBuffWindow` is not a focused-pet identity contract

The packet's first field is the pet id whose buff payload is being serialized, not the player's current focus target. Focus must come from the new `statFocusedPetId` auth-stat path instead.

### Direct `GetSpawnByID` from the PetInfo click seam crashes before target apply

The row match and singleton resolution were correct; the unsafe part was calling `PlayerManagerClient::GetSpawnByID` from inside `PetInfoWindow::WndNotification`. The current bounded-walk resolution is the safe replacement and still needs live confirmation.

### The cross-built `TryCopyBytes` path was not fault-safe

The non-MSVC path previously used raw `memcpy`, which could still hard-fault on an invalid node pointer. It now uses `ReadProcessMemory(GetCurrentProcess(), ...)` so the click-path tracing can fail closed instead of crashing immediately on unreadable memory.

## Real Next Target

Live-retest the new auxiliary HP transport and auxiliary-row targeting.

Already proven in `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `MultiPetSpawnRosterObserve ... spawn_id=... owner_id=... name="..."`
- `MultiPetExtraPetGaugeTextUpdate ... display_text="Biteyboi000" draw_text_updated=true window_text_updated=true`
- `MultiPetExtraPetGaugeTextUpdate ... display_text="BONEMAN000" draw_text_updated=true window_text_updated=true`

Next live proof to look for:
- `ServerAuthStats valid=true ... entry_count=6 ... has_statExtraPet0Hp1000=true ... has_statExtraPet1Hp1000=true ... has_statFocusedPetId=true`
- `MultiPetExtraPetGaugeEqTypeTrace ... server_auth_has_value=true`
- `MultiPetPetInfoTargetClickTrace ... resolution="target_applied"`
- visible movement in the two auxiliary bars while the non-focused pets take damage

## Next Session Plan

1. Open or refocus the pet window in a live run and check the log for:
   - `ServerAuthStats valid=true ... has_statFocusedPetId=true`
   - `PetInfoSpawnWalkTrace ...`
   - `MultiPetExtraPetGaugeEqTypeTrace ... server_auth_has_value=true`
   - stable `MultiPetExtraPetGaugeTextUpdate` names
   - `MultiPetPetInfoTargetClickTrace ... resolution="target_applied"`
2. Damage the two non-focused pets and confirm the "Other Pets" bars move.
3. Click each non-focused pet row and confirm it becomes the live target and/or pet-window focus.
4. If the names are right but the bars stay empty, compare the logged `focused_pet_id`, `other_pet_spawn_id`, and returned `extra_pet*_hp_1000` values against the visible row order before changing client mapping.

## Additional Landed Work From `main`

Ammo slot override support is now also present on this branch:
- `InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook` can now override ammo-slot rejection when the item's class mask intersects the authoritative multiclass mask
- supporting diagnostics include `AmmoSlotEquipOverride` and `AmmoSlotEquipResolveFailed`
- the related `tests/multiclass_identity_tests.cpp` cases landed from `main`

Live ammo-slot validation is still pending separately.

## Current Code State

Client repo changes of interest:
- `src/hook_manager.cpp`
- `src/multipet_spawn_observer.{h,cpp}`
- `src/packet_observer.cpp`
- `CMakeLists.txt`
- `tests/server_auth_stats_observer_tests.cpp`
- `tests/multipet_spawn_observer_tests.cpp`
- `tests/multiclass_identity_tests.cpp`

Server repo changes of interest:
- `zone/client.cpp`
- `zone/mob.cpp`
- `common/eq_packet_structs.h`

## Important Negative Results Already Proven

Do not reopen these without new contrary evidence:
- the extra gauges failing to instantiate
- a `0..255` gauge-scale theory for the extra pet bars
- relying on `GetLabelFromEQ(6670/6671)` for visible extra-row text
- calling `CXWnd::SetWindowTextA` from inside `GetGaugeValueFromEQHook`
- treating `OP_PetBuffWindow` as the focused-pet identity source
- direct `PlayerManagerClient::GetSpawnByID` from the PetInfo click seam

## Validation

Focused tests:
- `ctest --test-dir build-cross-i686 --output-on-failure -R "runtime_capabilities_tests|class_display_discovery_tests|multiclass_identity_tests|spell_level_selection_tests"`

Build and stage:
- `cmake --build build-cross-i686 -j4`
- copy `build-cross-i686/dinput8.dll` to `/home/zutfen/everquest_rof2/dinput8.dll`

Live proof source:
- `/home/zutfen/everquest_rof2/monomyth-client.log`
