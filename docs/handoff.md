# Handoff: Secondary-Class Spell Scribe / Memorize Investigation

## Scope

Continue the clean-room investigation into why secondary-class PAL spell scribing and memorization are not yet working in the ROF2 client hook DLL.

Repo:
- `/home/zutfen/code/EQEmu-Monomyth-Client`

Runtime log under test:
- `/home/zutfen/everquest_rof2/monomyth-client.log`

Related local references:
- MQ2 source: `/home/zutfen/Desktop/MQ2Emu_ROF2_Legacy_Source/`
- Ghidra notes: `/home/zutfen/monomyth_ghidra/out/right_click_chain_update_verified_new_exe.md`
- Repo spell UI notes: `docs/cleanroom-dll-research/eqgame-spell-ui-ghidra-notes.md`

Current branch:
- `codex/client-memorize-send-trace-001`

## What Is Already Proven

### 1. Server-side class-mask support is not the blocker

Prior audit already established the EQEmu server supports multiclass spell eligibility via class-mask-aware level checks. This session stayed on the client hook side.

### 2. We have a known-good normal memorize packet baseline

Using full packet trace on a normal MAG spell memorize, the real network opcode was confirmed as:
- `OP_MemorizeSpell` = `0x217c`

That run proved the client can send and receive the real memorize opcode in this executable and that the packet observer path is functioning.

### 3. A stale deployed DLL caused at least one false read

One log review initially looked like the new tracing was absent because the deployed `/home/zutfen/everquest_rof2/dinput8.dll` had not been updated. The correct startup receipts for the new build are:
- `full_packet_trace_dev_opt_in=true`
- `full_packet_trace_allowed=true`
- `PacketObserver state=initialized recv_log_policy=all_packets send_log_policy=all_packets`

If those lines are missing, the live EQ directory is still running an older DLL.

### 4. The important UX correction: scribing is not right-click in this client

Critical clarification from the user:
- This `eqgame.exe` does **not** use inventory right-click to scribe.
- The active UX is clicking the spell scroll into an empty spell slot in the spell book.

This invalidated the older assumption that `CInvSlot::HandleRButtonUp` was the relevant live path for the reproduce.

### 5. Spellbook click-to-scribe is a distinct path from spellbook memorize

For a PAL spell scroll click into an empty spellbook slot, the log showed a packet sequence like:
- send `OP_DeleteSpell` (`0x3358`)
- recv `OP_DeleteSpell`
- send `OP_MemorizeSpell` (`0x217c`)
- recv `OP_MemorizeSpell`

Around that scribe attempt there were **no**:
- `CanStartMemming`
- `StartSpellMemorizationPath`

So spellbook click-to-scribe is not just the same path as ordinary spellbook memorize.

## What We Misread And Corrected

### Earlier false conclusion

A prior run appeared to show `OP_MemorizeSpell` sends and was briefly interpreted as progress on secondary-class PAL memorization.

### Correction

The user clarified that run was only:
- a MAG character memorizing a MAG spell
- done specifically to identify the live opcode path

So that run is only a known-good baseline. It does **not** prove secondary-class PAL memorization works.

## Instrumentation Added In This Session

### Existing in-flight tracing already in the repo/worktree

These slices were already added during the current investigation and are still present in the dirty worktree:
- `StartSpellMemorizationPath` trace
- `MemSpellCommitPath` trace
- outbound `OP_MemorizeSpell` send observation
- dev-gated full packet trace mode

Relevant env flags:
- `MONOMYTH_ENABLE_PACKET_HOOKS=1`
- `MONOMYTH_ENABLE_FULL_PACKET_TRACE=1`
- `MONOMYTH_ENABLE_SPELL_USABILITY_DISCOVERY=1`
- `MONOMYTH_ENABLE_SPELL_USABILITY_TRACE=1`
- `MONOMYTH_ENABLE_MEMORIZE_SEND_TRACE=1`
- `MONOMYTH_ENABLE_MULTICLASS_SPELL_USABILITY=1`
- `MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE=1`

Note:
- `MONOMYTH_ENABLE_SCROLL_SCRIBE_TRACE=1` is now known to be irrelevant to the active scribe reproduce because the client behavior is spellbook click-to-scribe, not right-click.

### Newest slice added at the end of this session

To anchor on the proven spellbook scribe packet sequence, `src/hook_manager.cpp` now correlates:
- `OP_DeleteSpell` start
- bounded intermediate wrapper sends
- `OP_MemorizeSpell` follow-up

New log markers to grep:
- `SpellUsabilityTrace target=SpellbookScribeSend status=delete_send_start`
- `SpellUsabilityTrace target=SpellbookScribeSend status=intermediate_send`
- `SpellUsabilityTrace target=SpellbookScribeSend status=memorize_send_followup`
- `SpellUsabilityTrace target=SpellbookScribeSend status=not_observed`

The correlation logs:
- caller return RVA
- caller bytes
- packet prefix
- resolved relative-call targets

This is the current best seam for identifying the real click-to-scribe caller path without relying on the invalid right-click assumption.

## Current Worktree State

Modified files:
- `src/hook_manager.cpp`
- `src/packet_observer.cpp`
- `src/runtime_capabilities.cpp`
- `src/runtime_capabilities.h`
- `src/spell_usability_discovery.cpp`
- `src/spell_usability_discovery.h`
- `README.md`
- `CHANGELOG.md`

Build status:
- `cmake --build build-cross-i686` succeeded after the latest changes

Built DLL:
- `build-cross-i686/dinput8.dll`

Deployed runtime target:
- `/home/zutfen/everquest_rof2/dinput8.dll`

## Immediate Next Step

1. Deploy the freshly built DLL:
   - copy `build-cross-i686/dinput8.dll` to `/home/zutfen/everquest_rof2/dinput8.dll`
2. Run one bounded reproduce:
   - login
   - click a PAL spell scroll into an empty spellbook slot
   - optionally perform one separate PAL spell memorize attempt if needed
   - logout
3. Re-check `/home/zutfen/everquest_rof2/monomyth-client.log` for the new `SpellbookScribeSend` markers.
4. Use the logged caller RVA / bytes / resolved calls to identify the real spellbook click-to-scribe call path in Ghidra and/or the local clean-room notes.

## What The Next Session Should Answer

Primary question:
- What function actually emits the spellbook click-to-scribe `OP_DeleteSpell -> OP_MemorizeSpell` sequence for this client?

Secondary question:
- Once that caller path is identified, where is the PAL secondary-class eligibility being rejected before or around that packet sequence?

## Constraints To Preserve

- Stay clean-room. Do not import THJ or classless DLL gameplay code.
- Keep instrumentation trace-only until the real spellbook scribe seam is proven.
- Do not broaden into `CastSpell`, profile spoofing, or spell-record mutation without direct runtime evidence.
- Treat missing startup capability receipts as a deployment problem first, not a logic conclusion.

## Suggested Skills For The Next Session

- `handoff`
  - to refresh this document if the investigation continues

No other special skill is required unless the next session becomes PR/publish-oriented.
