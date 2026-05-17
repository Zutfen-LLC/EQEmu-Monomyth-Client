# EQGame Spell/UI Ghidra Notes

These notes capture cleanroom Ghidra findings for the fingerprinted ROF2
`eqgame.exe` build used to validate spell/UI locator candidates.

Fingerprint:

- SHA-256: `b259cd6b9291777e265d7d9d39312b101c393285468faa0ce86dff695181fddf`
- Size: `8774656` bytes

Pinned candidates:

- `EQ_Spell::GetSpellLevelNeeded`
  - VA: `0x004af700`
  - RVA: `0x000af700`
  - Size: `37` bytes
  - Shape: `thiscall`, `ECX=this`, one stack arg `class_id`, returns byte in `AL`, `ret 4`
  - Exact bytes:
    - `8b 44 24 04 8d 50 ff 83 fa 22 77 10 83 f8 24 72 01 cc`
    - `8a 84 01 46 02 00 00 c2 04 00 8a 81 47 02 00 00 c2 04 00`
- `CSpellBookWnd::CanStartMemming`
  - VA: `0x0075bd40`
  - RVA: `0x0035bd40`
  - Shape: `thiscall`, one stack arg, returns bool in `AL`, `ret 4`
  - Entry bytes:
    - `83 3d ac 35 e6 00 00 53 56 8b f1 b3 01 0f 8f 8f 01 00 00`
    - `8b 0d 7c fc d1 00 6a 01 e8 d0 51 f0 ff 84`
  - Caller evidence: calls `GetSpellLevelNeeded` at `0x0075bea5`

Related caller anchors:

- `StartSpellScribe`-like path at VA `0x0075ddf0` / RVA `0x0035ddf0`
- Spellbook dispatcher at VA `0x0075e790` / RVA `0x0035e790`

Unpinned targets:

- `CInvSlot::HandleRButtonUp`
- `EQ_Character::GetUsableClasses`
- `EQ_Character::CanEquip`

These remained intentionally unpinned because the available strings/xrefs did not
identify safe hook entry points.
