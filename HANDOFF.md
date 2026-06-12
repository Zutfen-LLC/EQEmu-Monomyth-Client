# Handoff

## Active Task

Finish the Monomyth multi-pet additions to the stock `PetInfoWindow`.

Current state:
- extra-pet gauges for `EQType 6670` and `6671` are created and visible
- gauge row text is now updated live via direct `CXStr::AssignFromAscii` from `GetGaugeValueFromEQHook`
- the hot draw-time `text_cxstr` sink is also updated from `GetGaugeValueFromEQHook`
- the direct text-write seam is proven hot in live logs with repeated `MultiPetExtraPetGaugeTextUpdate ... draw_text_updated=true window_text_updated=true`
- the dead `OP_RemoteMultiPetStatus` client scaffolding has been removed
- a new client-only fallback now derives other-pet names from the existing spawn/despawn stream plus focused-pet identity from `OP_ServerAuthStats`
- live visual confirmation now shows the two non-focused pet names instead of `No Pet`
- server-side `OP_ServerAuthStats` now emits `statExtraPet0Hp1000 = 100`, `statExtraPet1Hp1000 = 101`, and `statFocusedPetId = 102`, with refreshes on pet-window focus changes and pet HP updates
- live retest is still pending for the new auxiliary HP transport

The root cause of the `No Pet <100%>` text: the stock EQ gauge control stores its display text in a CXStr at CXWnd offset `0xe8`. The XML `<Text>No Pet</Text>` sets the initial value. The stock client only calls `GetLabelFromEQ` for known EQTypes (like 16 for the focused pet gauge), not for custom EQTypes 6670/6671. The percentage `<100%>` is appended by the gauge renderer from the fill value. So the text stayed at the XML default.

## What Is Proven

### PetInfo gauge creation is real

Client runtime proof from `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `extra_pet_gauge0_eqtype_0x1d8=6670`
- `extra_pet_gauge1_eqtype_0x1d8=6671`
- both extra gauge windows are visible and active under `PetInfoWindow`

Conclusion:
- the custom XML controls are instantiated
- we do not need more SIDL archaeology for whether the gauges exist

### Gauge text update seam is live and visible

Current live evidence from `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `MultiPetExtraPetGaugeTextUpdate ... eq_type=6670 ... display_text="Biteyboi000" draw_text_updated=true window_text_updated=true`
- `MultiPetExtraPetGaugeTextUpdate ... eq_type=6671 ... display_text="BONEMAN000" draw_text_updated=true window_text_updated=true`
- screenshot confirmation shows both "Other Pets" rows now render names on screen
- no zone-in crash after the `CXStr::AssignFromAscii` change

Conclusion:
- the visible text source includes the live draw `text_cxstr` sink passed through `GetGaugeValueFromEQ`
- updating the window-owned CXStr alone was not sufficient for visible text in this client build
- updating both sinks is safe in the hot `GetGaugeValueFromEQ` path
- the name-rendering blocker is resolved

### The visible row text was not coming through `GetLabelFromEQ`

Evidence:
- no `MultiPetExtraPetLabelEqTypeTrace` lines in any run
- the stock client does not call `GetLabelFromEQ` for EQType 6670/6671

Conclusion:
- the gauge text CXStr at offset `0xe8` is the actual visible text source
- `GetLabelFromEQ` is not the right seam for these gauge controls
- the text must be written directly via `CXStr::AssignFromAscii`

### Gauge text update seam is installed (v2, crash-fixed)

Code in `GetGaugeValueFromEQHook` at `src/hook_manager.cpp`:
- reads the gauge window pointer from `g_multipet_extra_pet_gauge_windows[slot]`
- computes the text CXStr pointer at `gauge_window + kCxWndTextFieldOffset` (0xe8)
- builds display text via `BuildMultiPetExtraPetDisplayText`
- writes directly via `CXStr::AssignFromAscii` (no window notifications)
- logs `MultiPetExtraPetGaugeTextUpdate` trace

Live validation is now complete for the write mechanism itself.

### Auxiliary HP transport is now wired through `OP_ServerAuthStats`

Current code evidence from `/home/zutfen/code/EQEmu-Monomyth`:
- `common/eq_packet_structs.h` now defines `statExtraPet0Hp1000 = 100` and `statExtraPet1Hp1000 = 101`
- `zone/client.cpp` now sends 6 `OP_ServerAuthStats` entries: class mask, two activated-skill masks, the two auxiliary pet HP values, and the focused pet id
- `Client::ConfigurePetWindow(...)` now calls `SendBulkStatsUpdate()` after focus changes
- `Mob::SendHPUpdate()` now calls `owner->SendBulkStatsUpdate()` for client-owned summoned pets

Validation status:
- client-side parser and gauge consumers were already present and now still compile/tests pass
- server-side touched translation units (`zone/client.cpp`, `zone/mob.cpp`) pass direct `-fsyntax-only` validation using the live build metadata
- live retest is still needed to confirm `ServerAuthStats valid=true ... has_statExtraPet0Hp1000=true ... has_statExtraPet1Hp1000=true ... has_statFocusedPetId=true` and visible bar movement

Conclusion:
- auxiliary bar HP is now on the stock auth-stat path
- the removed custom-name packet path is no longer part of the design

### New client-only fallback seam is installed

Code added:
- `src/multipet_spawn_observer.{h,cpp}`
- `tests/multipet_spawn_observer_tests.cpp`

What it does:
- parses single-spawn packets arriving as `OP_ZoneEntry` / `OP_NewSpawn`
- stores active pet `spawn_id -> { owner_id, name, first_seen_order }`
- reads the focused pet id from `OP_ServerAuthStats`
- removes pets on `OP_DeleteSpawn`
- derives the two "Other Pets" row labels from the focused pet's owner id plus stable first-seen order

Conclusion:
- the client now has a local name source that matches the existing `ConfigurePetWindow()` refresh behavior
- the live packet stream for owned pets produces the expected roster lines and visible names

## What Failed Most Recently

### `CXWnd::SetWindowTextA` from inside `GetGaugeValueFromEQHook` causes zone-in crash

First attempt used `TrySetCxWndTextFromAscii` which calls `CXWnd::SetWindowTextA` on the gauge window. Live log at `2026-06-11 17:55:38` showed:
- `MultiPetExtraPetGaugeEqTypeTrace count=1 eq_type=6670` logged successfully
- no `MultiPetExtraPetGaugeTextUpdate` line (crash before logging it)
- client crashed on zone-in

Root cause: `SetWindowTextA` triggers window notifications and repaint. `GetGaugeValueFromEQ` is called during the UI paint/refresh cycle. Calling `SetWindowTextA` from inside the gauge hook creates reentrancy into the paint cycle.

Fix: replaced with direct `CXStr::AssignFromAscii` on the text field at offset `0xe8`. This writes the string data in-place without triggering any window notifications or repaint.

### `OP_PetBuffWindow` is not a focused-pet identity contract

Why:
- the packet first field is the pet id whose buff list is being serialized, not the client's focused pet selection
- live log around `2026-06-11 21:14` showed the auxiliary row text flipping between `Bitebro` and `MrCuddles` while the visible focused row stayed on `Bitebro`
- non-focused pet buff refreshes can therefore overwrite a client-only `focused_pet_id` cache and let the focused pet leak back into the "Other Pets" list

Fix:
- stop reading focus from `OP_PetBuffWindow`
- send `statFocusedPetId = 102` on `OP_ServerAuthStats`
- derive name exclusion from the server-owned focused pet id plus the local spawn roster

### Server custom name transport is not the active path anymore

Why:
- the client scaffolding has now been removed
- the new client fallback already has enough information to derive names from the stock refresh stream if the spawn packets are observed correctly

Conclusion:
- do not spend the next session reviving `OP_RemoteMultiPetStatus` first
- validate the local spawn-roster seam before reopening any custom protocol work

## Real Next Target

Live-retest the new auxiliary HP transport.

Already proven in `/home/zutfen/everquest_rof2/monomyth-client.log`:
- `MultiPetSpawnRosterObserve ... spawn_id=... owner_id=... name="..."`
- `MultiPetExtraPetGaugeTextUpdate ... display_text="Biteyboi000" draw_text_updated=true window_text_updated=true`
- `MultiPetExtraPetGaugeTextUpdate ... display_text="BONEMAN000" draw_text_updated=true window_text_updated=true`

Next live proof to look for:
- `ServerAuthStats valid=true ... entry_count=6 ... has_statExtraPet0Hp1000=true ... has_statExtraPet1Hp1000=true ... has_statFocusedPetId=true`
- `MultiPetExtraPetGaugeEqTypeTrace ... server_auth_has_value=true`
- stable `MultiPetExtraPetGaugeTextUpdate` names after pet buff churn
- visible movement in the two auxiliary bars while the non-focused pets take damage

## Next Session Plan

1. Open or refocus the pet window in a live run and check the log for:
   - `ServerAuthStats valid=true ... entry_count=6 ... has_statFocusedPetId=true`
   - `MultiPetExtraPetGaugeEqTypeTrace ... server_auth_has_value=true`
   - `MultiPetSpawnRosterObserve`
   - stable `MultiPetExtraPetGaugeTextUpdate` names that do not flip to the focused pet after buff updates
2. Damage the two non-focused pets and confirm the "Other Pets" bars move.
3. If the names are right but the bars stay empty:
   - check whether the active run is using the newly staged client DLL and updated server build
   - compare the logged `focused_pet_id` and returned `extra_pet*_hp_1000` values against the visible row order before changing client mapping

## Current Code State

Client repo changes of interest:
- `src/hook_manager.cpp`
- `src/multipet_spawn_observer.{h,cpp}`
- `src/packet_observer.cpp`
- `CMakeLists.txt`
- `tests/server_auth_stats_observer_tests.cpp`
- `tests/multipet_spawn_observer_tests.cpp`

Server repo changes of interest:
- `zone/client.cpp`
- `zone/mob.cpp`
- `common/eq_packet_structs.h`
- `/home/zutfen/code/EQEmu-Monomyth/CHANGELOG.md`

## Important Negative Results Already Proven

Do not reopen these without new contrary evidence:
- `PetInfoWindow` child creation absence theory
  - disproven: the extra gauges are created
- `0..255` gauge scale theory
  - disproven: stock focused-pet gauge uses `0..1000`
- "name text must be owned by `GetLabelFromEQ(6670/6671)`"
  - disproven: stock client never calls `GetLabelFromEQ` for these EQTypes
- `CXWnd::SetWindowTextA` from inside `GetGaugeValueFromEQHook`
  - disproven: causes reentrancy crash on zone-in; use `CXStr::AssignFromAscii` directly instead
- "the current checkout is still sending extra-pet HP/name transport from the server"
  - disproven for the old checkpoint only: the branch now sends extra-pet HP through `OP_ServerAuthStats`, but there is still no `OP_RemoteMultiPetStatus` path
- "`OP_PetBuffWindow` first field is the focused pet id"
  - disproven: it is the pet id whose buff payload is being sent, so it can drift to non-focused pets during ordinary buff refreshes

## Validation Loop

Focused client tests:
- `ctest --test-dir build --output-on-failure -R "runtime_capabilities_tests|class_display_discovery_tests|multiclass_identity_tests|spell_level_selection_tests|server_auth_stats_observer_tests|remote_multiclass_identity_tests|multipet_spawn_observer_tests"`

Build and stage:
- `cmake --build build-cross-i686 -j4`
- copy `build-cross-i686/dinput8.dll` to `/home/zutfen/everquest_rof2/dinput8.dll`
- verify hashes match
- current staged DLL hash: `ea2bc5422f5d51547f2e2e3e243e0c311f88e51a4f651b88182cdf1fd68e958c`

Server build:
- full build directory `build` currently has stale `ninja` state and restarts cold
- fastest compile validation used this turn:
  - `ninja -C build zone/CMakeFiles/zone.dir/client.cpp.o`
- next live retest still needs a real updated zone binary deployment

Live proof source:
- `/home/zutfen/everquest_rof2/monomyth-client.log`
