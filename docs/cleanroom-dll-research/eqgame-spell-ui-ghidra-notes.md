# EQGame Spell/UI Ghidra Notes

These notes capture cleanroom Ghidra findings for the fingerprinted ROF2
`eqgame.exe` build used to validate spell/UI locator candidates.

Fingerprint:

- SHA-256: `2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e`
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
- `MemorizeSendPacketWrapper`
  - VA: `0x008c51f0`
  - RVA: `0x004c41f0`
  - Shape: `thiscall`, `ECX=this`, three stack args `(mode_like, packet_ptr, total_length)`
  - Entry bytes:
    - `55 8b ec 6a ff 68 d8 91 9a 00 64 a1 00 00 00 00`
    - `50 51 53 56 57 a1 80 87 b6 00 33 c5 50 8d 45 f4`
    - `64 a3 00 00 00 00 8b f1 8d be 54 02 00 00 8b cf`
  - Caller anchor: memorize path at `0x0075e6db` stores opcode `0x217c` / `OP_MemorizeSpell` into `0x00dd00e0`, writes a 16-byte payload beside it, then calls this wrapper with total length `0x12`
- `CInvSlot::HandleRButtonUp`
  - VA: `0x00697250`
  - RVA: `0x00297250`
  - Shape: conservative `thiscall` assumption, `ECX=this`, one pushed pointer arg
  - Entry-byte prefix:
    - `6a ff 64 a1 00 00 00 00 68 81 a8 98 00 50 b8 08 21 00 00`
    - `64 89 25 00 00 00 00 e8 21 4d 24 00 53 55 33 db 56 8b f1`
    - `33 ed 89 5c 24 24 38 5e 10 0f 84 a8 08 00 00 57 83 cf ff`
    - `8b c7 89 44 24 1c 66 89 44 24 20`
  - Caller anchor: `CInvSlotWnd` vtable slot 19 handler at `0x00699e10` calls this target at `0x00699e61`
- `EQ_Character::IsClassUsablePredicate`
  - VA: `0x004a1f50`
  - RVA: `0x000a1f50`
  - Shape: conservative bool-like predicate, `ECX=this`, one stack arg `class_id`, returns via `ret 4`
  - Exact bytes:
    - `8b c1 8b 4c 24 04 8d 51 ff 83 fa 0f 77 15 8b 40 68 ba 01 00 00 00`
    - `d3 e2 23 c2 f7 d8 1b c0 f7 d8 c2 04 00 32 c0 c2 04 00`

Related caller anchors:

- `StartSpellScribe`-like path at VA `0x0075ddf0` / RVA `0x0035ddf0`
- Spellbook dispatcher at VA `0x0075e790` / RVA `0x0035e790`

Intentionally unpinned targets:

- `EQ_Character::CanEquip`
- Raw `GetUsableClasses` mask getter

These remain intentionally unpinned because the current cleanroom evidence only
supports trace-safe pinning of the right-click target and the class-usability
predicate, not a broader item-usability or raw mask-getter hook.
