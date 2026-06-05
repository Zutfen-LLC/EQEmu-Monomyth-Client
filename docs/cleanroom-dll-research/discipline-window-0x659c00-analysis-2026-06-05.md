# Disciplines window: clean-room analysis of VA `0x659c00` / RVA `0x259c00`

Binary analyzed: `/home/zutfen/everquest_rof2/eqgame.exe`
Ghidra project: `/home/zutfen/ghidra_projects/eqgame_proj`

## Verdict

Direct disassembly proof: `0x659c00` is **not** a bool-returning gate and does **not** contain a return-false path.
It is a small `thiscall` helper that iterates the 8 `CAW_Button%d` child controls at `[this+0x230]..[this+0x24c]` and pushes packed resource/state values into each present child via `0x863c00`.

The caller at `0x65a070` (`RVA 0x25a070`) returns false only on its two early exits before `0x659c00` is called:
- `CEverQuest` global at `VA 0x00dd261c` / `RVA 0x009d261c` is null
- helper `VA 0x582350` / `RVA 0x182350` returns `AL=0`

Once execution reaches `call 0x659c00`, `BL` has already been set to `1`, and `0x659c00` preserves `EBX`. So `0x65a070` returns true regardless of anything `0x659c00` does.

That means the narrowest binary-backed next failure seam is not a return from `0x659c00`; it is the first fail-closed branch in the adjacent rebuild worker at `VA 0x65a0a0` / `RVA 0x25a0a0`, especially the spell-manager lookup leg at `VA 0x65a143` / `RVA 0x25a143`.

## 1) Function summary: `VA 0x659c00` / `RVA 0x259c00`

### Shape
Direct proof from Ghidra decompilation and raw disassembly:
- calling style: `__thiscall`/`__fastcall`-like member helper taking `this` in `ECX`
- size: `VA 0x659c00..0x659c71`
- caller xref: only `VA 0x65a08d` / `RVA 0x25a08d` inside `0x65a070`
- body: fixed loop over 8 child pointers

Recovered pseudocode shape:

```c
void helper(this) {
  int res_id = 0x165;
  int *slot_ptr = (int *)(this + 0x230);
  for (int i = 0; i < 8; i++, res_id++, slot_ptr++) {
    int a = -1;
    int b = -1;
    if (*slot_ptr != 0) {
      if (*(int *)((char *)slot_ptr + ((char *)&DAT_00e15c48 - (char *)this)) > 0) {
        a = pack4(*(uint32_t *)(DAT_00e639b0 + res_id*4));
        b = pack4(*(uint32_t *)(DAT_00e639b0 + res_id*4));
      }
      apply_to_child(*slot_ptr, a, b);   // 0x863c00
    }
  }
}
```

### Likely role
Direct proof:
- constructor at `VA 0x659d40` / `RVA 0x259d40` binds:
  - `[this+0x224]` -> `CAW_CombatEffectButton`
  - `[this+0x228]` -> `CAW_CombatEffectLabel`
  - `[this+0x22c]` -> `CAW_OpenCombatSkillSelectWnd`
  - `[this+0x230]..[this+0x24c]` -> `CAW_Button1` .. `CAW_Button8`
  - `[this+0x250]` -> `BlueIconBackground`
  - `[this+0x254]` -> `A_SpellIcons`
- `0x659c00` touches only `[this+0x230]..[this+0x24c]`
- `0x659c00` does not query the 300-entry combat ability array directly and does not touch the spell manager

Inference:
- this is a per-show/per-refresh helper that initializes or refreshes the 8 visible combat ability button children with packed icon/state data derived from the `DAT_00e639b0` table and a paired per-slot integer table at `DAT_00e15c48`
- it looks like button skin/icon-state setup, not the main ability-list rebuild

## 2) Exact return-false / failure exits

### Inside `0x659c00`
Direct proof: none.

`0x659c00` has no boolean return value and no `xor eax,eax` / `mov al,0` / fail-return epilogue. It is a `void` helper ending at:

```asm
VA 0x659c6c / RVA 0x259c6c: 5f                pop edi
VA 0x659c6d / RVA 0x259c6d: 5e                pop esi
VA 0x659c6e / RVA 0x259c6e: 5d                pop ebp
VA 0x659c6f / RVA 0x259c6f: 5b                pop ebx
VA 0x659c70 / RVA 0x259c70: 59                pop ecx
VA 0x659c71 / RVA 0x259c71: c3                ret
```

### Inside caller `0x65a070`
Direct proof: exactly two false exits, both before `0x659c00`.

```asm
VA 0x65a074 / RVA 0x25a074: 8b 0d 1c 26 dd 00    mov ecx,[0xdd261c]
VA 0x65a07c / RVA 0x25a07c: 85 c9                test ecx,ecx
VA 0x65a07e / RVA 0x25a07e: 74 12                je  0x65a092   ; return 0

VA 0x65a080 / RVA 0x25a080: e8 cb 82 f2 ff       call 0x582350
VA 0x65a085 / RVA 0x25a085: 84 c0                test al,al
VA 0x65a087 / RVA 0x25a087: 74 09                je  0x65a092   ; return 0

VA 0x65a089 / RVA 0x25a089: 8b ce                mov ecx,esi
VA 0x65a08b / RVA 0x25a08b: b3 01                mov bl,0x1
VA 0x65a08d / RVA 0x25a08d: e8 6e fb ff ff       call 0x659c00
VA 0x65a093 / RVA 0x25a093: 8a c3                mov al,bl      ; returns 1
```

Important direct proof:
- `BL=1` is set before the call to `0x659c00`
- `0x659c00` saves/restores `EBX`
- therefore `AL` is still `1` at `0x65a093`

So any observed false return attributed to `0x659c00` is inconsistent with this static path.

## 3) Key structure offsets read/written

Direct proof from `0x659ce0`, `0x659d40`, `0x659c00`, `0x659f20`, and `0x65a0a0`:

- `[this+0x224]` = `CAW_CombatEffectButton`
- `[this+0x228]` = `CAW_CombatEffectLabel`
- `[this+0x22c]` = `CAW_OpenCombatSkillSelectWnd`
- `[this+0x230]..[this+0x24c]` = `CAW_Button1` .. `CAW_Button8`
- `[this+0x250]` = `BlueIconBackground`
- `[this+0x254]` = `A_SpellIcons`

`0x659c00` reads:
- child control ptrs: `[this+0x230 + i*4]`
- paired slot-int table: `DAT_00e15c48` indexed by the same slot offset math
- packed 4-byte table at global `VA 0x00e639b0` / `RVA 0x00a639b0`

`0x659c00` writes:
- no direct stores into the owner object
- indirect child updates via `VA 0x863c00` / `RVA 0x463c00`

Confirmed live spell-manager global nearby:
- `VA 0x00e646b0` / `RVA 0x00a646b0`
- used in `0x65a0a0`, not `0x659c00`

## 4) Loops over combat ability slots or spell/skill records

### `0x659c00`
Direct proof:
- loop count is hard-coded `8`
- iterates only the 8 button child ptrs
- does not iterate the 300 combat ability array
- does not walk spell records

Core loop disassembly:

```asm
VA 0x659c11 / RVA 0x259c11: 8d b1 30 02 00 00    lea esi,[ecx+0x230]
VA 0x659c1b / RVA 0x259c1b: bd 08 00 00 00       mov ebp,0x8
...
VA 0x659c25 / RVA 0x259c25: 83 3e 00             cmp dword ptr [esi],0
VA 0x659c28 / RVA 0x259c28: 74 3b                je  0x659c65
VA 0x659c2a / RVA 0x259c2a: 83 3c 32 00          cmp dword ptr [edx+esi],0
VA 0x659c2e / RVA 0x259c2e: 7e 28                jle 0x659c58
...
VA 0x659c65 / RVA 0x259c65: 43                   inc ebx
VA 0x659c66 / RVA 0x259c66: 83 c6 04             add esi,0x4
VA 0x659c69 / RVA 0x259c69: 4d                   dec ebp
VA 0x659c6a / RVA 0x259c6a: 75 b4                jne 0x659c20
```

### Nearby rebuild worker `0x65a0a0`
Direct proof:
- this is the function that does per-button combat-ability -> spell-record work
- it also loops 8 times over `[this+0x230]..[this+0x24c]`
- it uses `0x65a12a` to fetch a combat ability entry from the 300-slot array
- it then uses spell-manager global `0x00e646b0`

## 5) Calls into window/control creation, list rebuild, or spell manager lookups

### Not in `0x659c00`
Direct proof:
- no control creation calls
- no spell-manager calls
- no `GetCombatAbility`/array fetches

Its only calls are:
- `VA 0x559580` / `RVA 0x159580`: tiny index helper `return base + index*4`
- `VA 0x88b400` / `RVA 0x48b400`: packs 4 bytes into a flag word
- `VA 0x863c00` / `RVA 0x463c00`: child-control state/icon update helper

### Nearby relevant functions
Direct proof:
- control creation/binding: `VA 0x659d40` / `RVA 0x259d40`
- combat effect/header refresh: `VA 0x659f20` / `RVA 0x259f20`
- per-button rebuild worker: `VA 0x65a0a0` / `RVA 0x25a0a0`

Note: the repo handoff's known hot rebuild callsite `VA 0x65a12a` / `RVA 0x25a12a` is inside `0x65a0a0` and is the first true combat-ability fetch in this local cluster.

## 6) Most likely failing branch/helper in the WIZ/WAR/PAL case

### Best narrow seam
Direct proof:
- once `0x65a12a` returns a positive combat ability id, the next helper is a spell-manager vfunc call through global `0x00e646b0`
- if that lookup returns null, execution jumps directly to the per-slot cleanup path at `0x65a1e3`

Disassembly:

```asm
; load per-slot value and fetch CombatAbilities[index]
VA 0x65a113 / RVA 0x25a113: 8b 04 3b             mov eax,[ebx+edi]
VA 0x65a116 / RVA 0x25a116: 3b c5                cmp eax,ebp
VA 0x65a118 / RVA 0x25a118: 0f 8e c5 00 00 00    jle 0x65a1e3
VA 0x65a11e / RVA 0x25a11e: 8b 0d 1c 26 dd 00    mov ecx,[0xdd261c]
VA 0x65a124 / RVA 0x25a124: 05 7c ff ff ff       add eax,-0x84
VA 0x65a129 / RVA 0x25a129: 50                   push eax
VA 0x65a12a / RVA 0x25a12a: e8 c1 a3 16 00       call 0x7c44f0
VA 0x65a12f / RVA 0x25a12f: 3b c5                cmp eax,ebp
VA 0x65a131 / RVA 0x25a131: 0f 8e ac 00 00 00    jle 0x65a1e3

; spell manager lookup through confirmed global 0xe646b0
VA 0x65a137 / RVA 0x25a137: 8b 0d b0 46 e6 00    mov ecx,[0xe646b0]
VA 0x65a13d / RVA 0x25a13d: 8b 11                mov edx,[ecx]
VA 0x65a13f / RVA 0x25a13f: 50                   push eax
VA 0x65a140 / RVA 0x25a140: 8b 42 0c             mov eax,[edx+0xc]
VA 0x65a143 / RVA 0x25a143: ff d0                call eax
VA 0x65a145 / RVA 0x25a145: 8b f0                mov esi,eax
VA 0x65a147 / RVA 0x25a147: 3b f5                cmp esi,ebp
VA 0x65a149 / RVA 0x25a149: 0f 84 94 00 00 00    je  0x65a1e3
```

Why this is the best seam:
- your runtime already proved the earlier gate at `0x582350` passes
- the show-side caller `0x65a070` does not itself fail after entering `0x659c00`
- `0x65a12a` is already known hot
- `0x65a143` is the earliest post-`GetCombatAbility` helper that can still fail closed on a single exact branch (`je 0x65a1e3`)
- that branch is narrower than tracing generic window-show code

Inference:
- in the WIZ/WAR/PAL case, the best single explanation for “have combat ability ids, still no usable button rebuild/path” is that at least one resolved combat ability id does not map cleanly through the spell-manager lookup at `0x65a143`, causing the slot path to short-circuit before icon/text/state setup

## 7) Suggested next runtime trace seam(s)

### Primary recommended seam
1. `VA 0x65a143` / `RVA 0x25a143`
   - exact site: indirect spell-manager vfunc call result checked at `0x65a149`
   - trace fields to log:
     - owner `this`
     - slot ordinal `1..8` (derive from current `[this+0x230 + i*4]` base)
     - paired table value read at `DAT_00e15c48` for that slot
     - ability-array index argument passed to `0x7c44f0`
     - returned combat ability id from `0x7c44f0`
     - spell-manager return ptr in `EAX/ESI`
     - whether branch takes `JE 0x65a1e3`
   - exact bytes around seam:

```asm
VA 0x65a13f / RVA 0x25a13f: 50                   push eax
VA 0x65a140 / RVA 0x25a140: 8b 42 0c             mov eax,[edx+0xc]
VA 0x65a143 / RVA 0x25a143: ff d0                call eax
VA 0x65a145 / RVA 0x25a145: 8b f0                mov esi,eax
VA 0x65a147 / RVA 0x25a147: 3b f5                cmp esi,ebp
VA 0x65a149 / RVA 0x25a149: 0f 84 94 00 00 00    je  0x65a1e3
```

### Secondary seam if you want the argument one step earlier
2. `VA 0x65a12a` / `RVA 0x25a12a`
   - exact site: hot rebuild `CombatAbilities[index]` fetch
   - use if you want to correlate the raw per-slot source value before the spell-manager call
   - exact bytes:

```asm
VA 0x65a124 / RVA 0x25a124: 05 7c ff ff ff       add eax,-0x84
VA 0x65a129 / RVA 0x25a129: 50                   push eax
VA 0x65a12a / RVA 0x25a12a: e8 c1 a3 16 00       call 0x7c44f0
VA 0x65a12f / RVA 0x25a12f: 3b c5                cmp eax,ebp
VA 0x65a131 / RVA 0x25a131: 0f 8e ac 00 00 00    jle 0x65a1e3
```

### Tertiary seam only if you insist on following `0x659c00`
3. `VA 0x863c00` / `RVA 0x463c00`
   - reason: this is the only stateful helper `0x659c00` actually calls
   - caveat: it is not a boolean gate and is less likely than `0x65a143` to explain the observed fail-closed behavior

## 8) Bottom line

Direct proof:
- `0x659c00` is a void 8-button child-update helper
- it has no return-false path
- `0x65a070` can only return false before `0x659c00` is called
- the confirmed spell-manager global in this live binary is `VA 0x00e646b0` / `RVA 0x00a646b0`

Best next trace seam:
- `VA 0x65a143` / `RVA 0x25a143`
- with optional paired argument trace at `VA 0x65a12a` / `RVA 0x25a12a`

That is the narrowest branch/helper cluster in this neighborhood that can still explain a WIZ/WAR/PAL failure after the earlier show gate already passed.
