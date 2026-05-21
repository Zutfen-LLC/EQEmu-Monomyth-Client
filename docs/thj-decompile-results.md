# THJ dinput8.dll Ghidra findings

Target binary: `/home/zutfen/Desktop/thj/dinput8.dll`

Status: updated with additional tracing

## Executive summary

The strongest result is that THJ does not appear to directly write the final UI controls for these class strings from inside this DLL. Instead, it centrally detours the EverQuest class-name producer functions and substitutes its own multiclass formatters.

The two key hook mappings are now recoverable:

- `/who` class-name path
  - target/original pointer global: `EQ_WhoClassName` at `0x10186328`
  - wrapper veneer: `WhoClassName` at RVA `0x000babe0`
  - THJ replacement function: `FUN_10025510` at RVA `0x00025510`
  - extra helper/metadata pushed into detour setup: `FUN_100e6d20` at RVA `0x000e6d20`

- char-select / inventory-adjacent class-name path
  - target/original pointer global: `EQ_CharSelectClassNameFunc` at `0x101866fc`
  - wrapper veneer: `CharSelectClassNameFunc` at RVA `0x000babd0`
  - THJ replacement function: `FUN_10024920` at RVA `0x00024920`
  - extra helper/metadata pushed into detour setup: `FUN_100e6d00` at RVA `0x000e6d00`

This means the best current deliverable is:
- inventory / char-select class text is intercepted through `EQ_CharSelectClassNameFunc`
- `/who` row class text is intercepted through `EQ_WhoClassName`
- THJ's contribution is primarily to return rewritten class strings; the final control write likely still occurs in the original EQ client code after these functions return

## Requested strings: results

Exact ASCII search results from Ghidra:
- `IDW_ClassTitle1`: not found
- `ClassValueLabel`: not found
- `Players in EverQuest`: not found
- `CC_Class_`: not found

This suggests those identifiers are either:
- not present in this DLL,
- resource / XML / external UI assets rather than plain ASCII in `dinput8.dll`, or
- represented differently than expected.

Relevant strings that were found:
- `/who` at `0x1012e6c4`
- `Class` at `0x1012e9d0`
- `Classes` at `0x101323dc`

The `/who` literal is only referenced in `InitializeMQ2Commands`, so it is not the visible row-writer itself.

## Central hook registration

A centralized registration function exists at `InitializeMQ2Labels`:
- Function RVA: `0x000e9bf0`

Relevant recovered registration sequence:

- `0x100ead6d`: `PUSH 0x100e6d20`
- `0x100ead72`: `PUSH 0x10025510`
- `0x100ead77`: `MOV ECX,dword ptr [0x10186328]`
- `0x100ead7d`: `PUSH ECX`
- `0x100ead7e`: `CALL 0x100e6230`

- `0x100ead86`: `PUSH 0x100e6d00`
- `0x100ead8b`: `PUSH 0x10024920`
- `0x100ead90`: `MOV EDX,dword ptr [0x101866fc]`
- `0x100ead96`: `PUSH EDX`
- `0x100ead97`: `CALL 0x100e6230`

Because `AddDetourf` is cdecl and the target is pushed last, the effective detour tuples are:

1. `/who`
- target/original: `[*0x10186328]` (`EQ_WhoClassName`)
- replacement: `0x10025510`
- helper/meta: `0x100e6d20`

2. char-select / inventory-adjacent
- target/original: `[*0x101866fc]` (`EQ_CharSelectClassNameFunc`)
- replacement: `0x10024920`
- helper/meta: `0x100e6d00`

## Hook engine / patch style

Recovered detour helpers:
- `AddDetourf` at RVA `0x000e6230`
- `AddDetour` at RVA `0x000e35e0`
- `RemoveDetour` at RVA `0x000e6160`

`AddDetourf` extracts three caller-provided values and calls:
- `AddDetour((uint *)param_1, local_8, local_4, 0x14)`

`AddDetour`:
- stores detour metadata in an internal linked structure
- snapshots original bytes
- applies the patch via `FUN_10001680(param_3,(char *)param_1,param_2)`

Conclusion:
- THJ is using centralized inline detour / trampoline patching, not merely swapping callsites in its own code.

## Wrapper/export veneers

Recovered wrapper functions:
- `CharSelectClassNameFunc` at RVA `0x000babd0`
- `WhoClassName` at RVA `0x000babe0`
- `GetClassDesc` at RVA `0x000baa10`
- `SetItemText` at RVA `0x000ba420`

These wrappers are thin dispatch veneers:
- `CharSelectClassNameFunc` calls through `*EQ_CharSelectClassNameFunc`
- `WhoClassName` calls through `*EQ_WhoClassName`
- `GetClassDesc` calls through `*CEverQuest__GetClassDesc`
- `SetItemText` calls through `*CListWnd__SetItemText`

Important implication:
- THJ does not appear to directly call `SetItemText` from its own class formatting logic in this DLL.
- The final UI write is likely performed by original EQ client code after the hooked class-name function returns a rewritten string.

## `/who` visible row / class text path

Best current match:
- function: `FUN_10025510`
- RVA: `0x00025510`
- installed as replacement for `EQ_WhoClassName`

Behavior recovered from decomp:
- takes a class bitmask `param_1`
- appends slash-joined class codes into static buffer `DAT_10188bb7 / DAT_10188bb8`
- trims trailing `/`
- returns either the built buffer or fallback via `FUN_100e6d20()` if empty

Decoded class code table from data constants:
- `0x101368f4`: `WAR/`
- `0x101368fc`: `CLR/`
- `0x10136904`: `PAL/`
- `0x1013690c`: `RNG/`
- `0x10136914`: `SHD/`
- `0x1013691c`: `DRU/`
- `0x10136924`: `MNK/`
- `0x1013692c`: `BRD/`
- `0x10136934`: `ROG/`
- `0x1013693c`: `SHM/`
- `0x10136944`: `NEC/`
- `0x1013694c`: `WIZ/`
- `0x10136954`: `MAG/`
- `0x1013695c`: `ENC/`
- `0x10136964`: `BST/`
- `0x1013696c`: `BER/`

Conclusion:
- `FUN_10025510` is the multiclass formatter entrypoint for the `/who` path.
- It does not itself write a control; it returns the row class text that the original client UI code likely consumes.

This is the best current answer to:
- “/who row class text is written in function A, using helper B or builder C”

Current best phrasing:
- `/who` row class text is produced by THJ replacement `FUN_10025510` (RVA `0x00025510`) on the `EQ_WhoClassName` hook path; the final visible row write likely happens later in EQ client UI code outside this DLL.

## Char-select / inventory-adjacent class text path

Best current match:
- function: `FUN_10024920`
- RVA: `0x00024920`
- installed as replacement for `EQ_CharSelectClassNameFunc`

This function is more nuanced than first suspected.

### Branch 1: special caller-dependent short-code path

The function begins with a callsite discriminator:
- `if ((unaff_retaddr + 0x400000) - baseAddress == 0x6843ff)`

In that branch it:
- builds slash-joined abbreviated class codes into `DAT_101873b7 / DAT_101873b8`
- uses the same `WAR/CLR/PAL/.../BER/` code table as the `/who` formatter
- trims trailing `/`
- returns either the built string or fallback `FUN_100e6d00()` if empty

This is gold because it gives an exact original-client callsite discriminator:
- original EQ-side callsite / return-site test: `0x6843ff` relative to client base

Interpretation:
- one specific original caller into `EQ_CharSelectClassNameFunc` gets the abbreviated slash-joined multiclass form
- this is a strong candidate for the exact char-select visible text path THJ wanted to special-case

### Branch 2: game-state fallback

If not the special caller and `gGameState == 5`:
- returns `FUN_100e6d00()` fallback directly

### Branch 3: full-name multiclass builder

Otherwise it:
- reads a class bitmask from `*(uint *)(*(int *)(*ppSpawnManager + 8) + 0x518)`
- builds slash-joined full class names into `DAT_101883b7 / DAT_101883b8`
- trims trailing `/`
- returns the built string or fallback `FUN_100e6d00()` if empty

Recovered full-name components include:
- `Warrior`
- `Cleric`
- `Paladin`
- `Ranger`
- `Shadow Knight`
- `Druid`
- `Monk`
- `Bard`
- `Rogue`
- `Shaman`
- `Necromancer`
- `Wizard`
- `Magician`
- `Enchanter`
- `Beastlord`
- `Berserker`

Conclusion:
- `FUN_10024920` is not just a simple char-select function; it is a context-sensitive class text producer.
- It can return either abbreviated multiclass codes or full slash-joined class names depending on original caller context and game state.
- This is the strongest current lead for the inventory class title / class-label behavior, but again it appears to be a producer, not the final UI writer.

## Inventory class title: best current answer

I still do not have a direct `IDW_ClassTitle1` or `ClassValueLabel` xref inside this DLL.

However, the best evidence-based answer now is:
- THJ likely does not write the inventory class title control directly from `dinput8.dll`.
- Instead, it intercepts the class-name provider `EQ_CharSelectClassNameFunc` and replaces it with `FUN_10024920`.
- `FUN_10024920` contains a context-sensitive multiclass formatter that can produce:
  - abbreviated `WAR/CLR/...` style strings for one specific original EQ caller (`0x6843ff` callsite discriminator), and
  - full slash-joined class names from the current player/spawn state in its general path.

So the most defensible current deliverable is:
- inventory / class-label text is produced via `EQ_CharSelectClassNameFunc` replacement `FUN_10024920`, while the ultimate text write likely remains in original EQ UI code.

## Text-write helpers

What I asked Ghidra to trace and what it showed:
- `SetItemText` at RVA `0x000ba420` is only a thin wrapper to `CListWnd__SetItemText`
- I did not recover any meaningful internal THJ callers to `SetItemText` from this class-formatting path
- `GetClassDesc` at RVA `0x000baa10` does have internal callers, but they are unrelated command / parser / gameplay helper paths, not an obvious final UI text writer chain

Relevant `GetClassDesc` callers seen inside this DLL:
- `FUN_100f7d60` via call at `0x100f7e00`
- `ParseSearchSpawnArgs` via call at `0x10105fb2`
- `DoAbility` via call at `0x100c5514`
- `Where` via call at `0x100c5baf`
- `FUN_100ebd30` via call at `0x100ebf92`
- `FUN_100e6940` via call at `0x100e69e0`
- `FUN_100e01d0` via call at `0x100e0270`

Current conclusion:
- the direct text-write helper chain the user asked for is not yet visible inside this DLL because the interesting logic here is at the string-production hook layer, not at the final control-setter layer.

## Related globals and symbols

Recovered globals of interest:
- `EQ_WhoClassName` at `0x10186328`
- `EQ_CharSelectClassNameFunc` at `0x101866fc`
- `CEverQuest__GetClassDesc` at `0x10186378`
- `CEverQuest__GetClassThreeLetterCode` at `0x10186528`

Recovered fallback strings:
- `0x101367e8`: `Unknown Class`
- `0x101367f8`: `DoodooHead`

## Best current deliverables in the user’s requested style

1. Inventory class title
- THJ does not appear to directly set the inventory class title control from this DLL.
- The class-title producing path is most likely `EQ_CharSelectClassNameFunc` -> THJ replacement `FUN_10024920` (RVA `0x00024920`).
- `FUN_10024920` is a multiclass formatter entrypoint that returns either abbreviated slash-joined codes or full slash-joined class names depending on call context.
- One exact original-client callsite discriminator inside that hook is `0x6843ff` relative to client base.

2. `/who` row class text
- `/who` row class text is produced via `EQ_WhoClassName` -> THJ replacement `FUN_10025510` (RVA `0x00025510`).
- `FUN_10025510` is the multiclass formatter entrypoint for slash-joined abbreviated class codes like `WAR/CLR/PAL/...`.
- The final visible row write likely still occurs in original EQ client UI code after this function returns the text.

3. Exact callsite RVAs they patched
- centralized registration site: `InitializeMQ2Labels` at RVA `0x000e9bf0`
- `/who` detour install call: around `0x100ead72`..`0x100ead7e`
- char-select / inventory-adjacent detour install call: around `0x100ead8b`..`0x100ead97`
- original-client callsite discriminator inside the char-select replacement: `0x6843ff` relative to client base

4. Patch style
- inline detour / trampoline via `AddDetourf` -> `AddDetour`

## Remaining unresolved items

- exact original EQ UI function that performs the final inventory class title control write
- exact original EQ UI function that performs the final `/who` row text control write
- exact role of helper stubs `FUN_100e6d20` and `FUN_100e6d00`
- whether the missing UI identifiers live in external EQ resources / XML rather than this DLL

## Current bottom line

If I had to give the tightest actionable answer right now:

- inventory/class label path: `EQ_CharSelectClassNameFunc` is detoured to `FUN_10024920` (`RVA 0x00024920`), which is a context-sensitive multiclass formatter returning either short codes or full slash-joined class names
- `/who` row class path: `EQ_WhoClassName` is detoured to `FUN_10025510` (`RVA 0x00025510`), which returns slash-joined abbreviated multiclass codes
- both hooks are centrally installed in `InitializeMQ2Labels` (`RVA 0x000e9bf0`) using inline detours
- the final visible text write is probably still in original EQ client code, not in THJ’s `dinput8.dll`
