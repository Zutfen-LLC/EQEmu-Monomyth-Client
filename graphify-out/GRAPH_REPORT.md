# Graph Report - .  (2026-05-15)

## Corpus Check
- 33 files · ~493,665 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 156 nodes · 205 edges · 20 communities detected
- Extraction: 85% EXTRACTED · 15% INFERRED · 0% AMBIGUOUS · INFERRED: 30 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_MQ2 DataType Registry|MQ2 Data/Type Registry]]
- [[_COMMUNITY_Hook Lifecycle Management|Hook Lifecycle Management]]
- [[_COMMUNITY_DLL Build Configuration|DLL Build Configuration]]
- [[_COMMUNITY_Design Rationale and Capabilities|Design Rationale and Capabilities]]
- [[_COMMUNITY_DirectInput Export Functions|DirectInput Export Functions]]
- [[_COMMUNITY_Logging Infrastructure|Logging Infrastructure]]
- [[_COMMUNITY_Character Type Hierarchy|Character Type Hierarchy]]
- [[_COMMUNITY_DLL Proxy Passthrough|DLL Proxy Passthrough]]
- [[_COMMUNITY_DLL Entry Points|DLL Entry Points]]
- [[_COMMUNITY_Client Fingerprinting|Client Fingerprinting]]
- [[_COMMUNITY_DirectInput Hook COM Wrappers|DirectInput Hook COM Wrappers]]
- [[_COMMUNITY_CICD Build Pipeline|CI/CD Build Pipeline]]
- [[_COMMUNITY_Logger Header|Logger Header]]
- [[_COMMUNITY_Fingerprint Header|Fingerprint Header]]
- [[_COMMUNITY_DInput Proxy Header|DInput Proxy Header]]
- [[_COMMUNITY_Hook Manager Header|Hook Manager Header]]
- [[_COMMUNITY_Packet Observer Header|Packet Observer Header]]
- [[_COMMUNITY_Log Thread Safety|Log Thread Safety]]
- [[_COMMUNITY_Config Header|Config Header]]
- [[_COMMUNITY_Runtime Capabilities Header|Runtime Capabilities Header]]

## God Nodes (most connected - your core abstractions)
1. `dinput8 Shared Library Target` - 12 edges
2. `One-Time Initialization Callback` - 9 edges
3. `monomyth::logger::Log` - 8 edges
4. `dataSpawn` - 7 edges
5. `InitializeMQ2Data` - 7 edges
6. `Evaluate()` - 6 edges
7. `DLL Proxy Passthrough Pattern` - 6 edges
8. `Monomyth Client Bootstrap` - 6 edges
9. `MQ2CharacterType` - 6 edges
10. `CharacterType GetMember Dispatcher (FUN_100df000)` - 6 edges

## Surprising Connections (you probably didn't know these)
- `dinput8 Shared Library Target` --calls--> `dinput8.def Module Definition`  [EXTRACTED]
  CMakeLists.txt → dinput8.def
- `version Library Dependency` --conceptually_related_to--> `ROF2 Fingerprint Guard`  [INFERRED]
  CMakeLists.txt → README.md
- `monomyth::hooks::Initialize` --semantically_similar_to--> `monomyth::packet_observer::Initialize`  [INFERRED] [semantically similar]
  src/hook_manager.h → src/packet_observer.h
- `GitHub Actions CI Workflow` --references--> `GitHub Actions CI for 32-bit Windows`  [EXTRACTED]
  README.md → CHANGELOG.md
- `CharacterType Constructor (FUN_1001b360)` --implements--> `MQ2CharacterType`  [EXTRACTED]
  docs/cleanroom-dll-research/character-xtarget-deep-dive.txt → docs/cleanroom-dll-research/character-members.txt

## Hyperedges (group relationships)
- **All dinput8.dll Exported Function Proxies** — dllmain_DirectInput8Create, dllmain_DllCanUnloadNow, dllmain_DllGetClassObject, dllmain_DllRegisterServer, dllmain_DllUnregisterServer, dllmain_GetdfDIJoystick [EXTRACTED 1.00]
- **One-Time Bootstrap Initialization Pipeline** — proxy_InitializeOnce, fingerprint_Evaluate, runtime_BuildCapabilityManifest, runtime_LogCapabilityManifest, packet_observer_Initialize, hooks_Initialize [EXTRACTED 1.00]
- **Capability Manifest Consumers** — runtime_Manifest, hooks_Initialize, packet_observer_Initialize, proxy_g_capabilities [EXTRACTED 0.95]
- **Capability Manifest Gatekeepers** — src_fingerprint_cpp, src_hook_manager_cpp, src_packet_observer_cpp, src_runtime_capabilities_cpp [INFERRED 0.85]
- **dinput8.dll Build Pipeline** — cmakelists_monomythclientdll, cmakelists_dinput8, changelog_github_actions_ci, changelog_build_artifacts [EXTRACTED 1.00]
- **Safety Model Components** — readme_safety_model, readme_fail_closed, rationale_inert_packet_observer, rationale_no_memory_patching [EXTRACTED 0.90]
- **TLO Registration via AddMQ2Data** — initializemq2data_func, datacharacter_func, datatarget_func, dataspawn_func, datawindow_func, addmq2data_func [EXTRACTED 1.00]
- **Type Constructors Called by InitializeMQ2DataTypes** — initializemq2datatypes_func, char_constructor, window_constructor, mq2spawntype, mq2_spelltype, mq2_altabilitytype [EXTRACTED 1.00]
- **CharacterType Inherits Spawn via Dynamic Delegation** — mq2charactertype, char_getmember_dispatcher, mq2spawntype, char_constructor [EXTRACTED 1.00]

## Communities

### Community 0 - "MQ2 Data/Type Registry"
Cohesion: 0.12
Nodes (24): AddDetour, AddMQ2Data, AddMQ2Type, CharacterType Constructor (FUN_1001b360), dataCharacter, dataSpawn, dataTarget, dataWindow (+16 more)

### Community 1 - "Hook Lifecycle Management"
Cohesion: 0.15
Nodes (22): Capability-Gated Initialization, Compile-Time Feature Flags, DllMain Entry Point, monomyth::fingerprint::Evaluate, ROF2 Client Version Validation, monomyth::fingerprint::Result, monomyth::hooks::Initialize, monomyth::hooks::Shutdown (+14 more)

### Community 2 - "DLL Build Configuration"
Cohesion: 0.15
Nodes (10): C++20 Standard Requirement, dinput8 Shared Library Target, dinput8.def Module Definition, DIRECTINPUT_VERSION 0x0800, MonomythClientDll Project, AppendBoolField(), BuildCapabilityManifest(), BuildDisabledCapabilityManifest() (+2 more)

### Community 3 - "Design Rationale and Capabilities"
Cohesion: 0.17
Nodes (15): version Library Dependency, Rationale: Hook capability fail-closed before any install point, Rationale: PacketObserver inert scaffold for safe future work, Rationale: No memory patches, detours, or gameplay behavior, Rationale: DirectInput proxying is primary responsibility, Runtime Capability Manifest, DirectInput8 Proxy Pattern, Fail-Closed Hook Capability (+7 more)

### Community 4 - "DirectInput Export Functions"
Cohesion: 0.2
Nodes (3): BuildSystemDinputPath(), InitializeOnce(), ResolveExports()

### Community 5 - "Logging Infrastructure"
Cohesion: 0.33
Nodes (8): BuildLocalLogPath(), BuildTempLogPath(), EnsureLogFile(), Log(), OpenLogFileAt(), TimestampPrefix(), WriteLine(), Logging Subsystem

### Community 6 - "Character Type Hierarchy"
Cohesion: 0.33
Nodes (9): CharacterType GetMember Dispatcher (FUN_100df000), Character Inherits Spawn via Delegation, Cleanroom Reimpl Design Rationale, MQ2AltAbilityType, MQ2ItemType, MQ2SpellType, MQ2CharacterType, MQ2SpawnType (+1 more)

### Community 7 - "DLL Proxy Passthrough"
Cohesion: 0.25
Nodes (8): DLL Proxy Passthrough Pattern, Exported DirectInput8Create Proxy, Exported DllCanUnloadNow Proxy, Exported DllGetClassObject Proxy, Exported DllRegisterServer Proxy, Exported DllUnregisterServer Proxy, Exported GetdfDIJoystick Proxy, monomyth::proxy::EnsureInitialized

### Community 8 - "DLL Entry Points"
Cohesion: 0.25
Nodes (0): 

### Community 9 - "Client Fingerprinting"
Cohesion: 0.57
Nodes (6): Basename(), ContainsValue(), Evaluate(), GetProcessPath(), ReadVersionStrings(), ToLower()

### Community 10 - "DirectInput Hook COM Wrappers"
Cohesion: 0.4
Nodes (6): dinput8.dll, DirectInput Proxy Layer, IDirectInput8Hook COM Wrapper, IDirectInputDevice8Hook COM Wrapper, MQ2Data System, MQ2Type System

### Community 11 - "CI/CD Build Pipeline"
Cohesion: 0.67
Nodes (3): Debug and Release dinput8.dll Build Artifacts, GitHub Actions CI for 32-bit Windows, GitHub Actions CI Workflow

### Community 12 - "Logger Header"
Cohesion: 1.0
Nodes (0): 

### Community 13 - "Fingerprint Header"
Cohesion: 1.0
Nodes (0): 

### Community 14 - "DInput Proxy Header"
Cohesion: 1.0
Nodes (0): 

### Community 15 - "Hook Manager Header"
Cohesion: 1.0
Nodes (0): 

### Community 16 - "Packet Observer Header"
Cohesion: 1.0
Nodes (0): 

### Community 17 - "Log Thread Safety"
Cohesion: 1.0
Nodes (2): Log File Handle, Log Mutex (Thread Safety)

### Community 18 - "Config Header"
Cohesion: 1.0
Nodes (0): 

### Community 19 - "Runtime Capabilities Header"
Cohesion: 1.0
Nodes (0): 

## Knowledge Gaps
- **32 isolated node(s):** `Compile-Time Feature Flags`, `monomyth::logger::Flush`, `Log Mutex (Thread Safety)`, `Log File Handle`, `Exported DllCanUnloadNow Proxy` (+27 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **Thin community `Logger Header`** (2 nodes): `monomyth()`, `logger.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Fingerprint Header`** (2 nodes): `fingerprint()`, `fingerprint.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `DInput Proxy Header`** (2 nodes): `monomyth()`, `dinput_proxy.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Hook Manager Header`** (2 nodes): `monomyth()`, `hook_manager.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Packet Observer Header`** (2 nodes): `packet_observer()`, `packet_observer.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Log Thread Safety`** (2 nodes): `Log File Handle`, `Log Mutex (Thread Safety)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Config Header`** (1 nodes): `config.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Runtime Capabilities Header`** (1 nodes): `runtime_capabilities.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `dinput8 Shared Library Target` connect `DLL Build Configuration` to `Design Rationale and Capabilities`, `DirectInput Export Functions`, `Logging Infrastructure`, `DLL Entry Points`, `Client Fingerprinting`?**
  _High betweenness centrality (0.132) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `dataSpawn` (e.g. with `dataCharacter` and `dataTarget`) actually correct?**
  _`dataSpawn` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `Compile-Time Feature Flags`, `monomyth::logger::Flush`, `Log Mutex (Thread Safety)` to the rest of the system?**
  _32 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `MQ2 Data/Type Registry` be split into smaller, more focused modules?**
  _Cohesion score 0.12 - nodes in this community are weakly interconnected._