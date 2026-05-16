# dinput8.dll reverse-engineering notes

Target: `/home/zutfen/dinput8.dll`
Generated: 2026-05-15
Analyst: Hermes via Ghidra headless + objdump/strings

## 1. Quick identification

- File type: PE32 DLL, x86
- Size: 1,608,192 bytes
- PE timestamp: 2025-05-20 23:56:55
- SHA-256: `55076b08c3f39bb3a40525e32311716e45a5d3a11129a8de038426b077294b25`
- PDB path embedded: `C:\Users\catapultam\Documents\GitHub\THJ\eqemupatcher\rof\dinput8.pdb`

This is not a stock DirectInput DLL. It is a custom proxy/injection DLL for the EverQuest ROF2-era client with heavy MacroQuest/MQ2-style functionality plus THJ/custom-server specific additions.

Important strings and identifiers found:
- `MQ2Main.dll`
- `MacroQuest`
- `applying mq2 injects`
- `initializing mq2 hooks`
- `mq2 prevention enabled`
- `CreateDevice Hook called`
- `Initializing chat hook`
- `Initializing Display Hooks`
- `Initializing Spawn-related Hooks`
- `CCommandHook::Detour(%s)`
- `eqgame.exe`
- `eqgame.ini`
- `macroquest.ini`

## 2. High-level architecture

At a high level, the DLL does four things:

1. Acts as a `dinput8.dll` proxy so the game loads it naturally.
2. Wraps DirectInput interfaces so it can hook device creation and input behavior.
3. Initializes a large MQ2-style runtime:
   - chat hooks
   - display hooks
   - command interception/registration
   - MQ2Data and MQ2Type systems
   - pulse/event hooks
   - spawn tracking
   - XML/window/UI hooks
4. Applies direct in-memory patches to the client using `VirtualProtect`, `FlushInstructionCache`, `ReadProcessMemory`, and `WriteProcessMemory`.

This is much more than a loader. It is effectively a merged proxy DLL + detour framework + MQ2 runtime + custom game patches.

## 3. DirectInput proxy / COM wrapper layer

### Exported entry point: `DirectInput8Create`
Address: `0x100c2470`

Decompile summary:
```c
int DirectInput8Create(..., int *out_iface) {
  if (DAT_1017bfc0 == 0) return failure;
  hr = (*DAT_1017bfc0)(...);
  if (hr >= 0) FUN_100c69c0(out_iface, ...);
  return hr;
}
```

Interpretation:
- `DAT_1017bfc0` is the real DirectInput factory function pointer.
- On success, the returned interface is wrapped by local code (`FUN_100c69c0`).
- This is a classic proxy DLL pattern.

### Device creation hook
Function: `FUN_100c5cd0` at `0x100c5cd0`

Decompile summary:
```c
OutputDebugStringA("CreateDevice Hook called\n");
real_result = real_CreateDevice(...);
if (real_result >= 0) {
  hook_obj = alloc(0x1c);
  hook_obj->vftable = IDirectInputDevice8Hook::vftable;
  hook_obj->parent = param_1;
  hook_obj->real_device = *param_3;
  copy_guid(...);
  *param_3 = hook_obj;
}
```

Interpretation:
- The DLL wraps `IDirectInputDevice8` objects in a custom `IDirectInputDevice8Hook` vtable.
- The DLL also defines `IDirectInput8Hook` and `IDirectInputDevice8Hook` RTTI/vtables.
- This is the interception point for keyboard/mouse/device behavior.

Implication for a custom client:
- If you want MQ2-like input interception without the exact original code, you need:
  - a `dinput8.dll` proxy loader,
  - a wrapper for the returned `IDirectInput8` interface,
  - a wrapper for each `CreateDevice` result,
  - your own vtable shims for the device methods you care about.

## 4. Detour framework

### `AddDetour`
Address: `0x100ea880`

Behavior:
- Uses a critical section (`DAT_1017c180`) to guard detour state.
- Maintains a linked list of installed detours at `DAT_1017c16c`.
- Allocates a detour record (~0x4a bytes).
- Stores target pointer, saved bytes, and the two function pointers/byte blocks passed in.
- Calls `FUN_10001680(param_3, (char*)param_1, param_2)` to apply the patch.
- Logs `Detour success.` or `Detour failed.`.

Observed fields from decompile:
- offset `+0x00`: target address
- offset `+0x04...`: copied original bytes / metadata
- offsets `+0x3a` and `+0x3e`: patch/original helper pointers used during removal
- offsets `+0x42` and `+0x46`: next/prev linked-list pointers

### `RemoveDetour`
Address: `0x100ed400`

Behavior:
- Looks up a detour record by target address.
- If restore data exists, calls `FUN_10001000(saved_patch_helper, saved_original_helper)`.
- Unlinks the detour record from the doubly linked list.
- Frees the record.

### `AddDetourf`
Address: `0x100ed4e0`

Behavior:
- A convenience wrapper around `AddDetour`.
- Pulls three values from the caller-provided stack frame / sentinel layout and passes them to `AddDetour(..., 0x14)`.
- Default patch size appears to be `0x14` bytes.

Practical conclusion:
- This DLL has an internal generic detour manager.
- Most subsystem init functions simply call `AddDetourf(symbol)` repeatedly.
- If you are rebuilding this for a custom client, you can replace the exact implementation with MinHook/PolyHook/custom trampolines, but the conceptual contract is:
  - install named hook
  - keep bookkeeping for uninstall
  - provide safe init/shutdown symmetry

## 5. Major initialization routines

There appear to be at least two major init paths of interest.

### 5.1 `FUN_100c26b0` at `0x100c26b0`
String refs: `initializing mq2 hooks`, `Initializing chat hook`, `Initializing Display Hooks`

This looks like a main MQ2-style bootstrap routine.

Observed behavior:
- Calls `FUN_100ed540()` / `InitOffsets`-style setup.
- Builds `gszINIFilename`.
- Performs strict client version gating against:
  - date: `May 10 2013`
  - time: `23:30:08`
- Builds DirectInput key name tables.
- Builds EQ mappable command tables.
- Initializes color adjective / syntax error / MQ2Data error tables.
- Calls:
  - `InitializeMQ2DataTypes()`
  - `InitializeMQ2Data()`
  - display/chat hook setup
  - spawn and other MQ2 systems

Why this matters:
- This is one of the best starting points for rebuilding compatibility with a custom client.
- The exact ROF2 build check strongly suggests the DLL expects a very specific executable layout.
- If your custom client diverges, the direct offsets and patch addresses will need abstraction.

### 5.2 `FUN_100c2d00` at `0x100c2d00`
String ref: `applying mq2 injects`

This is a larger custom/THJ init path, likely your fork-specific logic.

Observed call chain:
- `InitializeDisplayHook()`
- `InitializeChatHook()`
- `InitializeMQ2Commands()`
- `InitializeMQ2Windows()`
- `InitializeMQ2Pulse()`
- `InitializeMQ2Spawns()`
- `InitializeMapPlugin()`
- `InitializeMQ2ItemDisplay()`
- `InitializeMQ2Labels()`
- `InitializeTHJSkillTimers()`
- `InitializeTHJDiscTimers()`
- `InitializeTHJCam()`
- `InitializeTHJTracking()`
- `AddDetourf(ExecuteCmd)`
- `InitializeTHJWindows()`

Then it conditionally installs additional detours against `baseAddress + offset`, for example:
- `+0x460ef0`
- `+0x2cf4a0`
- `+0x3dc430`
- `+0x10a440`
- `+0x236670`
- `+0x089d70`
- `+0x4c51f0`
- `+0x4cf970`
- `+0x0c3250`

It also directly patches game memory with `VirtualProtect` + `FlushInstructionCache`, including examples at:
- `baseAddress + 0x2843f0`
- `baseAddress + 0x284395`
- `baseAddress + 0x0538ae`
- `baseAddress + 0x2a83ea` (fills 100 bytes with NOPs)
- `baseAddress + 0x1374bb` (fills 6 bytes with NOPs)
- `baseAddress + 0x2fa60f` (copies 5 bytes from `+0x2fa88c`)
- `baseAddress + 0x2fab62` (copies 5 bytes from `+0x2fa88c`)
- `baseAddress + 0x5c59f0` (writes zero)
- many one-byte writes setting value `8` at additional client offsets

It later detours external APIs fetched dynamically:
- `kernel32!GetModuleFileNameA`
- `gdi32!SetDeviceGammaRamp`

Interpretation:
- This is a fork/customization layer beyond stock MQ2 behavior.
- It contains hard-coded client patches and custom THJ modules.
- If your goal is “core functionality for a custom client”, this function is the clearest proof that you should separate:
  - generic MQ2-like systems,
  - UI/data/command systems,
  - server/client-fork specific patches.

## 6. MQ2 / client-protection logic

### `FUN_1002df60` at `0x1002df60`
String refs: `MQ2Main.dll`, `MQ2 Detected`, `Successfully unloaded MQ2`

Behavior:
- Reads and compares bytes from client memory (anti-tamper / state check).
- Enumerates process modules with `K32EnumProcessModules`.
- If a loaded module name equals `MQ2Main.dll`, it calls `FreeLibrary()` on it.
- On success it shows:
  - title: `MQ2 Detected`
  - message: `Successfully unloaded MQ2`
- Then it resets zoning/spawn-related state.

Interpretation:
- This build appears to contain “MQ2 prevention” or conflict-avoidance logic.
- Since this DLL already embeds much MQ2-like functionality, this may be preventing stock MQ2 from double-hooking the client.
- For your custom reimplementation, this is optional policy logic, not core architecture.

## 7. Hook groups by subsystem

### 7.1 Chat hooks
`InitializeChatHook()` at `0x100c6de0`

Installs detours for:
- `CEverQuest__dsp_chat`
- `CEverQuest__DoTellWindow`
- `CEverQuest__UPCNotificationFlush`

Purpose:
- intercept chat output,
- capture tells/notifications,
- likely drive event parsing, logging, triggers, and macro reactions.

Shutdown counterpart:
- `ShutdownChatHook()` removes the same detours.

### 7.2 Display/UI progress hooks
`InitializeDisplayHook()` at `0x100c70c0`

Installs detours for:
- `CDisplay__CleanGameUI`
- `CDisplay__ReloadUI`
- `CDisplay__ZoneMainUI`
- `EQ_LoadingS__SetProgressBar`

Purpose:
- observe UI lifecycle changes,
- hook reload/zone transitions,
- track loading progress / screen transitions.

### 7.3 Command subsystem
`InitializeMQ2Commands()` at `0x100c8000`

Observed behavior:
- Hooks `CEverQuest__InterpretCmd`.
- Walks the EQ command list (`EQADDR_CMDLIST`).
- Mirrors or re-registers client commands through `AddCommand(...)`.
- Special-cases `/` and `/whotarget` handling.
- Later re-adds command handlers via `RemoveCommand(...)` / `AddCommand(...)`.

Important custom commands observed elsewhere in window init:
- `/windows`
- `/notify`
- `/itemnotify`
- `/itemslots`

Interpretation:
- The command layer is a major integration point.
- For a custom client, emulate this as a command dispatcher abstraction instead of binding to raw client tables if possible.

### 7.4 MQ2Data providers
`InitializeMQ2Data()` at `0x100d6700`

This registers top-level MQ2-style data providers (TLOs / roots), including:
- `Spawn`
- `Target`
- `Me`
- `Spell`
- `Switch`
- `Ground`
- `Merchant`
- `Window`
- `Macro`
- `MacroQuest`
- `Math`
- `Zone`
- `Group`
- `String`
- `Int`
- `Bool`
- `Float`
- `Corpse`
- `Cursor`
- `NearestSpawn`
- `Time`
- `GameTime`
- `Ini`
- `Heading`
- `Defined`
- `LastSpawn`
- `FindItem`
- `FindItemBank`
- `InvSlot`
- `SelectedItem`
- `FindItemCount`
- `FindItemBankCount`
- `Skill`
- `AltAbility`
- `Raid`
- `NamingSpawn`
- `SpawnCount`
- `LineOfSight`
- `Plugin`
- `Select`
- `DoorTarget`
- `ItemTarget`
- `DynamicZone`
- `Friends`

This is one of the clearest maps of “core MQ2 functionality” embedded in the DLL.

### 7.5 MQ2 type system
`InitializeMQ2DataTypes()` at `0x100e5a00`

Observed constructed base types:
- `float`
- `int`
- `byte`
- `string`
- `spawn`
- `spell`
- `buff`
- `ticks`
- `character`
- `class`
- `race`
- `ground`
- `switch`
- plus many more via RTTI and constructor helpers

Examples of exposed members seen in decompile:
- `float`: `Centi`, `Milli`, `Precision`
- `int`: `Float`, `Reverse`
- `string`: `Length`, `Upper`, `Lower`, `Compare`, `CompareCS`, `Equal`, `NotEqual`, `Token`, `Replace`
- `buff`: `Level`, `Spell`, `Duration`, `Counters`
- `class`: `ShortName`, `PureCaster`, `CanCast`, `DruidType`, `NecromancerType`, `ShamanType`, `ClericType`, `PetClass`, `HealerType`
- `ground` / `switch`: `Distance`, `Heading`, `HeadingTo`, `LineOfSight`, plus position/default coordinates on switch

For a custom client, this is the conceptual schema you want to preserve even if the internals differ.

### 7.6 Item display subsystem
`InitializeMQ2ItemDisplay()` at `0x100edd00`

Installs detours for:
- `CItemDisplayWnd__SetSpell`
- `CItemDisplayWnd__UpdateStrings`

Purpose:
- inject additional spell/item text,
- customize tooltip/item-display generation.

### 7.7 Label/HUD subsystem
`InitializeMQ2Labels()` at `0x100f1230`

Observed hooks include:
- `CXStr__CXStr3`
- `CLabel__Draw`
- `CSidlManager__CreateLabel`
- `__GetGaugeValueFromEQ`
- `__GetLabelFromEQ`
- `EQ_Character__Cur_HP`
- `EQ_Character__Max_HP`
- `EQ_Character__Cur_Mana`
- `EQ_Character__Max_Mana`
- `EQ_Character__Cur_Endurance`
- `EQ_Character__Max_Endurance`
- `EQ_Character__GetUsableClasses`
- `EQ_Character__CanEquip`
- `EQ_Character__IsSpellcaster` (multiple variants)
- `EQ_Spell__GetSpellLevelNeeded`
- `CSpellBookWnd__CanStartMemming`
- `EQ_GetEQStr`
- `EQ_WhoClassName`
- `EQ_CharSelectClassNameFunc`
- one additional raw `baseAddress + 0x2a9540` hook

Purpose:
- supply custom HUD labels and live value expansion,
- override class/spell/name rendering,
- drive XML label substitutions and gauge updates.

### 7.8 Pulse / event loop subsystem
`InitializeMQ2Pulse()` at `0x100fa850`

Installs detours for:
- `ProcessGameEvents`
- `CEverQuest__EnterZone`
- `CEverQuest__SetGameState`

Other behavior:
- Reads a few bytes from live process memory and caches them.
- Appears to snapshot original code bytes for later validation or patch management.
- Sets `DAT_1017c010 = 2` under one signature condition.

Interpretation:
- This is the main “heartbeat” / per-frame / game-state transition subsystem.
- If you are rebuilding only the essentials, this plus the command/data systems are the real core.

### 7.9 Spawn subsystem
`InitializeMQ2Spawns()` at `0x100fbd10`

Installs detours for:
- `EQPlayer__EQPlayer`
- `EQPlayer__dEQPlayer`
- `EQPlayer__SetNameSpriteState`
- `EQPlayer__SetNameSpriteTint`

Other behavior:
- Clears `EQP_DistArray`.
- Resets `gSpawnCount`.
- Reads/writes caption color settings from `gszINIFilename` under the `Caption Colors` section.
- Persists ON/OFF and color settings back to the INI.

Interpretation:
- This is spawn lifecycle tracking + nameplate/caption customization.
- Good candidate to rebuild independently from the rest.

### 7.10 Window/XML subsystem
`InitializeMQ2Windows()` at `0x10111fb0`

Observed behavior:
- Builds/updates an `ItemSlotMap`.
- Generates additional slot mappings in the ranges:
  - `7000..7079`
  - `8000..8030`
- Installs detours for:
  - `CXMLSOMDocumentBase__XMLRead`
  - `CSidlScreenWnd__Init1`
  - `CXWndManager__RemoveWnd`
- Registers commands:
  - `/windows`
  - `/notify`
  - `/itemnotify`
  - `/itemslots`
- Iterates the live window manager (`ppWndMgr`), extracts XML metadata, and builds an internal window lookup/registry.

Companion helpers identified:
- `SendWndNotification` at `0x10110da0`
- `FindMQ2Window` at `0x10111da0`
- `ShutdownMQ2Windows` at `0x101119d0`

Interpretation:
- This is the basis for MQ2-style UI automation and `/notify` command behavior.
- For a custom client, this subsystem is essential if you want scriptable UI interaction.

## 8. Example: `dataMacroQuest` provider

Function: `FUN_100da0a0` at `0x100da0a0`
Likely corresponds to the `MacroQuest` top-level data handler.

Observed members returned include:
- game state string (`CHARSELECT`, `INGAME`, `UNKNOWN`)
- login name
- server name
- last command
- last tell
- last normal error
- last syntax error
- last MQ2Data error
- current clock value
- mouse state/value

This confirms the DLL is not just detouring functions; it also recreates the MQ2 data model exposed to scripts/macros.

## 9. Shutdown symmetry

The DLL has clean shutdown routines for most major groups:
- `ShutdownChatHook`
- `ShutdownDisplayHook`
- `ShutdownMQ2Commands`
- `ShutdownMQ2Data`
- `ShutdownMQ2DataTypes`
- `ShutdownMQ2Detours`
- `ShutdownMQ2Pulse`
- `ShutdownMQ2Spawns`
- `ShutdownMQ2Windows`

This is useful architecturally: the original design is subsystem-oriented, not a single monolith. That supports a clean reimplementation strategy.

## 10. Reimplementation guidance for a custom client

If your goal is to duplicate the core behavior for a custom EQEmu client, I would separate it into layers:

### Layer A: loader / bootstrap
- dinput8 proxy loader
- real `DirectInput8Create` forwarding
- wrapper objects for DirectInput interfaces

### Layer B: detour engine
- install/remove hook abstraction
- bookkeeping and rollback
- clear init/shutdown lifecycle

### Layer C: client abstraction
Replace raw hard-coded ROF2 addresses with an abstraction table:
- symbols / pattern scans / signatures
- version-specific address maps
- capability flags per client build

### Layer D: core subsystems to preserve first
1. command hook + command registry
2. pulse/game-event hook
3. MQ2Data root providers
4. MQ2Type system
5. spawn tracking
6. window `/notify` subsystem
7. chat/display hooks

### Layer E: optional / fork-specific features
- THJSkillTimers
- THJDiscTimers
- THJCam
- THJTracking
- THJWindows
- raw client patch bundles
- anti-stock-MQ2 unload logic

Recommended order for rebuilding:
1. DirectInput proxy loader
2. detour manager
3. `InterpretCmd` hook + command registry
4. `ProcessGameEvents` / `SetGameState` hooks
5. `MQ2Data` root registry
6. `MQ2Type` member schema
7. spawn/window systems
8. custom THJ extensions
9. raw binary patches only if still necessary

## 11. What looks client-version specific vs portable

### Likely portable concepts
- command interception model
- MQ2Data root registry
- MQ2Type registry
- window notify model
- spawn tracking model
- chat/display lifecycle hooks

### Likely ROF2-build-specific implementation details
- exact `baseAddress + offset` hooks
- direct NOP/byte patches
- the `May 10 2013 23:30:08` client gate
- memory checks against addresses like `0x00531f90` and `0x00519750`
- direct assumptions about internal window/layout structures

## 12. Raw analysis artifacts

Generated during analysis:
- `/home/zutfen/ghidra-work/out/ghidra-survey.txt`
- `/home/zutfen/ghidra-work/out/objdump-p.txt`
- `/home/zutfen/ghidra-work/out/objdump-x.txt`
- `/home/zutfen/ghidra-work/out/strings.txt`

These contain the full decompiler dump and are worth keeping if you continue the port.

## 13. Biggest takeaways

1. This DLL is effectively a full MQ2-like runtime embedded into a dinput8 proxy.
2. The cleanest conceptual boundaries are:
   - detours
   - commands
   - MQ2Data/MQ2Types
   - pulse/game-state
   - spawn tracking
   - window automation
3. The THJ/custom code path adds direct client patching and custom modules beyond generic MQ2 behavior.
4. The safest way to port this to a custom client is to preserve subsystem behavior while replacing hard-coded addresses with a versioned abstraction layer.

## 14. Next good reverse-engineering targets

If you want a second pass, I would focus on these next:
- `SendWndNotification` (`0x10110da0`) to fully document `/notify`
- `FindMQ2Window` (`0x10111da0`) to document UI lookup rules
- `ParseMQ2DataPortion` (`0x100d60f0`) to document MQ2 expression parsing
- `dataSpawn`, `dataTarget`, `dataCharacter` (`Me`), `dataWindow` for the most important TLO semantics
- the DirectInput wrapper function `FUN_100c69c0` to map the full vtable shim
- THJ-specific init functions to separate stock MQ2-ish behavior from your custom additions

## 15. Second-pass deep dive

Second-pass artifacts generated during this run:
- `/home/zutfen/ghidra-work/out/deep-dive.txt`
- `/home/zutfen/ghidra-work/scripts/DeepDiveSurvey.java`

### 15.1 `/notify` command flow

The `/notify` command handler is `FUN_101100a0` at `0x101100a0`.

Observed behavior:
- It tokenizes up to four arguments with quote-aware parsing.
- Argument layout is effectively:
  - arg1: window name
  - arg2: optional child/control name
  - arg3: action/verb
  - arg4: optional payload/index
- Registered from `InitializeMQ2Windows()` via:
  - `AddCommand("/notify", FUN_101100a0, 0, 1, 0)`

Dispatch behavior:
- If arg3 matches one of the mouse-click verbs, it calls `SendWndClick(window, child, verb)`.
- If arg3 is `listselect`, it calls `SendListSelect(window, child, index-1)`.
- If arg3 is `tabselect`, it resolves the window, optionally resolves the child control, verifies the target is a tab control (`CXWnd::GetType() == 0x29`), then calls `CTabWnd::SetPage(index-1, true)`.
- Otherwise it scans a table of approximately `0x1e` notification names and dispatches them through `SendWndNotification()`.

Important implementation detail:
- For generic notification verbs, `/notify` does not synthesize mouse events directly. It converts the verb into a notification index (`uVar11`) and forwards that code to `SendWndNotification(window, child, code, payload)`.
- One verb slot (`uVar11 == 0x1b`) consumes arg4 as a payload pointer instead of a plain integer, which suggests at least one notification is string/value-carrying rather than a simple click/select event.

Practical conclusion:
- `/notify` is a generic UI message injector, not just a click helper.
- `SendWndClick` and `SendListSelect` are convenience branches for common interaction styles, while `SendWndNotification` is the lower-level primitive.

### 15.2 `SendWndNotification()` semantics

Function: `SendWndNotification` at `0x10110da0`

Signature recovered by Ghidra:
```c
uint __cdecl SendWndNotification(uint *windowName, byte *childName, uint code, void *payload)
```

Behavior:
- Resolves the top-level window via `FindMQ2Window(windowName)`.
- If `childName` is non-null and non-empty, resolves the child control relative to the window with `FUN_100c2100(window, childName)`.
- Calls:
  - `EQClasses::CXWnd::WndNotification(window, child, code, payload)`
- Updates `*gpMouseEventTime` with `GetFastTime()` and returns success.

Failure behavior:
- If the window lookup fails, it formats an error string and returns failure.
- If the child lookup fails, it formats an error string and returns failure.

Architectural takeaway:
- The real client-facing automation boundary is `CXWnd::WndNotification`.
- A clean-room reimplementation only needs:
  1. stable window lookup,
  2. child-control lookup by name,
  3. a reproducible notification enum/code map,
  4. optional payload marshalling for notifications that carry values.

### 15.3 `SendWndClick()` and `SendWndClick2()`

`SendWndClick()` at `0x10111b20` is the named-window wrapper used by `/notify`, THJ window automation, and other helpers.

Observed behavior:
- Resolves the target window via `FindMQ2Window()`.
- Optionally resolves a child control unless arg2 is empty or `'0'`.
- Matches arg3 against an 8-entry verb table.
- Computes the target control's screen-rect center.
- Dispatches one of the following UI event sequences:
  - left button down
  - left button down + up
  - left button held
  - left button down + held + up-after-held
  - right button down
  - right button down + up
  - right button held
  - right button down + held + up-after-held
- Updates `*gpMouseEventTime` after success.

`SendWndClick2()` at `0x10111010` performs the same center-of-rect click synthesis when the caller already has a direct `CXWnd *` instead of a window name.

Implication:
- MQ2-style UI automation here is not OS-level mouse injection; it is in-process dispatch of `CXWnd` handlers at the semantic window/control level.

### 15.4 `FindMQ2Window()` lookup rules

Function: `FindMQ2Window` at `0x10111da0`

Observed behavior:
- Lowercases the requested name before lookup.
- Looks up the normalized name in an internal registry/hash table built by `InitializeMQ2Windows()`.
- The registry entry can resolve either:
  - a direct live `CXWnd *` at entry offset `+0x80`, or
  - an alternate pointer path stored at `+0x84`.
- If a cached entry is stale, it clears the registry slot and returns failure.

Special-case aliases:
- `bankN`
  - resolves bank-slot-backed container windows through character data structures
- `packN`
  - resolves pack/backpack container windows through inventory structures
- `enviro`
  - resolves the environment container via `ppContainerMgr + 0x8c`

Known callers:
- `SendWndNotification`
- `SendWndClick`
- `SendListSelect`
- `/combine`
- `/windowstate`
- `dataWindow`
- `/notify`
- THJ helpers

Interpretation:
- The registry is not just a static XML-name map; it is a hybrid of:
  - XML-driven named windows,
  - live container aliases,
  - cached runtime pointers.
- This is why `/notify bank1 ...` or `/notify pack3 ...` style addressing can work without a normal XML window name.

### 15.5 `InitializeMQ2Windows()` data model

The first-pass notes already covered command registration and detours, but the deeper pass makes the registry design clearer.

Additional confirmed behavior:
- Builds `ItemSlotMap` from canonical slot names plus generated numeric ranges.
- Explicitly populates generated ranges:
  - `7000..7079`
  - `8000..8030`
- Walks the live `ppWndMgr` list of windows.
- For each window with valid XML metadata (`GetXMLData()` and a type check of `== 0x31`), it:
  - extracts the XML name via `GetCXStr(...)`
  - normalizes/builds a key
  - allocates an `0x88`-byte registry record if needed
  - stores the live `CXWnd *` at record offset `+0x80`

This confirms that the window subsystem is effectively a name service over the live UI tree.

### 15.6 `ParseMQ2DataPortion()` grammar and evaluation model

Function: `ParseMQ2DataPortion` at `0x100d60f0`

This is the core MQ2 expression walker.

Observed grammar features:
- dot member traversal: `Root.Member.SubMember`
- bracket/index arguments: `Root[args]`
- quote-aware bracket parsing, including comma-separated content inside `[]`
- parenthesized type resolution/cast-style segments: `...(TypeName)...`
- root/TLO lookup when no current type exists
- typed-member dispatch once a current type is established
- fallback lookup for MQ2 data variables and macro-stack variables

Important control flow:
1. It begins with no current type/value in `param_2`.
2. If it encounters `.` and there is no current type, it resolves the token as either:
   - a top-level MQ2Data root (`FindMQ2Data`), or
   - an MQ2 data variable (`FindMQ2DataVariable`).
3. If a current type already exists, it dispatches member lookup through a type virtual at offset `+0x10`.
4. If bracket content is present, it accumulates the bracket expression into a local buffer (`local_808`) and passes it to the type/root resolver.
5. If it encounters `(TypeName)`, it resolves the type via `FindMQ2DataType()` and replaces the current type with that explicit type.
6. If typed-member lookup fails, it falls back through two helper routines (`FUN_10019c10`, `FUN_10019cd0`) before raising `MQ2DataError()`.

Special variable resolution path:
- If a token is not a root and not a normal data variable, it walks macro-stack linked structures at offsets like `+0x808` / `+0x80c`, which strongly suggests local macro variables / stack-frame variables are part of the same expression namespace.

Interpretation:
- This is a true mixed-mode evaluator, not just a flat TLO parser.
- It supports:
  - roots,
  - chained members,
  - indexed lookups,
  - type coercion/introspection,
  - macro locals/variables,
  - array-style resolution.

### 15.7 `dataSpawn()` semantics

Function: `dataSpawn` at `0x100d5d20`

Behavior:
- If the argument is numeric-ish (digits, optional leading `-`, and `.` tolerated by the parser), it converts it with `FUN_10088b58()` and resolves directly through:
  - `EQClasses::EQPlayerManager::GetSpawnByID(*ppSpawnManager, id)`
- Otherwise it builds a search descriptor and calls:
  - `ParseSearchSpawn(arg, searchStruct)`
  - `SearchThroughSpawns(searchStruct, *ppCharSpawn)`
- On success it returns:
  - `param_2[0] = pSpawnType`
  - `param_2[1] = EQPlayer *`

Why this matters:
- `${Spawn[...]}` supports both direct ID addressing and search-expression addressing.
- `Target`, `Character`, and other spawn-adjacent TLOs likely reuse the same type contracts via `pSpawnType` or derived types.

### 15.8 Confirmed TLO root registry

The deeper pass confirms the exact `InitializeMQ2Data()` root registrations, including the ones that mattered most for the next pass:
- `Spawn` -> `dataSpawn`
- `Target` -> `dataTarget`
- `Me` -> `dataCharacter`
- `Window` -> `dataWindow`
- `MacroQuest` -> `dataMacroQuest`
- plus many more (`Spell`, `Switch`, `Ground`, `Merchant`, `Group`, `String`, `Float`, `Corpse`, `Cursor`, `NearestSpawn`, `Heading`, `Plugin`, `DoorTarget`, `ItemTarget`, `DynamicZone`, `Friends`, etc.)

Important correction from the earlier pass:
- the local-player TLO root is `Me`, not `Character`
- the runtime type is still `character` / `pCharacterType`

This confirms that the DLL recreates a large MQ2-compatible root object namespace, not just a handful of convenience variables, and that it broadly follows classic MQ2 naming conventions where the player-facing root name and the underlying type name are not always identical.

### 15.9 Confirmed type relationships for `Me`/`character`, `Target`, and `Window`

From `InitializeMQ2DataTypes()`:
- `pCharacterType = FUN_1001b360(...)`
- `pWindowType = FUN_1001d020(...)`
- `MQ2TargetType` is built explicitly and exposes at least:
  - `Buff`
  - `BuffCount`
  - `BuffDuration`
  - `PctAggro`
  - `SecondaryPctAggro`
  - `SecondaryAggroPlayer`

Confirmed inheritance/association edges:
- `pCharacterType[0x14] = pSpawnType`
- `pTargetType[0x14] = pSpawnType`
- `pRaidMemberType[0x14] = pSpawnType`

Interpretation:
- `Me` is the TLO root, while `character` is the type name returned by `dataCharacter`.
- `character` and `target` are not unrelated value objects; they are spawn-derived types layered on top of the generic spawn contract.
- `Window` is built as its own dedicated type helper and is paired with `dataWindow` + `FindMQ2Window`, which strongly suggests the `Window` TLO is a typed wrapper over the same named-window registry used by `/notify`.

### 15.10 THJ-specific window automation hook

Function: `FUN_1003cd50` at `0x1003cd50`
Caller: `InitializeTHJWindows`

Observed behavior:
- Caches pointers to:
  - `InventoryWindow`
  - child `IW_AltCurrPage`
  - child `AltCurr_CreateItemButton`
- Checks visibility of the alt-currency page via `CXWnd::IsReallyVisible`.
- Uses `SendWndClick("InventoryWindow", "AltCurr_DisplayMissingButton", "leftmouseup")` as the automation primitive.
- Maintains a small state machine in globals like `DAT_10193bf0` and `DAT_1018e613`.
- Marks a byte at `AltCurr_CreateItemButton + 0x138` and calls `FUN_10116310()` during the transition sequence.

Interpretation:
- This is fork-specific scripted UI automation sitting on top of the generic window subsystem.
- It is a good example of how the DLL authors expected custom features to be built:
  1. resolve named windows/controls,
  2. test visibility/state,
  3. synthesize semantic UI interactions with `SendWndClick`,
  4. track progress in globals.

### 15.11 DirectInput wrapper map: `FUN_100c69c0`

The first pass established that `DirectInput8Create` forwards to `FUN_100c69c0`. The deeper pass clarifies what this wrapper does.

Observed behavior:
- It checks the requested interface GUID against multiple hard-coded GUID blobs.
- For matching GUIDs, it wraps the returned COM interface in custom wrapper objects allocated at size `0x0c`.
- Confirmed wrapper vtables include:
  - `m_IDirectInput8A::vftable`
  - `m_IDirectInput8W::vftable`
  - `m_IClassFactory::vftable`
- It also uses internal hash/container helpers to cache and reuse wrappers rather than wrapping the same interface pointer repeatedly.

Interpretation:
- The proxy layer is broader than just `DirectInput8Create` + `CreateDevice`.
- It includes interface-family aware wrapping for ANSI/Wide DirectInput 8 objects and at least one COM class-factory path.
- For a reimplementation, the clean boundary is:
  - identify requested COM interface,
  - wrap with the corresponding shim class,
  - cache wrapper objects by underlying interface pointer,
  - then let device-level hooks take over downstream.

### 15.12 Exact `/notify` verb table

I extracted the verb table directly from `DAT_10135530` with a targeted headless script.
Artifact:
- `/home/zutfen/ghidra-work/out/notify-table.txt`

The handler in `FUN_101100a0` scans `0x1e` entries (indices `0..29`) and uses the matching index as the `WndNotification` code passed to `SendWndNotification(...)`.

Recovered table:

| Code | Verb string | Notes |
|---|---|---|
| 0 | `<null>` | no named verb in table |
| 1 | `leftmouse` | overlaps click verb table |
| 2 | `leftmouseup` | overlaps click verb table |
| 3 | `rightmouse` | overlaps click verb table |
| 4 | `<null>` | no named verb in table |
| 5 | `<null>` | no named verb in table |
| 6 | `enter` | generic notification path |
| 7 | `<null>` | no named verb in table |
| 8 | `<null>` | no named verb in table |
| 9 | `help` | generic notification path |
| 10 | `close` | generic notification path |
| 11 | `<null>` | no named verb in table |
| 12 | `<null>` | no named verb in table |
| 13 | `<null>` | no named verb in table |
| 14 | `newvalue` | generic notification path |
| 15 | `<null>` | no named verb in table |
| 16 | `<null>` | no named verb in table |
| 17 | `<null>` | no named verb in table |
| 18 | `<null>` | no named verb in table |
| 19 | `<null>` | no named verb in table |
| 20 | `contextmenu` | generic notification path |
| 21 | `mouseover` | generic notification path |
| 22 | `history` | generic notification path |
| 23 | `leftmousehold` | generic notification path |
| 24 | `<null>` | no named verb in table |
| 25 | `<null>` | no named verb in table |
| 26 | `<null>` | no named verb in table |
| 27 | `link` | special payload handling in `/notify` |
| 28 | `<null>` | no named verb in table |
| 29 | `resetdefaultposition` | generic notification path |

Important precedence note:
- `FUN_101100a0` first tries the click-helper path through `SendWndClick(...)` when there is no fourth argument payload and the verb matches the separate 8-entry click table.
- Extracted click verbs from `PTR_s_leftmouse_10135a00` are:
  - `leftmouse`
  - `leftmouseup`
  - `leftmouseheld`
  - `leftmouseheldup`
  - `rightmouse`
  - `rightmouseup`
  - `rightmouseheld`
  - `rightmouseheldup`
- Because of that precedence, `/notify <wnd> <child> leftmouse` and `/notify <wnd> <child> leftmouseup` normally go through click synthesis, not through generic `WndNotification(code=1/2)` dispatch.
- The overlap still matters architecturally because it reveals the client's underlying notification-number convention, even when the normal `/notify` fast path prefers semantic click injection.

Special handling for `link`:
- `link` is code `27` (`0x1b`), which matches the special branch already seen in the handler.
- When the loop hits code `0x1b`, `/notify` passes arg4 as a raw payload pointer/string buffer instead of the normal numeric-style payload.
- There is also a pre-check that compares the verb against the `link` string before the `SendWndClick(...)` fallback; if matched, it forces a non-null payload sentinel even when arg4 is absent.
- This is the strongest evidence in the handler that not all notifications are simple button-style events; some expect structured or string-ish payloads.

What is now known vs. still unresolved:
- Known exactly:
  - the textual verb names present in the table
  - their numeric indices/codes
  - which entries are unnamed/null
  - the overlap with the click-helper verb table
  - the special payload behavior for code `27` / `link`
- Still unresolved in this pass:
  - the semantic meaning of the null-coded notification slots
  - whether some null slots correspond to valid `CXWnd` notification numbers that simply were not given public `/notify` names
  - the exact UI-side handling semantics for each code inside specific window classes

### 15.13 `Window` TLO and `MQ2WindowType`

Artifacts from this pass:
- `/home/zutfen/ghidra-work/out/window-tlo-deep-dive.txt`
- `/home/zutfen/ghidra-work/out/window-string-addrs.txt`
- `/home/zutfen/ghidra-work/scripts/WindowTloDeepDive.java`
- `/home/zutfen/ghidra-work/scripts/DumpStringAddrs.java`

#### `dataWindow()` root resolver

Function: `dataWindow` at `0x100d5920`

Recovered behavior is very small and direct:
- If the input string is empty, it returns failure.
- Otherwise it calls `FindMQ2Window(name)`.
- On success it writes:
  - `param_2[0] = pWindowType`
  - `param_2[1] = CXWnd *`
- Then it returns success.

This confirms the `Window` TLO is just a typed wrapper over the same named-window registry used by `/notify`, `/windowstate`, `/combine`, and the THJ automation helpers.

Practical interpretation:
- `${Window[InventoryWindow]}` is effectively `FindMQ2Window("InventoryWindow")` plus `MQ2WindowType` projection.
- The TLO does not appear to do any extra search logic beyond what `FindMQ2Window()` already implements.
- Because `FindMQ2Window()` supports aliases like `packN`, `bankN`, and `enviro`, the `Window` TLO likely inherits those alias semantics too.

#### `MQ2WindowType` constructor: `FUN_1001d020`

Function: `FUN_1001d020` at `0x1001d020`

This function initializes `pWindowType` during `InitializeMQ2DataTypes()`.
It registers the type name `window`, assigns `MQ2WindowType::vftable`, and installs the following members:

| Member ID | Name |
|---|---|
| 1 | `Open` |
| 2 | `Child` |
| 3 | `VScrollMax` |
| 4 | `VScrollPos` |
| 5 | `VScrollPct` |
| 6 | `HScrollMax` |
| 7 | `HScrollPos` |
| 8 | `HScrollPct` |
| 9 | `Children` |
| 10 | `Siblings` |
| 12 | `FirstChild` |
| 13 | `Next` |
| 14 | `Minimized` |
| 15 | `X` |
| 16 | `Y` |
| 17 | `Height` |
| 18 | `Width` |
| 19 | `MouseOver` |
| 20 | `BGColor` |
| 21 | `Text` |
| 22 | `Tooltip` |
| 23 | `List` |
| 24 | `Checked` |
| 25 | `Style` |
| 26 | `Enabled` |
| 27 | `Highlighted` |
| 28 | `Name` |
| 29 | `ScreenID` |
| 30 | `Type` |
| 31 | `Items` |
| 32 | `HisTradeReady` |
| 33 | `MyTradeReady` |

Important structural observations:
- `Open`, `Minimized`, `Checked`, `Enabled`, `Highlighted`, `MouseOver`, `HisTradeReady`, and `MyTradeReady` strongly suggest mixed boolean/state accessors.
- `Child`, `FirstChild`, `Next`, `Name`, `ScreenID`, and `Type` indicate tree navigation and metadata access.
- `Children` and `Siblings` imply count-style accessors rather than pointer-returning traversal nodes.
- `VScroll*` and `HScroll*` show the type exposes direct scrollbar state, which is useful for scripting list and text controls without synthetic mouse drags.
- `Text`, `Tooltip`, and `List` show the type is not limited to geometric properties; it also exposes control content.
- `Items`, `HisTradeReady`, and `MyTradeReady` suggest the same generic `window` type is used across specialized windows like trade or list/container UIs rather than having separate narrow wrapper types for every screen.

#### `FUN_1001d2a0`: likely a type-side string-to-window resolver helper

Function: `FUN_1001d2a0` at `0x1001d2a0`

Recovered logic:
- Calls `FindMQ2Window(param_2)`
- Stores the result into `*param_1`
- Returns `true` if non-null, else `false`

Interpretation:
- This is almost certainly a helper used by `MQ2WindowType` for coercion/conversion from a string/index into a `CXWnd *` payload.
- Even without fully reconstructing the vtable layout, its placement and behavior strongly suggest it is one of the type-level parse/resolve entry points.

#### Relationship to `FindMQ2Window()`

`Window` is more tightly coupled to `FindMQ2Window()` than `Target`/`Character` are to spawn search.
The model is basically:
1. Resolve a window name/alias to a live `CXWnd *`
2. Tag the result as `pWindowType`
3. Expose state and navigation through `MQ2WindowType` members

That means the `Window` TLO is not an independent subsystem. It is the typed query facade over the UI name-service/cache built by `InitializeMQ2Windows()`.

#### Clean-room reimplementation implications

To reproduce `Window` TLO behavior, the minimum viable pieces are:
1. a stable `FindWindowByNameOrAlias()` equivalent
2. a `WindowType` object whose payload is a live UI node pointer/handle
3. member accessors for:
   - hierarchy traversal (`Child`, `FirstChild`, `Next`)
   - geometry (`X`, `Y`, `Height`, `Width`)
   - visibility/state (`Open`, `Minimized`, `Enabled`, `Highlighted`, `MouseOver`, `Checked`)
   - text/content (`Text`, `Tooltip`, `List`)
   - scroll state (`VScroll*`, `HScroll*`)
4. screen-specific convenience accessors where generic windows expose structured state (`Items`, trade-ready flags)

The most important architectural point is that `Window` is a thin projection over live UI state, not a detached snapshot object.

#### Exact `MQ2WindowType::GetMember` dispatcher

A later headless pass recovered the actual vtable slot and member dispatcher:
- `MQ2WindowType::vftable = 0x1013f7a8`
- `vtable + 0x10 -> FUN_100db090`

Artifact:
- `/home/zutfen/ghidra-work/out/type-vtables2.txt`

Recovered core semantics of `FUN_100db090`:
- It resolves the requested member through `FUN_10019c10(this, memberName)`.
- If the member is unknown, it returns failure immediately.
- It switches on the resolved window member ID and projects directly from the live `CXWnd *` payload.
- Window-navigation members return `pWindowType` on success, but degrade to integer `0` when the pointed child/sibling window is null.
- String-producing members copy live `CXStr`/XML-backed text into `DataTypeTemp` before returning `pStringType`.

Confirmed member behaviors from the dispatcher:
- `Open` -> boolean from `CXWnd + 0x196`
- `Child[arg]` -> `FUN_100c2100(window, arg)` lookup, returned as `pWindowType`
- `VScrollMax` -> `CXWnd + 0x190`
- `VScrollPos` -> `CXWnd + 0x17c`
- `VScrollPct` -> `(VScrollPos * 100) / VScrollMax`
- `HScrollMax` -> `CXWnd + 0x1a0`
- `HScrollPos` -> `CXWnd + 0x198`
- `HScrollPct` -> `(HScrollPos * 100) / HScrollMax`
- `Children` -> boolean from `CXWnd + 0x10`
- `Siblings` -> boolean from `CXWnd + 0x08`
- `Minimized` -> boolean from `CXWnd + 0x1ce`
- `X` / `Y`-style geometry fields are projected from the window rectangle members at `+0x60/+0x64/+0x68/+0x6c`
- `MouseOver` -> boolean from `CXWnd + 0x20`
- `BGColor` -> ARGB value from `CXWnd + 0x128`, returned as `pArgbType`
- `Tooltip` -> copied from the tooltip `CXStr` at `CXWnd + 0xe8`
- `Checked` -> boolean from `CXWnd + 0x1e4`
- `Enabled` -> boolean test on the enable/state field at `param_1[0x1a]`
- `Highlighted` -> boolean from `CXWnd + 0x1e5`
- `Items` -> for list/combo-style widgets, returns backing item count from the embedded list object
- `HisTradeReady` / `MyTradeReady` -> booleans sourced from `*ppTradeWnd + 0x2f8` / `+0x2f9`

Two implementation details matter for a clean-room rebuild:
1. `Window` members are overwhelmingly direct field projections over a live UI node; there is very little abstraction between the TLO and `CXWnd` internals.
2. Some members are widget-sensitive rather than universally valid. `Child`, `Tooltip`, and `Items` all branch through UI helper logic instead of using a single fixed field.

### 15.14 `Target` TLO and `MQ2TargetType`

Artifacts from this pass:
- `/home/zutfen/ghidra-work/out/target-tlo-deep-dive.txt`
- `/home/zutfen/ghidra-work/out/target-string-addrs.txt`
- `/home/zutfen/ghidra-work/scripts/TargetTloDeepDive.java`
- `/home/zutfen/ghidra-work/scripts/DumpTargetStringAddrs.java`

#### `dataTarget()` root resolver

Function: `dataTarget` at `0x100d5ab0`

Recovered behavior:
- It ignores its input parameter entirely.
- It checks `*ppTarget`.
- If a current target exists, it writes:
  - `param_2[0] = pTargetType`
  - `param_2[1] = *ppTarget`
- Then it returns success.
- If `*ppTarget == 0`, it returns failure.

Interpretation:
- `${Target}` is a pure live-state projection of the current target pointer, not a search expression resolver.
- This differs sharply from `${Spawn[...]}`, which parses either an ID or a search query.
- So `${Target}` is conceptually closer to `${Me}`-style live singleton roots than to `${Spawn[...]}`.

#### Exact `MQ2TargetType` member set

The `MQ2TargetType` construction recovered from `InitializeMQ2DataTypes()` is now exact:

| Member ID | Name |
|---|---|
| 1 | `Buff` |
| 2 | `BuffCount` |
| 3 | `BuffDuration` |
| 4 | `PctAggro` |
| 5 | `SecondaryPctAggro` |
| 6 | `SecondaryAggroPlayer` |

This resolves the previously unknown `DAT_1013d974` string: it is `Buff`.

Structural interpretation:
- `Buff`, `BuffCount`, and `BuffDuration` make `Target` a combat-state-aware overlay on top of a base spawn.
- `PctAggro`, `SecondaryPctAggro`, and `SecondaryAggroPlayer` show that the target type layers hate/aggro inspection on top of the generic spawn model.
- The small size of the member table strongly suggests `Target` is intentionally narrow because most other useful properties are inherited from `pSpawnType`.

#### Inheritance edge: `Target` derives from `Spawn`

`InitializeMQ2DataTypes()` sets:
- `pTargetType[0x14] = pSpawnType`

That confirms `Target` is not a standalone object model. It extends the spawn contract with a few target-specific combat members.

Practical meaning:
- `${Target.Name}`, `${Target.ID}`, `${Target.Type}`, etc. almost certainly come from inherited spawn behavior.
- `${Target.Buff[...]}` and `${Target.PctAggro}` are the value-add members provided specifically by `MQ2TargetType`.

#### Contrast with `dataSpawn()`

This pass makes the distinction between `Target` and `Spawn` very clear:

`dataTarget()`:
- no parsing
- no search
- no lookup by ID
- just returns `*ppTarget`

`dataSpawn()`:
- numeric input -> `GetSpawnByID`
- non-numeric input -> `ParseSearchSpawn` + `SearchThroughSpawns`

So the hierarchy is effectively:
- `Spawn` = query/search root returning `pSpawnType`
- `Target` = current-target singleton returning `pTargetType`
- `TargetType` = `SpawnType` + target-combat extensions

#### Clean-room reimplementation implications

To reproduce `Target` cleanly, you do not need a separate search subsystem beyond what `Spawn` already needs.
You need:
1. a live `currentTarget` pointer/handle source
2. a `TargetType` that inherits all `SpawnType` accessors
3. six target-specific members:
   - `Buff`
   - `BuffCount`
   - `BuffDuration`
   - `PctAggro`
   - `SecondaryPctAggro`
   - `SecondaryAggroPlayer`

The key implementation insight is that `Target` is a view over one global pointer, while `Spawn` is a selection/query engine.

#### Exact `MQ2TargetType::GetMember` dispatcher

A later vtable pass recovered the target-side dispatcher exactly:
- `MQ2TargetType::vftable = 0x1013f40c`
- `vtable + 0x10 -> FUN_100d72b0`

Artifact:
- `/home/zutfen/ghidra-work/out/type-vtables2.txt`

Recovered core semantics of `FUN_100d72b0`:
- If the live target pointer is null, it returns failure.
- It resolves the requested member through `FUN_10019c10(this, memberName)`.
- If the member is not one of the six target-local members, it falls through to `pSpawnType`'s own `GetMember` against the live target spawn.
- This is the exact mechanism behind the earlier structural conclusion that `TargetType = SpawnType + small combat overlay`.

Confirmed target-member behaviors:
- `Buff[arg]`
  - with no index/name argument: returns the first active target buff as `pSpellType`
  - with a numeric argument: returns the Nth non-empty buff slot as `pSpellType`
  - with a string argument: scans active target buffs by spell name and returns the matching `pSpellType`
- `BuffCount`
  - counts non-zero entries across the target-buff slot array at `*ppTargetWnd + 0x53c`
  - returns `pIntType`
- `BuffDuration[arg]`
  - resolves the same buff-selection modes as `Buff`
  - converts the paired duration value from the array at `*ppTargetWnd + 0x6c0`
  - returns `pTicksType`
- `PctAggro`
  - returns the primary aggro percentage byte from `*ppAggroInfo + 0x4`
  - returned as `pIntType`
- `SecondaryPctAggro`
  - returns the secondary aggro percentage byte from `*ppAggroInfo + 0xc`
  - returned as `pIntType`
- `SecondaryAggroPlayer`
  - reads the spawn ID at `*ppAggroInfo + 0xe0`
  - resolves it through `EQPlayerManager::GetSpawnByID(*ppSpawnManager, id)`
  - returns the resulting spawn as `pSpawnType`

This means the target-specific logic is extremely compact but semantically rich:
- buffs are modeled as a sparse target-window slot table plus a parallel duration table
- aggro data is modeled through one separate global aggro-info structure
- everything else is inherited from generic spawn handling

### 15.15 `Me` TLO, `MQ2CharacterType`, and embedded `XTarget`

Artifacts from this pass:
- `/home/zutfen/ghidra-work/out/character-xtarget-deep-dive.txt`
- `/home/zutfen/ghidra-work/out/character-members.txt`
- `/home/zutfen/ghidra-work/scripts/CharacterXTargetDeepDive.java`
- `/home/zutfen/ghidra-work/scripts/ExtractCharacterMembers.java`

#### `dataCharacter()` is really the `Me` root resolver

Function: `dataCharacter` at `0x100d5a80`

Recovered behavior:
- It ignores its input parameter.
- It checks `*ppCharData`.
- If present, it writes:
  - `param_2[0] = pCharacterType`
  - `param_2[1] = *ppCharData`
- Then it returns success.
- Otherwise it returns failure.

Exact root-name confirmation from string recovery:
- `DAT_1013d270` = `Me`

This is the important correction to the earlier model:
- the TLO root exposed by `InitializeMQ2Data()` is `Me`
- the runtime type returned by that root is `character`

So the real relationship is:
- `${Me}` -> `dataCharacter()` -> `pCharacterType`
- not `${Character}` -> `dataCharacter()`

Architecturally this puts `Me` in the same family as `Target`: a singleton live-state root, not a search/query root like `Spawn`.

#### `MQ2CharacterType` constructor: `FUN_1001b360`

Function: `FUN_1001b360` at `0x1001b360`

Recovered high-level structure:
- Registers type name `character`
- Assigns `MQ2CharacterType::vftable`
- Installs a very large member table
- `InitializeMQ2DataTypes()` later sets `pCharacterType[0x14] = pSpawnType`

This confirms `character` is a large spawn-derived overlay for local-player state, not a small wrapper.

Exact member-table size recovered from the constructor helper:
- highest recovered member ID: `187`
- several IDs are intentionally sparse/skipped (for example `9` and `41`)

Artifact with the exact full table:
- `/home/zutfen/ghidra-work/out/character-members.txt`

Important semantic clusters from the exact table:

1. Identity / root links
- `1 ID`
- `2 Name`
- `3 Level`
- `4 Exp`
- `5 Spawn`

`Spawn` is especially important: it strongly suggests `${Me.Spawn}` exposes or reprojects the underlying spawn-side view for the local player.

2. Vitals / resources
- `10 CurrentHPs`
- `11 MaxHPs`
- `12 HPRegen`
- `13 PctHPs`
- `14 CurrentMana`
- `15 MaxMana`
- `16 ManaRegen`
- `17 PctMana`
- `38 Endurance`
- `91 MaxEndurance`
- `92 PctEndurance`
- `122 CurrentEndurance`
- `123 EnduranceRegen`

3. Buff/song/book overlays
- `18 Buff`
- `19 Song`
- `20 Book`

This shows `character` is not just a stat bag. It also exposes spellbook/buff-context projections.

4. Inventory / currency / containers
- `39 Inventory`
- `40 Bank`
- `43 FreeInventory`
- `44 Gem`
- `98 LargestFreeInventory`
- `78 Platinum`
- `79 Gold`
- `80 Silver`
- `81 Copper`
- `82-85` banked coin variants

5. Group / raid / leadership projections
- `99 TargetOfTarget`
- `100 RaidAssistTarget`
- `101 GroupAssistTarget`
- `102 RaidMarkNPC`
- `103 GroupMarkNPC`
- `120 GroupList`
- `121 AmIGroupLeader`
- many `LA*` leadership-AA style fields from `139` onward

6. Modern or late-era progression / utility fields
- `133 RadiantCrystals`
- `134 EbonCrystals`
- `156 Doubloons`
- `157 Orux`
- `158 Phosphenes`
- `159 Phosphites`
- `175 Faycites`
- `176 Chronobines`
- `177 Mercenary`
- `180 MercenaryStance`
- `181 SkillCap`
- `182 GemTimer`
- `183 HaveExpansion`

7. Aggro / target-state crossover near the tail
- `178 XTarget`
- `184 PctAggro`
- `185 SecondaryPctAggro`
- `186 SecondaryAggroPlayer`
- `187 AggroLock`

This is a major architectural clue: a lot of “target-ish” and aggro-ish state is exposed directly off `${Me}` / `character`, not only off `${Target}`.

#### Exact `MQ2XTargetType` member set

`InitializeMQ2DataTypes()` builds `MQ2XTargetType` inline under type name `xtarget`.
Resolved member names are:

| Member ID | Name |
|---|---|
| 1 | `Type` |
| 2 | `ID` |
| 3 | `Name` |
| 4 | `PctAggro` |

Key observations:
- `XTarget` is a very small type compared with `character`.
- It looks like a narrow descriptor for one extended-target slot rather than a full spawn wrapper.
- The presence of `Type` first strongly suggests slot classification matters as much as the pointed entity.

#### Clean-room reimplementation implications

To model this subsystem faithfully, the minimal design is:
1. `Me` as a singleton root over live character data (`ppCharData`-style state)
2. `CharacterType` as a very large local-player façade that inherits spawn behavior
3. embedded projections for:
   - buffs/songs/book
   - inventory/bank/gems
   - group/raid leadership targeting
   - currencies and expansion-era progression state
   - aggro and extended-target views
4. `XTargetType` as a small slot descriptor object with `Type`, `ID`, `Name`, and `PctAggro`

The most important conceptual correction from this pass is that `Character` is a type name, while `Me` is the actual player-facing TLO root.

#### Exact `MQ2CharacterType::GetMember` dispatcher

The same vtable pass recovered the local-player dispatcher entry:
- `MQ2CharacterType::vftable = 0x1013f770`
- `vtable + 0x10 -> FUN_100df000`

Artifact:
- `/home/zutfen/ghidra-work/out/type-vtables2.txt`

Recovered core semantics of `FUN_100df000`:
- It is a very large switch dispatcher over the `character` member table recovered from `FUN_1001b360`.
- It first resolves the requested member through `FUN_10019c10(this, memberName)`.
- If the member is not one of the `character`-local members, it delegates directly to `pSpawnType::GetMember` using the embedded spawn pointer at `param_1 + 0x2dd0`.
- This is the exact inheritance mechanism behind `pCharacterType[0x14] = pSpawnType`.

Confirmed early/member-local behaviors visible in the recovered dispatcher:
- `ID` -> integer projected from the embedded spawn object (`*(spawn + 0x148)`)
- `Name` -> local-player name string at `param_1 + 0x3228`
- `Level` -> integer projected from the player/char-data chain (`*ppCharData ... + 0x3388`)
- `Exp` -> integer from `param_1 + 0x2910`
- `Spawn` -> returns the embedded spawn pointer itself as `pSpawnType`

The larger structural takeaway is more important than any one case:
- `character` is not implemented as a copy of spawn logic; it is a local overlay that only handles player-specific members itself.
- Unhandled members are inherited dynamically by tail-calling the base `SpawnType` accessor on the embedded spawn pointer.
- That architecture explains why the constructor can expose a huge local-player surface while still reusing the generic spawn contract for name, loc, class/race-style identity, and other shared spawn semantics.

For a clean-room rebuild, this is the correct mental model:
1. `Me` resolves to live character state.
2. `CharacterType` projects local-player-only fields directly from char-data/global UI structures.
3. Anything not recognized locally is forwarded to `SpawnType` against the embedded player spawn.

### 15.16 `AddMQ2Data` / `AddMQ2Type` registry mechanics

Artifacts supporting this pass:
- `/home/zutfen/ghidra-work/out/deep-dive.txt`

#### `AddMQ2Data()` at `0x100d6b70`

Recovered behavior:
- First calls `FindMQ2Data(name)` and refuses duplicates.
- Uses a critical section at `DAT_101932a8`.
- Maintains a growable slot array at `DAT_101932a4` with current capacity in `DAT_101932a0`.
- Searches for the first free slot; if full, grows capacity by `+10` entries.
- Allocates a `0x44`-byte record for each root.
- Copies the root name string into the record base.
- Stores the provider callback at record offset `+0x40`.
- Inserts the record pointer into `DAT_101932a4[slot]`.
- Also inserts a hash/index entry through `FUN_10012210(&DAT_10193298, name)` and stores `slot + 1`.

Important structural insight:
- the root registry is not just a linked list or raw array
- it is a dual structure:
  1. a dense/growable slot array of root records
  2. a hash-style name index that stores a 1-based slot number

That explains why parser-side lookups often do:
1. hash the token name
2. fetch an integer index
3. turn that into `DAT_101932a4[index-1]`
4. then call the provider callback from `record + 0x40`

#### `AddMQ2Type()` at `0x100d6f70`

Recovered behavior is the type-side mirror of `AddMQ2Data()`:
- It derives the type name from `param_1 + 4`.
- It checks duplicates with `FindMQ2DataType(name)`.
- Uses a separate critical section at `DAT_101932c8`.
- Maintains a growable slot array at `DAT_101932c4` with capacity in `DAT_101932c0`.
- Finds the first free slot, growing by `+10` when needed.
- Stores the type object pointer itself in `DAT_101932c4[slot]`.
- Inserts a hash/index entry through `FUN_10012210(&DAT_10193290, typeName)` and stores `slot + 1`.

This means the type system has the same two-level structure as the root system:
- hash lookup by lowercase-ish name
- then 1-based slot indirection
- then an object pointer with vtable/member metadata

#### Why this matters for the expression engine

This registry design ties together several previously separate observations:
- `ParseMQ2DataPortion()` uses `FindMQ2Data` / `FindMQ2DataType` and also the lower-level hash helpers directly.
- Root resolution and type resolution both depend on stable name-to-slot maps, not linear scans.
- The DLL's MQ2 layer is therefore a proper reflective runtime with:
  - named roots
  - named types
  - per-type member dispatch
  - sparse but stable registry slots

For a clean-room rebuild, reproducing this exact memory layout is not necessary, but reproducing the contract is:
1. unique registration by name
2. fast lookup from token -> root/type object
3. stable object records holding callbacks or member-dispatch metadata
4. thread-safe mutation during startup/shutdown
