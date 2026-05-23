#include "hook_manager.h"

#include <windows.h>

#include <atomic>
#include <array>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include "logger.h"
#include "multiclass_cache.h"
#include "multiclass_identity.h"
#include "multiclass_skill_visibility.h"
#include "opcode_reference.h"
#include "packet_observer.h"
#include "server_auth_stats_observer.h"
#include "spell_level_selection.h"

namespace monomyth::hooks {
namespace {

bool g_initialized = false;

#if defined(_M_IX86) || defined(__i386__)

#if defined(_MSC_VER)
#define MONOMYTH_FASTCALL __fastcall
#define MONOMYTH_THISCALL __thiscall
#define MONOMYTH_NOINLINE __declspec(noinline)
#define MONOMYTH_USED
#else
#define MONOMYTH_FASTCALL __attribute__((fastcall))
#define MONOMYTH_THISCALL __attribute__((thiscall))
#define MONOMYTH_NOINLINE __attribute__((noinline))
#define MONOMYTH_USED __attribute__((used))
#endif

constexpr std::size_t kJmpPatchBytes = 5;
constexpr std::size_t kMaxStolenBytes = 16;
constexpr std::size_t kIntermediateSendPrefixByteCap = 16;
constexpr std::uint64_t kSpellTraceInitialLogCount = 20;
constexpr std::uint64_t kSpellTraceLogInterval = 100;
constexpr std::uint64_t kSkillVisibilityOverrideInitialLogCount = 32;
constexpr std::uint64_t kSkillVisibilityOverrideLogInterval = 100;
constexpr std::uint32_t kMemorizeSpellOpcode = 0x217c;
constexpr std::uint32_t kDeleteSpellOpcode = 0x3358;
constexpr std::uint32_t kMoveItemOpcode = 0x32ee;
constexpr std::uint32_t kGmTrainingOpcode = 0x1966;
constexpr std::uint32_t kGmEndTrainingOpcode = 0x4d6b;
constexpr std::uint32_t kMemorizeSendCorrelationMaxWrapperSends = 8;
constexpr std::uint32_t kSpellbookScribeCorrelationMaxWrapperSends = 8;
constexpr std::uint32_t kInventoryClassDisplayCorrelationBudget = 48;
constexpr std::uint32_t kItemDisplayClassDisplayCorrelationBudget = 64;
constexpr std::size_t kScribeGateDescriptorTableOffset = 0x4;
constexpr std::size_t kScribeGateRelativeOffsetField = 0x4;
constexpr std::size_t kScribeGateLookupContextBias = 0x8;
constexpr std::size_t kScribeGateLookupKeyOffset = 0x4;
constexpr std::size_t kScribeGateNodeRecordOffset = 0x4;
constexpr std::size_t kScribeGateNodeNextOffset = 0xc;
constexpr std::size_t kScribeGateResolvedClassIdOffset = 0x3374;
constexpr std::uint32_t kScribeGateMaxRelativeLookupOffset = 0x00100000;
constexpr std::size_t kScribeGateMaxLookupNodes = 256;
constexpr std::size_t kClientItemClassMaskOffset = 0x68;
constexpr std::size_t kClientItemInfoEquipSlotsOffset = 0x0f0;
constexpr std::size_t kClientItemInfoClassesOffset = 0x170;
constexpr std::size_t kClientItemInfoItemClassOffset = 0x1d4;
constexpr std::uint32_t kClientItemWrapperGetDataThunkRva = 0x003b06e0;
constexpr std::uint32_t kCharacterZoneClientHasSkillRva = 0x0004a1b0;
constexpr std::int32_t kFirstGeneralInventorySlot = 23;
constexpr std::int32_t kPrimaryEquipmentSlot = 13;
constexpr std::int32_t kSecondaryEquipmentSlot = 14;
constexpr std::uint32_t kSecondaryEquipmentSlotBit = 1u << kSecondaryEquipmentSlot;
constexpr std::size_t kLocalPlayerCharacterOffset = 0x02cc;
constexpr std::size_t kLocalPlayerHoldingAnimationOffset = 0x0334;
constexpr std::uint8_t kHoldingAnimationShield = 2;
constexpr std::uint8_t kHoldingAnimationDualWield = 3;
constexpr std::uint8_t kHoldingAnimationLeftHandWeapon = 5;
constexpr std::uint8_t kHoldingAnimationTwoHandedWeapon = 6;
constexpr std::uint8_t kHoldingAnimationBow = 7;
constexpr int kSkillDualWield = 22;
constexpr std::size_t kPacketOpcodeBytes = 2;
constexpr std::uint32_t kEquipClickRecordLookupCallsiteRva = 0x000f7827;
constexpr std::uint32_t kEquipClickCanEquipCallsiteRva = 0x000f7841;
constexpr std::uint32_t kEquipClickRequirementLookupCallsiteRva = 0x000f78d5;
constexpr std::uint32_t kAutoEquipClassGateCallerReturnARva = 0x000fe184;
constexpr std::uint32_t kAutoEquipClassGateCallerReturnBRva = 0x000fe354;
constexpr std::uint32_t kAutoEquipClassGateTargetRva = 0x0004c430;
constexpr std::uint32_t kGuildTrainerClassLookupCallsiteRva = 0x001374a1;
constexpr std::uint32_t kGuildTrainerClassLookupTargetRva = 0x003db210;
constexpr std::uint32_t kEquipLocalRejectMessageCallsiteRva = 0x000f7987;
constexpr std::uint32_t kDragDropLocalRejectMessageCallsiteRva = 0x002995b3;
constexpr std::uint32_t kDragDropSilentPrecheckCallsiteRva = 0x00299718;
constexpr std::uint32_t kMoveItemFromSlotResolveCallsiteRva = 0x0029973a;
constexpr std::uint32_t kMoveItemToSlotResolveCallsiteRva = 0x00299754;
constexpr std::uint32_t kMoveItemBranchKindSiteACallsiteRva = 0x00295eeb;
constexpr std::uint32_t kMoveItemBranchKindSiteBCallsiteRva = 0x002960d3;
constexpr std::uint32_t kMoveItemBranchKindTargetRva = 0x003affd0;
constexpr std::uint32_t kMoveItemBranchBoolSiteACallsiteRva = 0x00295ef9;
constexpr std::uint32_t kMoveItemBranchBoolSiteBCallsiteRva = 0x002960f2;
constexpr std::uint32_t kMoveItemBranchBoolTargetRva = 0x003b03d0;
constexpr std::uint32_t kMoveItemStackLocalGateSiteBCallsiteRva = 0x00296111;
constexpr std::uint32_t kMoveItemStackLocalGateTargetRva = 0x000322b0;
constexpr std::uint32_t kEverQuestLMouseUpTargetRva = 0x000c1760;
constexpr std::uint32_t kCXWndHandleLButtonUpTargetRva = 0x000c1e91;
constexpr std::uint32_t kInventoryWindowWndNotificationTargetRva = 0x00293490;
constexpr std::uint32_t kInventorySummaryRefreshCandidateARva = 0x00290100;
constexpr std::uint32_t kInventorySummaryRefreshCandidateBRva = 0x002905f0;
constexpr std::uint32_t kInventorySummaryRefreshCandidateCRva = 0x00312b00;
constexpr std::uint32_t kInventorySummaryRefreshCandidateDRva = 0x00292c90;
constexpr std::uint32_t kCharSelectClassNameFuncRva = 0x00321210;
constexpr std::uint32_t kCharacterListWndUpdateListRva = 0x00430760;
constexpr std::uint32_t kCharSelectNativeClassSubstitutionBudget = 48;
constexpr std::uint32_t kItemDisplayRefreshWorkerTargetRva = 0x002ae100;
constexpr std::uint32_t kItemDisplayClassRowBuilderTargetRva = 0x002a9540;
constexpr std::uint32_t kInvSlotWndHandleLButtonUpTargetRva = 0x0029a5d0;
constexpr std::uint32_t kInvSlotWndHandleLButtonUpAfterHeldTargetRva = 0x00299c30;
constexpr std::uint32_t kInvSlotHandleLButtonCoreTargetRva = 0x00295670;
constexpr std::uint32_t kInvSlotHandleLButtonCoreCallerReturnRva = 0x0029a640;
constexpr std::uint32_t kInvSlotHandleLButtonCorePrecheckCallsiteRva = 0x002956bb;
constexpr std::uint32_t kInvSlotHandleLButtonCorePrecheckTargetRva = 0x00464140;
constexpr std::uint32_t kInvSlotHandleLButtonCoreModeBitsACallsiteRva = 0x00295709;
constexpr std::uint32_t kInvSlotHandleLButtonCoreModeBitsBCallsiteRva = 0x0029571a;
constexpr std::uint32_t kInvSlotHandleLButtonCoreModeBitsCCallsiteRva = 0x00295765;
constexpr std::uint32_t kInvSlotHandleLButtonCoreModeBitsDCallsiteRva = 0x00295774;
constexpr std::uint32_t kInvSlotHandleLButtonCoreModeBitsTargetRva = 0x00475bd0;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLocalGateCallsiteRva = 0x002957cf;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLocalGateTargetRva = 0x00294140;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolveObjectCallsiteRva = 0x002957e3;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolveObjectTargetRva = 0x00294780;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolvedKindACallsiteRva = 0x00295808;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolvedKindBCallsiteRva = 0x00295864;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolvedKindTargetRva = 0x003affd0;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolvedFlagACallsiteRva = 0x00295815;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolvedFlagBCallsiteRva = 0x00295871;
constexpr std::uint32_t kInvSlotHandleLButtonCoreResolvedFlagTargetRva = 0x003b06b0;
constexpr std::uint32_t kInvSlotHandleLButtonCoreModeBitsECallsiteRva = 0x002958b6;
constexpr std::uint32_t kInvSlotHandleLButtonCoreAltDispatchCallsiteRva = 0x002958ea;
constexpr std::uint32_t kInvSlotHandleLButtonCoreAltDispatchTargetRva = 0x002ae610;
constexpr std::uint32_t kInvSlotHandleLButtonCoreSlot17MessageCallsiteRva = 0x00295920;
constexpr std::uint32_t kInvSlotHandleLButtonCoreSlot17MessageTargetRva = 0x0029baf0;
constexpr std::uint32_t kInvSlotHandleLButtonCoreGlobalAction49CallsiteRva = 0x0029598d;
constexpr std::uint32_t kInvSlotHandleLButtonCoreGlobalAction49TargetRva = 0x000421e0;
constexpr std::uint32_t kInvSlotHandleLButtonCoreItemRangeGateCallsiteRva = 0x002959a9;
constexpr std::uint32_t kInvSlotHandleLButtonCoreItemRangeGateTargetRva = 0x000ad150;
constexpr std::uint32_t kInvSlotHandleLButtonCorePostResolveGateCallsiteRva = 0x002959e5;
constexpr std::uint32_t kInvSlotHandleLButtonCorePostResolveGateTargetRva = 0x00032960;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateSlot17GateCallsiteRva = 0x00295a37;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateSlot17GateTargetRva = 0x002f0180;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateSlot17ApplyCallsiteRva = 0x00295a5b;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateSlot17ApplyTargetRva = 0x002f31a0;
constexpr std::uint32_t kInvSlotHandleLButtonCoreManagerPrecheckACallsiteRva = 0x002959fc;
constexpr std::uint32_t kInvSlotHandleLButtonCoreManagerPrecheckBCallsiteRva = 0x00295a6f;
constexpr std::uint32_t kInvSlotHandleLButtonCoreManagerPrecheckCCallsiteRva = 0x00295ab2;
constexpr std::uint32_t kInvSlotHandleLButtonCoreManagerPrecheckDCallsiteRva = 0x00295ae3;
constexpr std::uint32_t kInvSlotHandleLButtonCoreManagerPrecheckECallsiteRva = 0x00295b18;
constexpr std::uint32_t kInvSlotHandleLButtonCoreManagerPrecheckFCallsiteRva = 0x00295b82;
constexpr std::uint32_t kInvSlotHandleLButtonCorePostLookupModeGateCallsiteRva = 0x00295c6c;
constexpr std::uint32_t kInvSlotHandleLButtonCorePostLookupModeGateTargetRva = 0x003b0380;
constexpr std::uint32_t kInvSlotHandleLButtonCorePostLookupUiPulseCallsiteRva = 0x00295d0f;
constexpr std::uint32_t kInvSlotHandleLButtonCorePostLookupUiPulseTargetRva = 0x00325b00;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchGateACallsiteRva = 0x00295fe9;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchGateATargetRva = 0x00032290;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchPrepCallsiteRva = 0x00296026;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchPrepTargetRva = 0x001831b0;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchGateBCallsiteRva = 0x0029604f;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchGateBTargetRva = 0x0025c160;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchDispatchCallsiteRva = 0x00296091;
constexpr std::uint32_t kInvSlotHandleLButtonCoreLateBranchDispatchTargetRva = 0x0004c430;
constexpr std::uint32_t kInvSlotHandleLButtonCoreStateRootRva = 0x009d2630;
constexpr std::uint32_t kInvSlotHandleLButtonCoreDispatcherObjectRva = 0x00a646b0;
constexpr std::uint32_t kMoveItemOwnerPrimarySetterTargetRva = 0x003bb560;
constexpr std::uint32_t kMoveItemCtorSiteACallsiteRva = 0x00295f36;
constexpr std::uint32_t kMoveItemCtorSiteBCallsiteRva = 0x0029631e;
constexpr std::uint32_t kMoveItemValidationGateTargetRva = 0x00298d80;
constexpr std::uint32_t kMoveItemCtorTargetRva = 0x00060130;
constexpr std::uint32_t kMoveItemSlot21LookupTargetRva = 0x0002dec0;
constexpr std::uint32_t kMoveItemSlot21LookupCallerReturnRva = 0x000c1faa;
constexpr std::uint32_t kInventoryWndNotificationSlot21LookupCallerReturnARva = 0x00193ae3;
constexpr std::uint32_t kInventoryWndNotificationSlot21LookupCallerReturnBRva = 0x00193b11;
constexpr std::uint32_t kInvSlotHandleLButtonCoreSlot21LookupCallerReturnARva = 0x00295bea;
constexpr std::uint32_t kInvSlotHandleLButtonCoreSlot21LookupCallerReturnBRva = 0x00295c37;
constexpr std::uint32_t kMoveItemSlotPopulateTargetRva = 0x003e4f40;
constexpr std::uint32_t kMoveItemSlotPopulateCallerReturnRva = 0x00060157;
constexpr std::uint32_t kMoveItemDescriptorBuildTargetRva = 0x0005fd30;
constexpr std::uint32_t kMoveItemDescriptorBuildCallerReturnRva = 0x003e4f69;
constexpr std::uint32_t kDragDropSilentPrecheckTargetRva = 0x003b1160;
constexpr std::uint32_t kMoveItemSlotResolveTargetRva = 0x003ea290;
constexpr std::uint32_t kEquipLocalRecordLookupCallsiteRva = 0x000a41d6;
constexpr std::uint32_t kEquipLocalRequirementLookupACallsiteRva = 0x000a4201;
constexpr std::uint32_t kEquipLocalRequirementLookupBCallsiteRva = 0x000a42c2;
constexpr std::uint32_t kEquipNestedInventoryGateCallsiteRva = 0x000a42a9;
constexpr std::uint32_t kEquipNestedValidatorCallsiteRva = 0x000a45ea;
constexpr std::uint32_t kEquipRecordLookupTargetRva = 0x000a3c60;
constexpr std::uint32_t kEquipRequirementLookupTargetRva = 0x000a3d70;
constexpr std::uint32_t kEquipNestedInventoryGateTargetRva = 0x00182670;
constexpr std::uint32_t kEquipNestedValidatorTargetRva = 0x000a4230;
constexpr std::uint32_t kEquipMessageResolverTargetRva = 0x003d0660;
constexpr std::uint32_t kDragContextGlobalRva = 0x009d261c;
constexpr std::uint32_t kDragFlagsGlobalRva = 0x009d2660;
constexpr std::uint32_t kActiveWindowGlobalRva = 0x009d25ac;
constexpr std::uint32_t kInventoryWindowGlobalRva = 0x0091ec8c;
constexpr std::uint32_t kActiveGuildTrainerGlobalRva = 0x009d2658;
// MQ2 legacy ROF2 exposes pinstCharData at this same RVA; use it as the
// authoritative local character object when we need the live PC profile.
constexpr std::uint32_t kLocalCharDataGlobalRva = 0x009d261c;
constexpr std::uint32_t kLocalPlayerGlobalRva = 0x009d2630;
constexpr std::uint32_t kGetClassDescRva = 0x00114dc0;
constexpr std::uint32_t kGetClassThreeLetterCodeRva = 0x001153c0;
constexpr std::uint32_t kGetDeityDescRva = 0x00115f70;
constexpr std::uint32_t kCXStrAssignTargetRva = 0x00405d90;
constexpr std::uint32_t kCXWndSetWindowTextATargetRva = 0x00406ec0;
// eqlib maps 0x136310 to CEverQuest::LeftClickedOnPlayer, not a live /who wrapper.
// Keep the detour code path for legacy context experiments, but label the seam honestly.
constexpr std::uint32_t kLeftClickedOnPlayerSurrogateRva = 0x00136310;
constexpr std::uint32_t kWhoClassNameClassLookupTargetRva = 0x003d0660;
constexpr std::uint32_t kWhoClassNameClassLookupCallerReturnARva = 0x001364ec;
constexpr std::uint32_t kWhoClassNameClassLookupCallerReturnBRva = 0x001365c7;
constexpr std::uint32_t kWhoClassNameClassLookupCallerReturnCRva = 0x00136606;
constexpr std::uint32_t kWhoAllClassLabelLookupCallerRva = 0x000477e6;
constexpr std::uint32_t kItemDisplayStringLookupCallerRvaMin = 0x002a0000;
constexpr std::uint32_t kItemDisplayStringLookupCallerRvaMaxExclusive = 0x002b0000;
constexpr std::uint32_t kProgressionSelectionClassLookupCallsiteRva = 0x003212b6;
constexpr std::uint32_t kProgressionSelectionClassLookupTargetRva = 0x00042c00;
constexpr std::uint32_t kCharSelectLateFullNameCallsiteARva = 0x003249ac;
constexpr std::uint32_t kCharSelectLateFullNameCallsiteBCallsiteRva = 0x00324d84;
constexpr std::uint32_t kCharSelectLateFullNameCallsiteCCallsiteRva = 0x003252ab;
constexpr std::size_t kInventoryClassTitleControlOffset = 0x02cc;
constexpr std::size_t kCxWndTextFieldOffset = 0x00e8;
constexpr std::size_t kEqSpawnNameOffset = 0x00a4;
constexpr std::size_t kEqSpawnNameMaxBytes = 0x40;
constexpr std::size_t kGuildTrainerClassLookupResolvedClassOffset = 0x3374;
constexpr std::size_t kGuildTrainerCallerClassOffset = 0x0eb8;
// MacroQuest eqlib's emu/ROF2 layout reads PlayerClient::GetClass() from
// mActorClient.Class. With mActorClient at 0x0ea4 and Class at +0x13c inside
// ActorClient, the effective in-world class field is PlayerClient + 0x0fe0.
constexpr std::size_t kEqPlayerDisplayedClassOffset = 0x0fe0;
constexpr wchar_t kMemorizeSendTraceSliceId[] = L"CLIENT-MEM-SEND-TRACE-001";
constexpr std::array<std::uint8_t, 5> kGuildTrainerClassLookupCallsiteBytes = {{
    0xe8, 0x6a, 0x3d, 0x2a, 0x00,
}};

using ReceiveDispatchFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* source_context,
    std::uint32_t opcode,
    const void* payload,
    std::uint32_t payload_length);
using HandleRButtonUpFn = void (MONOMYTH_THISCALL*)(
    void* this_slot,
    void* point);
using GetSpellLevelNeededFn = std::uint8_t (MONOMYTH_THISCALL*)(
    const void* this_spell,
    unsigned int class_id);
// Cleanroom evidence only proves a bool-like predicate with ECX=this and one class-id arg.
using IsClassUsablePredicateFn = int (MONOMYTH_THISCALL*)(
    void* this_character,
    unsigned int class_id);
using GuildTrainerClassLookupFn = void* (MONOMYTH_THISCALL*)(void* this_context);
// Cleanroom evidence pins a bool-like item usability gate with ECX=this and five stack args.
using CanEquipFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    void* inventory_or_container_like,
    void* item_like,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5);
using InvSlotMgrMoveItemFn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    void* move_data_a,
    void* move_data_b,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5,
    unsigned long arg6);
using EquipRecordLookupFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    unsigned long lookup_id);
using EquipRequirementLookupFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    void* record_or_table_like,
    void* descriptor_like,
    unsigned long mode_like);
using EquipNestedInventoryGateFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    unsigned long arg1,
    unsigned long arg2,
    unsigned long arg3);
using EquipNestedValidatorFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    void* arg1,
    void* arg2,
    unsigned long arg3);
using EquipMessageResolverFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    unsigned long message_id,
    unsigned long arg2,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5);
using DragDropSilentPrecheckFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context,
    void* probe_like);
using MoveItemSlotResolveFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* slot_like,
    void* resolved_slot_like);
using MoveItemBranchKindFn = int (MONOMYTH_THISCALL*)(
    void* this_context);
using MoveItemBranchBoolFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context);
using MoveItemStackLocalGateFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context);
using MoveItemBranchResolvedObjectFn = void* (MONOMYTH_THISCALL*)(
    void* this_context);
using ItemWrapperGetDataThunkFn = void* (MONOMYTH_THISCALL*)(
    void* this_context);
using CharacterZoneClientHasSkillFn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    int skill_id);
using EverQuestLMouseUpFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* point_like);
using CXWndHandleLButtonUpFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    void* point_like,
    std::uint32_t flags_like);
using InventoryWindowWndNotificationFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    void* sender_window,
    std::uint32_t notification_code,
    void* payload_like);
using ItemDisplayRefreshWorkerFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* arg1_like,
    void* arg2_like);
using ItemDisplayClassRowBuilderFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* arg1_like,
    void* arg2_like);
using InvSlotWndHandleLButtonUpFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    void* point_like,
    std::uint32_t flags_like);
using CXStrAssignFn = void* (MONOMYTH_THISCALL*)(
    void* dst_cxstr_field,
    void* src_cxstr_temp);
using InvSlotHandleLButtonCoreFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* point_like,
    std::uint32_t slot_like,
    std::uint32_t held_flag_like);
using InvSlotHandleLButtonCorePrecheckFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context);
using InvSlotHandleLButtonCoreModeBitsFn = std::uint32_t (MONOMYTH_THISCALL*)(
    void* this_context);
using InvSlotHandleLButtonCoreLocalGateFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context,
    const void* slot_record);
using InvSlotHandleLButtonCoreResolveObjectFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    void* output_pointer);
using InvSlotHandleLButtonCoreResolvedFlagFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context);
using InvSlotHandleLButtonCoreAltDispatchFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    const void* slot_record,
    std::uint32_t mode_flag);
using InvSlotHandleLButtonCoreSlot17MessageFn = void* (CDECL*)(
    std::uint32_t message_id,
    void* text_builder_like,
    std::uint32_t zero_like);
using InvSlotHandleLButtonCoreGlobalAction49Fn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    std::uint32_t action_id);
using InvSlotHandleLButtonCoreItemRangeGateFn = std::uint8_t (CDECL*)(
    std::uint32_t item_id_like);
using InvSlotHandleLButtonCorePostResolveGateFn = std::uint32_t (WINAPI*)(
    void* slot_context_like,
    std::uint32_t mode_like);
using InvSlotHandleLButtonCorePostLookupModeGateFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context);
using InvSlotHandleLButtonCorePostLookupUiPulseFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    std::uint32_t target_id,
    std::int32_t value_a,
    std::int32_t value_b,
    std::int32_t x,
    std::int32_t y,
    std::uint32_t one_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b);
using InvSlotHandleLButtonCoreLateBranchGateAFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* slot_record_like);
using InvSlotHandleLButtonCoreLateBranchPrepFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context,
    std::int32_t slot_like,
    void* lookup_result_like);
using InvSlotHandleLButtonCoreLateBranchGateBFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* slot_record_like);
using InvSlotHandleLButtonCoreLateBranchDispatchFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context,
    void* lookup_result_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b);
using InvSlotHandleLButtonCoreLateSlot17GateFn = std::uint8_t (MONOMYTH_THISCALL*)(
    void* this_context,
    const void* slot_record);
using InvSlotHandleLButtonCoreLateSlot17ApplyFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    const void* slot_record);
using MoveItemOwnerPrimarySetterFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* primary_object_like);
using MoveItemValidationGateFn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    void* move_data_a,
    void* move_data_b,
    std::uint32_t arg3,
    std::uint32_t arg4,
    std::uint32_t arg5,
    std::uint32_t arg6);
using MoveItemCtorFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    void* slot_like,
    std::uint32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4);
using MoveItemSlot21LookupFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    void* output_like,
    std::uint32_t slot_like);
using MoveItemDescriptorBuildFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    void* output_words,
    std::int32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4);
using MoveItemSlotPopulateFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    void* slot_like,
    std::int32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4,
    std::int32_t arg5);
using SpellbookDispatcherFn = void (MONOMYTH_THISCALL*)(
    void* this_window,
    int slot_like);
using StartSpellScribePathFn = int (MONOMYTH_THISCALL*)(
    void* this_window,
    int spellbook_entry_like);
using StartSpellScribePrecheckModeGetterFn = int (MONOMYTH_THISCALL*)(
    void* this_context);
using StartSpellScribePrecheckGateFn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    void* descriptor_like,
    int require_known_like,
    int allow_recheck_like);
using StartSpellScribePrecheckLookupFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    void* spell_or_scroll_like);
using StartSpellScribePrecheckFastAcceptFn = int (MONOMYTH_THISCALL*)(
    void* this_context);
using StartSpellScribePrecheckClassResolverFn = void* (MONOMYTH_THISCALL*)(
    void* this_context);
using StartSpellScribePrecheckAssignedMaskGetterFn = std::uint32_t (MONOMYTH_THISCALL*)(
    void* this_context,
    int flag_like,
    int extra_like);
using StartSpellScribePrecheckRule4462c0Fn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    void* descriptor_like,
    int flag_like);
using StartSpellScribePrecheckRule446190Fn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    void* descriptor_like,
    int flag_like);
using StartSpellScribePrecheckRule446200Fn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    void* descriptor_like,
    int flag_like);
using StartSpellScribePrecheckRule446380Fn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    void* descriptor_like,
    int flag_like,
    int zero_like);
using CanStartMemmingFn = bool (MONOMYTH_THISCALL*)(
    void* this_window,
    int spell_or_book_index);
using SpellbookMemorizeSendPathFn = int (MONOMYTH_THISCALL*)(
    void* this_window);
using StartSpellMemorizationPathFn = int (MONOMYTH_THISCALL*)(
    void* this_window,
    std::uint32_t pending_slot_state,
    std::uint32_t zero_like_a,
    std::uint32_t mode_like,
    std::int32_t spell_or_book_index,
    std::uint32_t zero_like_b,
    std::int32_t slot_like);
using MemorizeSendPacketWrapperFn = bool (MONOMYTH_THISCALL*)(
    void* this_context,
    std::uint32_t mode_like,
    const void* packet,
    std::uint32_t total_length);
using GetClassDescFn = const char* (MONOMYTH_THISCALL*)(
    void* this_context,
    unsigned int class_id);
using GetDeityDescFn = const char* (CDECL*)(
    unsigned int deity_id);
using GetClassThreeLetterCodeFn = const char* (MONOMYTH_THISCALL*)(
    void* this_context,
    unsigned int class_id);
using InventorySummaryRefreshCandidateAFn = void (MONOMYTH_THISCALL*)(
    void* this_context);
using InventorySummaryRefreshCandidateBFn = void (MONOMYTH_THISCALL*)(
    void* this_context);
using InventorySummaryRefreshCandidateCFn = void (MONOMYTH_THISCALL*)(
    void* this_context);
using InventorySummaryRefreshCandidateDFn = int (MONOMYTH_THISCALL*)(
    void* this_context);
using CharacterListWndUpdateListFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    bool force_update);
using CharSelectClassNameFuncFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    unsigned int selected_index);
using WhoClassNameFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    void* subject);
using CXWndSetWindowTextAFn = void (MONOMYTH_THISCALL*)(
    void* this_context,
    const char* text);
using ProgressionSelectionClassLookupFn = const char* (CDECL*)(
    unsigned int class_id);
using WhoClassNameClassLookupFn = const char* (MONOMYTH_THISCALL*)(
    void* this_context,
    std::uint32_t string_id,
    void* found_flag_out);

struct InlineDetour {
    std::uint8_t* target = nullptr;
    void* hook = nullptr;
    void* trampoline = nullptr;
    std::size_t patch_length = 0;
    std::array<std::uint8_t, kMaxStolenBytes> original = {};
    std::array<std::uint8_t, kJmpPatchBytes> patch = {};
    bool installed = false;
};

struct CallsitePatch {
    std::uint8_t* address = nullptr;
    std::array<std::uint8_t, kJmpPatchBytes> original = {};
    std::array<std::uint8_t, kJmpPatchBytes> patch = {};
    bool installed = false;
};

struct KnownMemorizeFollowupCallsite {
    std::uint32_t return_rva = 0;
    std::uint32_t mode_like = 0;
    const wchar_t* label = L"unknown";
    std::array<std::uint8_t, kMaxStolenBytes> caller_context_bytes = {};
};

struct KnownMemorizeFollowupCallsiteMatch {
    const KnownMemorizeFollowupCallsite* site = nullptr;
    bool bytes_match = false;
};

struct RelativeCallResolution {
    std::uintptr_t call_site = 0;
    std::uintptr_t target = 0;
    bool resolved = false;
};

struct JumpThunkResolution {
    std::uintptr_t terminal_target = 0;
    std::uint32_t hop_count = 0;
    bool resolved = false;
};

#pragma pack(push, 1)
struct ClientInventorySlotWire {
    std::int16_t type = 0;
    std::int16_t unknown02 = 0;
    std::int16_t slot = 0;
    std::int16_t subindex = 0;
    std::int16_t augindex = 0;
    std::int16_t unknown01 = 0;
};

struct ClientMoveItemWire {
    ClientInventorySlotWire from_slot = {};
    ClientInventorySlotWire to_slot = {};
    std::uint32_t number_in_stack = 0;
};

struct ClientPointWire {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct InvSlotHandleLButtonCoreSlotContextSnapshot {
    std::uintptr_t child_pointer = 0;
    ClientInventorySlotWire slot_record = {};
    std::array<std::uint8_t, 24> this_bytes = {};
    std::array<std::uint8_t, 24> child_bytes = {};
    bool child_pointer_copied = false;
    bool slot_record_copied = false;
    bool this_bytes_copied = false;
    bool child_bytes_copied = false;
};

struct InvSlotHandleLButtonCoreLateManagerSnapshot {
    std::uint32_t field_23c = 0;
    std::uint32_t field_240 = 0;
    std::uint32_t field_244 = 0;
    std::uintptr_t field_248 = 0;
    std::uintptr_t field_254 = 0;
    std::uintptr_t field_258 = 0;
    std::uintptr_t field_25c = 0;
    std::uintptr_t field_260 = 0;
    std::uintptr_t field_264 = 0;
    std::uintptr_t field_2a8 = 0;
    bool field_23c_copied = false;
    bool field_240_copied = false;
    bool field_244_copied = false;
    bool field_248_copied = false;
    bool field_254_copied = false;
    bool field_258_copied = false;
    bool field_25c_copied = false;
    bool field_260_copied = false;
    bool field_264_copied = false;
    bool field_2a8_copied = false;
};

struct InvSlotHandleLButtonCorePostResolveGateSnapshot {
    std::uintptr_t drag_context_root = 0;
    std::uintptr_t drag_context_manager = 0;
    std::uintptr_t state_root = 0;
    std::uintptr_t dispatcher_object = 0;
    std::uintptr_t dispatcher_vtable = 0;
    std::uintptr_t dispatcher_virtual_0c = 0;
    std::int32_t state_field_448 = -1;
    std::uint8_t state_field_eb8 = 0;
    std::uint8_t slot_context_field_244 = 0;
    std::array<std::uint8_t, 24> slot_context_bytes = {};
    std::array<std::uint8_t, 24> drag_context_manager_bytes = {};
    bool drag_context_root_copied = false;
    bool drag_context_manager_copied = false;
    bool state_root_copied = false;
    bool dispatcher_object_copied = false;
    bool dispatcher_vtable_copied = false;
    bool dispatcher_virtual_0c_copied = false;
    bool state_field_448_copied = false;
    bool state_field_eb8_copied = false;
    bool slot_context_field_244_copied = false;
    bool slot_context_bytes_copied = false;
    bool drag_context_manager_bytes_copied = false;
};
#pragma pack(pop)

struct SpellbookMemStateSnapshot {
    std::uint32_t state_234 = 0;
    std::uint32_t state_238 = 0;
    std::uint32_t state_23c = 0;
    std::uint32_t state_240 = 0;
    std::uint32_t state_244 = 0;
    bool state_234_copied = false;
    bool state_238_copied = false;
    bool state_23c_copied = false;
    bool state_240_copied = false;
    bool state_244_copied = false;
};

struct StartSpellScribePrecheckClassMaskSnapshot {
    bool active = false;
    std::uint32_t correlation_id = 0;
    bool class_resolver_called = false;
    bool class_resolver_emulated = false;
    void* class_resolver_this = nullptr;
    std::uintptr_t class_lookup_context = 0;
    bool class_lookup_context_copied = false;
    std::uint32_t class_lookup_key = 0;
    bool class_lookup_key_copied = false;
    void* class_record = nullptr;
    bool class_id_copied = false;
    std::uint8_t class_id = 0;
    bool assigned_mask_getter_called = false;
    void* assigned_mask_getter_this = nullptr;
    int assigned_mask_flag_like = 0;
    int assigned_mask_extra_like = 0;
    std::uint32_t assigned_mask = 0;
    bool rule_4462c0_called = false;
    bool rule_446190_called = false;
    bool rule_446200_called = false;
    bool rule_446380_called = false;
    bool authoritative_mask_present = false;
    std::uint32_t authoritative_mask = 0;
    std::uint32_t authoritative_mask_intersection = 0;
    bool authoritative_mask_intersects_spell_mask = false;
    bool behavior_override_applied = false;
};

constexpr std::array<KnownMemorizeFollowupCallsite, 2> kKnownMemorizeFollowupCallsites = {{
    {
        0x0013e1cd,
        0,
        L"PostCanStartMemmingClientUpdateBranch",
        {{0x00, 0x66, 0xa5, 0xe8, 0x23, 0x70, 0x38, 0x00,
          0x6a, 0x00, 0x8a, 0xd8, 0xe8, 0x7b, 0xcf, 0x39}}
    },
    {
        0x0018caba,
        4,
        L"PostCanStartMemmingFloatListThingBranch",
        {{0x56, 0x6a, 0x04, 0xe8, 0x36, 0x87, 0x33, 0x00,
          0x55, 0x8a, 0xd8, 0xe8, 0x8f, 0xe6, 0x34, 0x00}}
    },
}};

InlineDetour g_receive_dispatch_detour = {};
InlineDetour g_handle_rbutton_up_detour = {};
InlineDetour g_get_spell_level_needed_detour = {};
InlineDetour g_is_class_usable_predicate_detour = {};
InlineDetour g_can_equip_detour = {};
InlineDetour g_inv_slot_mgr_move_item_detour = {};
InlineDetour g_move_item_validation_gate_detour = {};
InlineDetour g_move_item_slot21_lookup_detour = {};
InlineDetour g_move_item_descriptor_build_detour = {};
InlineDetour g_move_item_slot_populate_detour = {};
InlineDetour g_everquest_lmouse_up_detour = {};
InlineDetour g_cxwnd_handle_lbutton_up_detour = {};
InlineDetour g_inventory_window_wnd_notification_detour = {};
InlineDetour g_inventory_summary_refresh_candidate_a_detour = {};
InlineDetour g_inventory_summary_refresh_candidate_b_detour = {};
InlineDetour g_inventory_summary_refresh_candidate_c_detour = {};
InlineDetour g_inventory_summary_refresh_candidate_d_detour = {};
InlineDetour g_character_zone_client_has_skill_detour = {};
InlineDetour g_char_select_class_name_func_detour = {};
InlineDetour g_character_list_update_list_detour = {};
InlineDetour g_item_display_refresh_worker_trace_detour = {};
InlineDetour g_item_display_refresh_worker_detour = {};
InlineDetour g_invslot_wnd_handle_lbutton_up_detour = {};
InlineDetour g_invslot_wnd_handle_lbutton_up_afterheld_detour = {};
InlineDetour g_invslot_handle_lbutton_core_detour = {};
InlineDetour g_move_item_owner_primary_setter_detour = {};
InlineDetour g_spellbook_dispatcher_detour = {};
InlineDetour g_start_spell_scribe_path_detour = {};
InlineDetour g_start_spell_scribe_precheck_mode_getter_detour = {};
InlineDetour g_start_spell_scribe_precheck_gate_detour = {};
InlineDetour g_start_spell_scribe_precheck_lookup_detour = {};
InlineDetour g_start_spell_scribe_precheck_fast_accept_detour = {};
InlineDetour g_start_spell_scribe_precheck_class_resolver_detour = {};
InlineDetour g_start_spell_scribe_precheck_assigned_mask_getter_detour = {};
InlineDetour g_start_spell_scribe_precheck_rule_4462c0_detour = {};
InlineDetour g_start_spell_scribe_precheck_rule_446190_detour = {};
InlineDetour g_start_spell_scribe_precheck_rule_446200_detour = {};
InlineDetour g_start_spell_scribe_precheck_rule_446380_detour = {};
InlineDetour g_can_start_memming_detour = {};
InlineDetour g_spellbook_memorize_send_path_detour = {};
InlineDetour g_start_spell_memorization_path_detour = {};
InlineDetour g_memorize_send_packet_wrapper_detour = {};
InlineDetour g_get_class_desc_detour = {};
InlineDetour g_get_class_three_letter_code_detour = {};
InlineDetour g_get_deity_desc_detour = {};
InlineDetour g_who_class_name_detour = {};
InlineDetour g_who_class_name_class_lookup_detour = {};
InlineDetour g_cxstr_assign_detour = {};
InlineDetour g_cxwnd_set_window_text_a_detour = {};
CallsitePatch g_progression_selection_class_lookup_callsite_patch = {};
CallsitePatch g_char_select_late_full_name_callsite_a_patch = {};
CallsitePatch g_char_select_late_full_name_callsite_b_patch = {};
CallsitePatch g_char_select_late_full_name_callsite_c_patch = {};
CallsitePatch g_equip_click_record_lookup_callsite_patch = {};
CallsitePatch g_equip_click_can_equip_callsite_patch = {};
CallsitePatch g_equip_click_requirement_lookup_callsite_patch = {};
CallsitePatch g_equip_local_reject_message_callsite_patch = {};
CallsitePatch g_drag_drop_local_reject_message_callsite_patch = {};
CallsitePatch g_drag_drop_silent_precheck_callsite_patch = {};
CallsitePatch g_move_item_from_slot_resolve_callsite_patch = {};
CallsitePatch g_move_item_to_slot_resolve_callsite_patch = {};
CallsitePatch g_move_item_branch_kind_site_a_callsite_patch = {};
CallsitePatch g_move_item_branch_kind_site_b_callsite_patch = {};
CallsitePatch g_move_item_branch_bool_site_a_callsite_patch = {};
CallsitePatch g_move_item_branch_bool_site_b_callsite_patch = {};
CallsitePatch g_move_item_stack_local_gate_site_b_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_precheck_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_mode_bits_a_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_mode_bits_b_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_mode_bits_c_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_mode_bits_d_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_local_gate_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_resolve_object_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_resolved_kind_a_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_resolved_kind_b_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_resolved_flag_a_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_resolved_flag_b_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_mode_bits_e_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_alt_dispatch_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_slot17_message_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_global_action49_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_item_range_gate_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_post_resolve_gate_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_late_slot17_gate_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_late_slot17_apply_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_manager_precheck_a_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_manager_precheck_b_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_manager_precheck_c_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_manager_precheck_d_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_manager_precheck_e_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_manager_precheck_f_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_post_lookup_mode_gate_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_post_lookup_ui_pulse_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_late_branch_gate_a_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_late_branch_prep_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_late_branch_gate_b_callsite_patch = {};
CallsitePatch g_invslot_handle_lbutton_core_late_branch_dispatch_callsite_patch = {};
CallsitePatch g_move_item_ctor_site_a_callsite_patch = {};
CallsitePatch g_move_item_ctor_site_b_callsite_patch = {};
CallsitePatch g_guild_trainer_class_lookup_callsite_patch = {};
CallsitePatch g_equip_local_record_lookup_callsite_patch = {};
CallsitePatch g_equip_local_requirement_lookup_a_callsite_patch = {};
CallsitePatch g_equip_local_requirement_lookup_b_callsite_patch = {};
CallsitePatch g_equip_nested_inventory_gate_callsite_patch = {};
CallsitePatch g_equip_nested_validator_callsite_patch = {};
ReceiveDispatchFn g_original_receive_dispatch = nullptr;
HandleRButtonUpFn g_original_handle_rbutton_up = nullptr;
GetSpellLevelNeededFn g_original_get_spell_level_needed = nullptr;
IsClassUsablePredicateFn g_original_is_class_usable_predicate = nullptr;
GuildTrainerClassLookupFn g_original_guild_trainer_class_lookup = nullptr;
CanEquipFn g_original_can_equip = nullptr;
CharacterZoneClientHasSkillFn g_original_character_zone_client_has_skill = nullptr;
InvSlotMgrMoveItemFn g_original_inv_slot_mgr_move_item = nullptr;
EquipRecordLookupFn g_original_equip_record_lookup = nullptr;
EquipRequirementLookupFn g_original_equip_requirement_lookup = nullptr;
EquipNestedInventoryGateFn g_original_equip_nested_inventory_gate = nullptr;
EquipNestedValidatorFn g_original_equip_nested_validator = nullptr;
EquipMessageResolverFn g_original_equip_message_resolver = nullptr;
DragDropSilentPrecheckFn g_original_drag_drop_silent_precheck = nullptr;
MoveItemSlotResolveFn g_original_move_item_slot_resolve = nullptr;
MoveItemBranchKindFn g_original_move_item_branch_kind = nullptr;
MoveItemBranchBoolFn g_original_move_item_branch_bool = nullptr;
MoveItemStackLocalGateFn g_original_move_item_stack_local_gate = nullptr;
EverQuestLMouseUpFn g_original_everquest_lmouse_up = nullptr;
CXWndHandleLButtonUpFn g_original_cxwnd_handle_lbutton_up = nullptr;
InventoryWindowWndNotificationFn g_original_inventory_window_wnd_notification = nullptr;
ItemDisplayRefreshWorkerFn g_original_item_display_refresh_worker_trace = nullptr;
ItemDisplayClassRowBuilderFn g_original_item_display_class_row_builder = nullptr;
InvSlotWndHandleLButtonUpFn g_original_invslot_wnd_handle_lbutton_up = nullptr;
InvSlotWndHandleLButtonUpFn g_original_invslot_wnd_handle_lbutton_up_afterheld = nullptr;
InvSlotHandleLButtonCoreFn g_original_invslot_handle_lbutton_core = nullptr;
InvSlotHandleLButtonCorePrecheckFn g_original_invslot_handle_lbutton_core_precheck = nullptr;
InvSlotHandleLButtonCoreModeBitsFn g_original_invslot_handle_lbutton_core_mode_bits = nullptr;
InvSlotHandleLButtonCoreLocalGateFn g_original_invslot_handle_lbutton_core_local_gate = nullptr;
InvSlotHandleLButtonCoreResolveObjectFn g_original_invslot_handle_lbutton_core_resolve_object =
    nullptr;
MoveItemBranchKindFn g_original_invslot_handle_lbutton_core_resolved_kind = nullptr;
InvSlotHandleLButtonCoreResolvedFlagFn g_original_invslot_handle_lbutton_core_resolved_flag =
    nullptr;
InvSlotHandleLButtonCoreAltDispatchFn g_original_invslot_handle_lbutton_core_alt_dispatch =
    nullptr;
InvSlotHandleLButtonCoreSlot17MessageFn g_original_invslot_handle_lbutton_core_slot17_message =
    nullptr;
InvSlotHandleLButtonCoreGlobalAction49Fn
    g_original_invslot_handle_lbutton_core_global_action49 = nullptr;
InvSlotHandleLButtonCoreItemRangeGateFn
    g_original_invslot_handle_lbutton_core_item_range_gate = nullptr;
InvSlotHandleLButtonCorePostResolveGateFn
    g_original_invslot_handle_lbutton_core_post_resolve_gate = nullptr;
InvSlotHandleLButtonCorePostLookupModeGateFn
    g_original_invslot_handle_lbutton_core_post_lookup_mode_gate = nullptr;
InvSlotHandleLButtonCorePostLookupUiPulseFn
    g_original_invslot_handle_lbutton_core_post_lookup_ui_pulse = nullptr;
InvSlotHandleLButtonCoreLateBranchGateAFn
    g_original_invslot_handle_lbutton_core_late_branch_gate_a = nullptr;
InvSlotHandleLButtonCoreLateBranchPrepFn
    g_original_invslot_handle_lbutton_core_late_branch_prep = nullptr;
InvSlotHandleLButtonCoreLateBranchGateBFn
    g_original_invslot_handle_lbutton_core_late_branch_gate_b = nullptr;
InvSlotHandleLButtonCoreLateBranchDispatchFn
    g_original_invslot_handle_lbutton_core_late_branch_dispatch = nullptr;
InvSlotHandleLButtonCoreLateSlot17GateFn g_original_invslot_handle_lbutton_core_late_slot17_gate =
    nullptr;
InvSlotHandleLButtonCoreLateSlot17ApplyFn
    g_original_invslot_handle_lbutton_core_late_slot17_apply = nullptr;
MoveItemOwnerPrimarySetterFn g_original_move_item_owner_primary_setter = nullptr;
MoveItemValidationGateFn g_original_move_item_validation_gate = nullptr;
MoveItemCtorFn g_original_move_item_ctor = nullptr;
MoveItemSlot21LookupFn g_original_move_item_slot21_lookup = nullptr;
MoveItemDescriptorBuildFn g_original_move_item_descriptor_build = nullptr;
MoveItemSlotPopulateFn g_original_move_item_slot_populate = nullptr;
SpellbookDispatcherFn g_original_spellbook_dispatcher = nullptr;
StartSpellScribePathFn g_original_start_spell_scribe_path = nullptr;
StartSpellScribePrecheckModeGetterFn g_original_start_spell_scribe_precheck_mode_getter =
    nullptr;
StartSpellScribePrecheckGateFn g_original_start_spell_scribe_precheck_gate = nullptr;
StartSpellScribePrecheckLookupFn g_original_start_spell_scribe_precheck_lookup = nullptr;
StartSpellScribePrecheckFastAcceptFn g_original_start_spell_scribe_precheck_fast_accept =
    nullptr;
StartSpellScribePrecheckClassResolverFn g_original_start_spell_scribe_precheck_class_resolver =
    nullptr;
StartSpellScribePrecheckAssignedMaskGetterFn
    g_original_start_spell_scribe_precheck_assigned_mask_getter = nullptr;
StartSpellScribePrecheckRule4462c0Fn g_original_start_spell_scribe_precheck_rule_4462c0 =
    nullptr;
StartSpellScribePrecheckRule446190Fn g_original_start_spell_scribe_precheck_rule_446190 =
    nullptr;
StartSpellScribePrecheckRule446200Fn g_original_start_spell_scribe_precheck_rule_446200 =
    nullptr;
StartSpellScribePrecheckRule446380Fn g_original_start_spell_scribe_precheck_rule_446380 =
    nullptr;
CanStartMemmingFn g_original_can_start_memming = nullptr;
SpellbookMemorizeSendPathFn g_original_spellbook_memorize_send_path = nullptr;
StartSpellMemorizationPathFn g_original_start_spell_memorization_path = nullptr;
MemorizeSendPacketWrapperFn g_original_memorize_send_packet_wrapper = nullptr;
GetClassDescFn g_original_get_class_desc = nullptr;
GetDeityDescFn g_original_get_deity_desc = nullptr;
GetClassThreeLetterCodeFn g_original_get_class_three_letter_code = nullptr;
InventorySummaryRefreshCandidateAFn g_original_inventory_summary_refresh_candidate_a = nullptr;
InventorySummaryRefreshCandidateBFn g_original_inventory_summary_refresh_candidate_b = nullptr;
InventorySummaryRefreshCandidateCFn g_original_inventory_summary_refresh_candidate_c = nullptr;
InventorySummaryRefreshCandidateDFn g_original_inventory_summary_refresh_candidate_d = nullptr;
CharacterListWndUpdateListFn g_original_character_list_update_list = nullptr;
CharSelectClassNameFuncFn g_original_char_select_class_name_func = nullptr;
WhoClassNameFn g_original_who_class_name = nullptr;
CXStrAssignFn g_original_cxstr_assign = nullptr;
CXWndSetWindowTextAFn g_original_cxwnd_set_window_text_a = nullptr;
ProgressionSelectionClassLookupFn g_original_progression_selection_class_lookup = nullptr;
WhoClassNameClassLookupFn g_original_who_class_name_class_lookup = nullptr;
std::uint64_t g_get_spell_level_needed_trace_count = 0;
std::uint64_t g_can_start_memming_trace_count = 0;
std::uint64_t g_memorize_send_trace_count = 0;
std::uint64_t g_scroll_scribe_event_count = 0;
std::uint64_t g_ui_class_helper_trace_count = 0;
std::uint64_t g_inventory_class_title_trace_count = 0;
std::uint64_t g_inventory_on_process_frame_trace_count = 0;
std::uint64_t g_item_display_refresh_worker_entry_trace_count = 0;
std::uint64_t g_character_list_update_list_trace_count = 0;
std::uint64_t g_character_list_forced_refresh_count = 0;
std::uint64_t g_char_select_native_class_substitution_trace_count = 0;
std::uint64_t g_progression_selection_trace_count = 0;
std::uint64_t g_char_select_class_name_func_trace_count = 0;
std::uint64_t g_char_select_late_full_name_trace_count = 0;
std::uint64_t g_skill_visibility_override_count = 0;
constexpr std::size_t kUiClassHelperCallerCatalogCapacity = 64;
constexpr std::size_t kUiClassProducerCandidateCatalogCapacity = 24;
std::array<std::uint32_t, kUiClassHelperCallerCatalogCapacity>
    g_get_class_desc_seen_caller_rvas = {};
std::size_t g_get_class_desc_seen_caller_rva_count = 0;
std::array<std::uint32_t, kUiClassHelperCallerCatalogCapacity>
    g_get_class_three_letter_seen_caller_rvas = {};
std::size_t g_get_class_three_letter_seen_caller_rva_count = 0;
std::array<std::uint32_t, kUiClassProducerCandidateCatalogCapacity>
    g_ui_class_producer_candidate_seen_caller_rvas = {};
std::size_t g_ui_class_producer_candidate_seen_caller_rva_count = 0;
std::atomic<std::uint32_t> g_inventory_class_display_correlation_remaining = 0;
std::atomic<std::uint64_t> g_inventory_class_display_correlation_activation = 0;
std::atomic<std::uint32_t> g_inventory_class_display_correlation_notification_code = 0;
std::atomic<std::uintptr_t>
    g_inventory_class_display_correlation_notification_caller_return = 0;
std::atomic<std::uint32_t> g_inventory_class_display_correlation_notification_caller_rva = 0;
std::atomic<std::uintptr_t> g_inventory_class_display_correlation_sender_window = 0;
std::atomic<std::uintptr_t> g_inventory_class_display_correlation_payload_like = 0;
std::atomic<std::uintptr_t> g_character_list_window_pointer = 0;
std::atomic<std::uintptr_t> g_character_list_last_forced_refresh_window = 0;
std::atomic<std::uint32_t> g_character_list_last_forced_refresh_mask = 0;
std::atomic<std::uint32_t> g_char_select_native_class_substitution_remaining = 0;
std::atomic<std::uint64_t> g_char_select_native_class_substitution_activation = 0;
std::atomic<bool> g_multiclass_cache_persist_pending = false;
std::atomic<std::uint32_t> g_multiclass_cache_persist_pending_mask = 0;
std::atomic<std::uint32_t> g_item_display_class_display_correlation_remaining = 0;
std::atomic<std::uint64_t> g_item_display_class_display_correlation_activation = 0;
std::atomic<std::uintptr_t> g_item_display_class_display_correlation_refresh_caller_return = 0;
std::atomic<std::uint32_t> g_item_display_class_display_correlation_refresh_caller_rva = 0;
std::atomic<std::uintptr_t> g_item_display_class_display_correlation_window_this = 0;
std::atomic<std::uintptr_t> g_item_display_class_display_correlation_arg1_like = 0;
std::atomic<std::uintptr_t> g_item_display_class_display_correlation_arg2_like = 0;
std::uint32_t g_memorize_send_pending_correlation_id = 0;
std::uint32_t g_memorize_send_correlation_count = 0;
std::uint32_t g_memorize_send_pending_wrapper_sends = 0;
void* g_memorize_send_pending_window = nullptr;
std::uint32_t g_spellbook_scribe_pending_correlation_id = 0;
std::uint32_t g_spellbook_scribe_correlation_count = 0;
std::uint32_t g_spellbook_scribe_pending_wrapper_sends = 0;
std::uint32_t g_spellbook_ui_active_correlation_id = 0;
std::uint32_t g_spellbook_ui_correlation_count = 0;
std::uint32_t g_scroll_scribe_active_correlation_id = 0;
std::uintptr_t g_spellbook_dispatcher_address = 0;
std::uintptr_t g_start_spell_scribe_path_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_mode_getter_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_gate_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_lookup_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_fast_accept_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_class_resolver_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_assigned_mask_getter_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_rule_4462c0_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_rule_446190_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_rule_446200_address = 0;
std::uintptr_t g_start_spell_scribe_precheck_rule_446380_address = 0;
std::uintptr_t g_spellbook_memorize_send_path_address = 0;
std::uintptr_t g_start_spell_memorization_path_address = 0;
std::uintptr_t g_memorize_send_packet_wrapper_address = 0;
std::uintptr_t g_inv_slot_mgr_move_item_address = 0;
std::uintptr_t g_character_zone_client_has_skill_address = 0;
CharacterZoneClientHasSkillFn g_character_zone_client_has_skill = nullptr;
bool g_scroll_scribe_active_logging = false;
StartSpellScribePrecheckClassMaskSnapshot g_start_spell_scribe_precheck_class_mask_snapshot = {};
std::wstring g_handle_rbutton_up_evidence_source = L"unknown";
std::wstring g_is_class_usable_predicate_evidence_source = L"unknown";
bool g_multiclass_spell_usability_enabled = false;
bool g_multiclass_item_usability_enabled = false;
bool g_multiclass_skill_visibility_enabled = false;
bool g_multiclass_ui_display_enabled = false;
thread_local bool g_character_list_manual_refresh_in_progress = false;
std::uint64_t g_is_class_usable_predicate_override_count = 0;
std::uint64_t g_invslot_handle_lbutton_core_late_branch_prep_override_count = 0;
std::uint64_t g_invslot_handle_lbutton_core_late_branch_gate_b_override_count = 0;
std::uint64_t g_auto_equip_class_gate_override_count = 0;
std::uint64_t g_guild_trainer_class_lookup_override_count = 0;
std::uint64_t g_guild_trainer_class_lookup_call_count = 0;
std::uint8_t g_pending_guild_trainer_session_class_id = 0;
std::uint64_t g_who_class_name_entry_trace_count = 0;
thread_local const void* g_active_who_class_name_subject = nullptr;
std::uint64_t g_ui_class_display_trace_count = 0;
std::uint32_t g_active_equip_nested_validation_id = 0;
std::uint32_t g_equip_nested_validation_count = 0;
std::uintptr_t g_invslot_handle_lbutton_core_last_late_lookup_item_pointer = 0;
std::array<std::uint8_t, kGuildTrainerClassLookupResolvedClassOffset + 1>
    g_guild_trainer_class_lookup_override_record = {};

struct InventoryClassDisplayCorrelationWindow {
    bool active = false;
    std::uint64_t activation = 0;
    std::uint32_t notification_code = 0;
    std::uintptr_t notification_caller_return = 0;
    std::uint32_t notification_caller_rva = 0;
    std::uintptr_t sender_window = 0;
    std::uintptr_t payload_like = 0;
    std::uint32_t remaining_before = 0;
    std::uint32_t remaining_after = 0;
};

struct ItemDisplayClassDisplayCorrelationWindow {
    bool active = false;
    std::uint64_t activation = 0;
    std::uintptr_t refresh_caller_return = 0;
    std::uint32_t refresh_caller_rva = 0;
    std::uintptr_t window_this = 0;
    std::uintptr_t arg1_like = 0;
    std::uintptr_t arg2_like = 0;
    std::uint32_t remaining_before = 0;
    std::uint32_t remaining_after = 0;
};

struct OffhandWeaponPolicySnapshot {
    std::uintptr_t item_data_like = 0;
    std::uint32_t item_class_mask = 0;
    std::uint32_t equip_slots = 0;
    std::uint32_t local_player_level = 0;
    std::uint32_t dual_wield_required_level = 0;
    std::uint8_t item_class = 0xff;
    std::uint8_t holding_animation = 0;
    std::uint8_t dual_wield_entitlement_class_id = 0;
    bool item_class_mask_copied = false;
    bool equip_slots_copied = false;
    bool local_player_level_copied = false;
    bool item_class_copied = false;
    bool holding_animation_copied = false;
    bool is_weapon = false;
    bool can_wear_secondary = false;
    bool item_matches_assigned_class = false;
    bool dual_wield_skill_checked = false;
    bool has_dual_wield_skill = false;
    bool authoritative_dual_wield_class_present = false;
    bool dual_wield_entitlement_fallback_used = false;
    bool dual_wield_matrix_matched = false;
    bool has_dual_wield_entitlement = false;
    bool primary_blocks_secondary_weapon = false;
    bool eligible = false;
};

struct GuildTrainerClassResolution {
    bool module_base_available = false;
    bool current_frame_copied = false;
    bool caller_ebp_copied = false;
    bool active_trainer_global_copied = false;
    bool active_trainer_present = false;
    bool caller_context_used = false;
    bool caller_class_copied = false;
    bool displayed_class_copied = false;
    bool fallback_class_copied = false;
    bool normalized_class_copied = false;
    std::uintptr_t current_frame = 0;
    std::uintptr_t caller_ebp = 0;
    std::uintptr_t trainer_spawn_like = 0;
    unsigned int trainer_class_id = 0;
    unsigned int normalized_class_id = 0;
};

struct GuildTrainerSessionClassOverrideState {
    bool active = false;
    std::uint8_t active_class_id = 0;
    std::uintptr_t local_player = 0;
    bool displayed_original_copied = false;
    std::uint8_t displayed_original_class_id = 0;
    std::uintptr_t profile = 0;
    bool profile_original_copied = false;
    std::uint32_t profile_original_class_id = 0;
};

GuildTrainerSessionClassOverrideState g_guild_trainer_session_class_override = {};

std::uintptr_t GetHostModuleBase() noexcept;

std::wstring HexPtr(std::uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::uint32_t ReadClientItemClassMask(const void* item_like) noexcept {
    if (item_like == nullptr) {
        return 0;
    }

    std::uint32_t class_mask = 0;
    std::memcpy(
        &class_mask,
        static_cast<const std::uint8_t*>(item_like) + kClientItemClassMaskOffset,
        sizeof(class_mask));
    return class_mask;
}

std::wstring Hex32(std::uint32_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring Hex64(std::uint64_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

bool ShouldLogUiClassDisplayTrace(std::uint64_t count) noexcept {
    return count <= 20 || (count % 50) == 0;
}

const wchar_t* ClassDisplayStyleName(
    monomyth::multiclass_identity::ClassDisplayStyle style) noexcept {
    switch (style) {
        case monomyth::multiclass_identity::ClassDisplayStyle::kFullName:
            return L"full_name";
        case monomyth::multiclass_identity::ClassDisplayStyle::kThreeLetterCode:
            return L"three_letter_code";
    }

    return L"unknown";
}

void LogUiClassDisplayTrace(
    const wchar_t* surface,
    const wchar_t* status,
    monomyth::multiclass_identity::ClassDisplayStyle style,
    const void* subject,
    const void* local_player,
    bool subject_matches_local,
    bool primary_class_copied,
    std::uint8_t primary_class_id,
    const monomyth::server_auth_stats::Snapshot& snapshot,
    const char* formatted,
    const wchar_t* reason) {
    const std::uint64_t count = ++g_ui_class_display_trace_count;
    if (!ShouldLogUiClassDisplayTrace(count)) {
        return;
    }

    std::wstring message = L"UiClassDisplayTrace count=";
    message += std::to_wstring(count);
    message += L" surface=";
    message += (surface == nullptr ? L"unknown" : surface);
    message += L" status=";
    message += (status == nullptr ? L"unknown" : status);
    message += L" style=";
    message += ClassDisplayStyleName(style);
    message += L" subject=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(subject));
    message += L" local_player=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(local_player));
    message += L" subject_matches_local=";
    message += (subject_matches_local ? L"true" : L"false");
    message += L" primary_class_status=";
    message += (primary_class_copied ? L"copied" : L"unavailable");
    if (primary_class_copied) {
        message += L" primary_class_id=";
        message += std::to_wstring(primary_class_id);
    }
    message += L" assigned_mask_status=";
    message += (snapshot.has_classes_bitmask ? L"present" : L"missing");
    message += L" assigned_mask=";
    message += Hex32(snapshot.has_classes_bitmask ? snapshot.classes_bitmask : 0);
    if (formatted != nullptr && formatted[0] != '\0') {
        std::wstring formatted_w;
        formatted_w.reserve(std::strlen(formatted));
        for (const char* it = formatted; *it != '\0'; ++it) {
            formatted_w.push_back(static_cast<unsigned char>(*it));
        }
        message += L" formatted=\"";
        message += formatted_w;
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    monomyth::logger::Log(message);
}

std::uintptr_t GetCallerReturnAddress() noexcept {
#if defined(_MSC_VER)
    return reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#elif defined(__clang__) || defined(__GNUC__)
    return reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#else
    return 0;
#endif
}

std::wstring WidenAsciiLossy(std::string_view value) {
    std::wstring widened;
    widened.reserve(value.size());
    for (const unsigned char ch : value) {
        widened.push_back(static_cast<wchar_t>(ch));
    }
    return widened;
}

std::string TrimAsciiWhitespace(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

bool ShouldLogUiClassHelperTrace(std::uint64_t count) noexcept {
    return count <= 40 || (count % 100) == 0;
}

bool ShouldLogWhoClassNameTrace(std::uint64_t count) noexcept {
    return count <= 20 || (count % 50) == 0;
}

bool ShouldLogItemDisplayRefreshWorkerEntryTrace(std::uint64_t count) noexcept {
    return count <= 20 || (count % 50) == 0;
}

bool ShouldLogInventoryOnProcessFrameTrace(std::uint64_t count) noexcept {
    return count <= 20 || (count % 50) == 0;
}

std::wstring FormatAssignedMask(
    const monomyth::server_auth_stats::Snapshot& snapshot);

bool TryReadLocalProfileClassId(std::uint8_t* class_id) noexcept;
bool TryReadLocalPlayerDisplayedClassId(std::uint8_t* class_id) noexcept;
const char* BuildPreferredLocalPlayerClassDisplayAscii(
    unsigned int requested_class_id,
    monomyth::multiclass_identity::ClassDisplayStyle style,
    const wchar_t* surface) noexcept;

bool ShouldLogProgressionSelectionTrace(std::uint64_t count) noexcept {
    return count <= 20 || (count % 50) == 0;
}

bool ShouldLogCharSelectClassNameFuncTrace(std::uint64_t count) noexcept {
    return count <= 20 || (count % 50) == 0;
}

bool ShouldLogCharSelectLateFullNameTrace(std::uint64_t count) noexcept {
    return count <= 20 || (count % 50) == 0;
}

const wchar_t* DescribeKnownUiClassProducerCandidate(
    const wchar_t* helper,
    std::uint32_t caller_rva) noexcept {
    if (helper == nullptr || caller_rva == 0) {
        return nullptr;
    }

    if (std::wcscmp(helper, L"GetClassDesc") == 0) {
        switch (caller_rva) {
            case 0x0018e554:
                return L"known_early_full_name_candidate_pre_auth";
            case 0x002781a6:
                return L"known_local_self_three_letter_surface";
            case 0x002843ff:
                return L"thj_abbreviated_branch_anchor";
            case 0x00312c55:
                return L"static_scan_ui_builder_candidate_a";
            case 0x00324d89:
                return L"static_scan_inventory_or_charselect_candidate_b";
            case 0x003252b0:
                return L"static_scan_inventory_or_charselect_candidate_c";
            default:
                return nullptr;
        }
    }

    if (std::wcscmp(helper, L"GetClassThreeLetterCode") == 0) {
        switch (caller_rva) {
            case 0x0027997b:
                return L"static_scan_full_name_ui_candidate_a";
            case 0x00279f23:
                return L"static_scan_full_name_ui_candidate_b";
            case 0x0027a2e7:
                return L"static_scan_full_name_ui_candidate_c";
            default:
                return nullptr;
        }
    }

    return nullptr;
}

bool ShouldCatalogUiClassProducerCandidate(
    std::uint32_t caller_rva) noexcept {
    if (caller_rva == 0) {
        return false;
    }

    for (std::size_t i = 0; i < g_ui_class_producer_candidate_seen_caller_rva_count; ++i) {
        if (g_ui_class_producer_candidate_seen_caller_rvas[i] == caller_rva) {
            return false;
        }
    }

    if (g_ui_class_producer_candidate_seen_caller_rva_count <
        g_ui_class_producer_candidate_seen_caller_rvas.size()) {
        g_ui_class_producer_candidate_seen_caller_rvas
            [g_ui_class_producer_candidate_seen_caller_rva_count] = caller_rva;
        ++g_ui_class_producer_candidate_seen_caller_rva_count;
        return true;
    }

    return false;
}

bool ShouldCatalogUiClassHelperCaller(
    const wchar_t* helper,
    std::uint32_t caller_rva) noexcept {
    if (helper == nullptr || caller_rva == 0) {
        return false;
    }

    std::array<std::uint32_t, kUiClassHelperCallerCatalogCapacity>* seen = nullptr;
    std::size_t* seen_count = nullptr;
    if (std::wcscmp(helper, L"GetClassDesc") == 0) {
        seen = &g_get_class_desc_seen_caller_rvas;
        seen_count = &g_get_class_desc_seen_caller_rva_count;
    } else if (std::wcscmp(helper, L"GetClassThreeLetterCode") == 0) {
        seen = &g_get_class_three_letter_seen_caller_rvas;
        seen_count = &g_get_class_three_letter_seen_caller_rva_count;
    } else {
        return false;
    }

    for (std::size_t i = 0; i < *seen_count; ++i) {
        if ((*seen)[i] == caller_rva) {
            return false;
        }
    }

    if (*seen_count < seen->size()) {
        (*seen)[*seen_count] = caller_rva;
        ++(*seen_count);
        return true;
    }

    return false;
}

void MaybeLogUiClassProducerCandidateTrace(
    const wchar_t* helper,
    std::uintptr_t caller_return_address,
    unsigned int requested_class_id,
    bool local_class_copied,
    std::uint8_t local_class_id,
    const monomyth::server_auth_stats::Snapshot& snapshot,
    bool override_applied,
    const char* formatted,
    const wchar_t* reason) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const wchar_t* const candidate =
        DescribeKnownUiClassProducerCandidate(helper, caller_rva);
    if (candidate == nullptr || !ShouldCatalogUiClassProducerCandidate(caller_rva)) {
        return;
    }

    std::wstring message = L"UiClassProducerCandidateTrace helper=";
    message += (helper == nullptr ? L"unknown" : helper);
    message += L" candidate=\"";
    message += candidate;
    message += L"\" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" requested_class_id=";
    message += std::to_wstring(requested_class_id);
    message += L" local_class_copied=";
    message += (local_class_copied ? L"true" : L"false");
    message += L" local_class_id=";
    message += std::to_wstring(local_class_id);
    message += L" has_assigned_mask=";
    message += (snapshot.has_classes_bitmask ? L"true" : L"false");
    message += L" assigned_mask=";
    message += Hex32(snapshot.has_classes_bitmask ? snapshot.classes_bitmask : 0);
    message += L" override_applied=";
    message += (override_applied ? L"true" : L"false");
    if (formatted != nullptr && formatted[0] != '\0') {
        message += L" formatted=\"";
        message += WidenAsciiLossy(formatted);
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    monomyth::logger::Log(message);
}

void MaybeLogUiClassHelperCallerCatalogTrace(
    const wchar_t* helper,
    std::uintptr_t caller_return_address,
    unsigned int requested_class_id,
    bool local_class_copied,
    std::uint8_t local_class_id,
    const monomyth::server_auth_stats::Snapshot& snapshot,
    bool override_applied,
    const char* formatted,
    const wchar_t* reason) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const bool local_player_relevant =
        local_class_copied && requested_class_id == local_class_id;
    if (!snapshot.has_classes_bitmask && !override_applied && !local_player_relevant) {
        return;
    }
    if (!ShouldCatalogUiClassHelperCaller(helper, caller_rva)) {
        return;
    }

    std::wstring message = L"UiClassHelperCallerCatalogTrace helper=";
    message += (helper == nullptr ? L"unknown" : helper);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" requested_class_id=";
    message += std::to_wstring(requested_class_id);
    message += L" local_class_copied=";
    message += (local_class_copied ? L"true" : L"false");
    message += L" local_class_id=";
    message += std::to_wstring(local_class_id);
    message += L" has_assigned_mask=";
    message += (snapshot.has_classes_bitmask ? L"true" : L"false");
    message += L" assigned_mask=";
    message += Hex32(snapshot.has_classes_bitmask ? snapshot.classes_bitmask : 0);
    message += L" override_applied=";
    message += (override_applied ? L"true" : L"false");
    if (formatted != nullptr && formatted[0] != '\0') {
        message += L" formatted=\"";
        message += WidenAsciiLossy(formatted);
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    monomyth::logger::Log(message);
}

bool TryResolveSemanticClassDisplayStyleForCaller(
    std::uintptr_t caller_return_address,
    monomyth::multiclass_identity::ClassDisplayStyle* style,
    const wchar_t** reason) noexcept {
    if (style == nullptr) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0 || caller_return_address < module_base) {
        return false;
    }

    const std::uint32_t caller_rva =
        static_cast<std::uint32_t>(caller_return_address - module_base);
    switch (caller_rva) {
        case 0x002781a6:
        case 0x002843ff:
            *style = monomyth::multiclass_identity::ClassDisplayStyle::kThreeLetterCode;
            if (reason != nullptr) {
                *reason = L"known_local_self_three_letter_caller_rva";
            }
            return true;
        default:
            break;
    }

    return false;
}

bool ShouldBypassLocalPlayerClassOverrideForCaller(
    std::uintptr_t caller_return_address) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0 || caller_return_address < module_base) {
        return false;
    }

    const std::uint32_t caller_rva =
        static_cast<std::uint32_t>(caller_return_address - module_base);
    switch (caller_rva) {
        case 0x0018e554:
            return true;
        default:
            return false;
    }
}

const char* TryBuildCachedCharSelectFullNameDisplayAscii(
    unsigned int requested_class_id,
    const wchar_t* surface) noexcept {
    if (!monomyth::multiclass_identity::IsPlayableClassId(requested_class_id)) {
        return nullptr;
    }

    std::string server_name;
    if (!monomyth::multiclass_cache::TryGetCurrentServerName(&server_name)) {
        return nullptr;
    }

    std::uint32_t cached_mask = 0;
    if (!monomyth::multiclass_cache::TryLookupUniqueClassMaskForNativeClassId(
            requested_class_id,
            server_name.c_str(),
            &cached_mask) ||
        !monomyth::multiclass_identity::IsPlayableClassMask(cached_mask) ||
        !monomyth::multiclass_identity::HasClass(cached_mask, requested_class_id)) {
        return nullptr;
    }

    static thread_local std::string buffer;
    buffer = monomyth::multiclass_identity::FormatClassDisplayAscii(
        requested_class_id,
        true,
        cached_mask,
        monomyth::multiclass_identity::ClassDisplayStyle::kFullName);
    if (buffer.empty()) {
        return nullptr;
    }

    std::wstring message = L"CharSelectCachedClassHelperTrace requested_class_id=";
    message += std::to_wstring(requested_class_id);
    message += L" cached_mask=";
    message += Hex32(cached_mask);
    message += L" server=\"";
    message += WidenAsciiLossy(server_name);
    message += L"\"";
    message += L" formatted=\"";
    message += WidenAsciiLossy(buffer);
    message += L"\" surface=\"";
    message += (surface == nullptr ? L"" : surface);
    message += L"\"";
    monomyth::logger::Log(message);
    return buffer.c_str();
}

void LogUiClassHelperTrace(
    const wchar_t* helper,
    std::uintptr_t caller_return_address,
    unsigned int requested_class_id,
    bool local_class_copied,
    std::uint8_t local_class_id,
    const monomyth::server_auth_stats::Snapshot& snapshot,
    bool override_applied,
    const char* formatted,
    const wchar_t* reason) {
    MaybeLogUiClassHelperCallerCatalogTrace(
        helper,
        caller_return_address,
        requested_class_id,
        local_class_copied,
        local_class_id,
        snapshot,
        override_applied,
        formatted,
        reason);
    MaybeLogUiClassProducerCandidateTrace(
        helper,
        caller_return_address,
        requested_class_id,
        local_class_copied,
        local_class_id,
        snapshot,
        override_applied,
        formatted,
        reason);

    const std::uint64_t count = ++g_ui_class_helper_trace_count;
    if (!ShouldLogUiClassHelperTrace(count)) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;

    std::wstring message = L"UiClassHelperTrace count=";
    message += std::to_wstring(count);
    message += L" helper=";
    message += (helper == nullptr ? L"unknown" : helper);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" requested_class_id=";
    message += std::to_wstring(requested_class_id);
    message += L" local_class_copied=";
    message += (local_class_copied ? L"true" : L"false");
    message += L" local_class_id=";
    message += std::to_wstring(local_class_id);
    message += L" has_assigned_mask=";
    message += (snapshot.has_classes_bitmask ? L"true" : L"false");
    message += L" assigned_mask=";
    message += Hex32(snapshot.has_classes_bitmask ? snapshot.classes_bitmask : 0);
    message += L" override_applied=";
    message += (override_applied ? L"true" : L"false");
    if (formatted != nullptr && formatted[0] != '\0') {
        message += L" formatted=\"";
        message += WidenAsciiLossy(formatted);
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    monomyth::logger::Log(message);
}

void LogWhoClassNameEntryTrace(
    void* this_context,
    void* subject) noexcept {
    const std::uint64_t count = ++g_who_class_name_entry_trace_count;
    if (!ShouldLogWhoClassNameTrace(count)) {
        return;
    }

    std::wstring message = L"WhoClassNameEntryTrace count=";
    message += std::to_wstring(count);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" subject=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(subject));
    monomyth::logger::Log(message);
}

void LogWhoAllClassDisplayCorrelationTrace(
    const wchar_t* surface,
    const wchar_t* input_name,
    std::uintptr_t caller_return_address,
    std::uint32_t input_value,
    const monomyth::packet_observer::WhoAllClassDisplayCorrelationWindow& correlation,
    const void* subject,
    bool override_applied,
    const wchar_t* reason,
    const char* formatted) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;

    std::wstring message = L"WhoAllClassDisplayTrace";
    message += L" activation=";
    message += std::to_wstring(correlation.activation);
    message += L" receive_sequence=";
    message += std::to_wstring(correlation.receive_sequence);
    message += L" response_index=";
    message += std::to_wstring(correlation.response_index);
    message += L" remaining_before=";
    message += std::to_wstring(correlation.remaining_before);
    message += L" remaining_after=";
    message += std::to_wstring(correlation.remaining_after);
    message += L" surface=";
    message += (surface == nullptr ? L"unknown" : surface);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" ";
    message += (input_name == nullptr ? L"input" : input_name);
    message += L"=";
    message += Hex32(input_value);
    message += L" subject=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(subject));
    message += L" override_applied=";
    message += (override_applied ? L"true" : L"false");
    if (formatted != nullptr && formatted[0] != '\0') {
        message += L" formatted=\"";
        message += WidenAsciiLossy(formatted);
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    monomyth::logger::Log(message);
}

void ArmInventoryClassDisplayCorrelation(
    std::uintptr_t caller_return_address,
    void* sender_window,
    std::uint32_t notification_code,
    void* payload_like) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const std::uint64_t activation =
        g_inventory_class_display_correlation_activation.fetch_add(1) + 1;
    g_inventory_class_display_correlation_notification_code.store(notification_code);
    g_inventory_class_display_correlation_notification_caller_return.store(
        caller_return_address);
    g_inventory_class_display_correlation_notification_caller_rva.store(caller_rva);
    g_inventory_class_display_correlation_sender_window.store(
        reinterpret_cast<std::uintptr_t>(sender_window));
    g_inventory_class_display_correlation_payload_like.store(
        reinterpret_cast<std::uintptr_t>(payload_like));
    g_inventory_class_display_correlation_remaining.store(
        kInventoryClassDisplayCorrelationBudget);

    std::wstring message = L"InventoryClassDisplayCorrelationWindow";
    message += L" activation=";
    message += std::to_wstring(activation);
    message += L" notification_code=";
    message += Hex32(notification_code);
    message += L" notification_caller_return=";
    message += HexPtr(caller_return_address);
    message += L" notification_caller_rva=";
    message += Hex32(caller_rva);
    message += L" sender_window=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(sender_window));
    message += L" payload_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(payload_like));
    message += L" budget=";
    message += std::to_wstring(kInventoryClassDisplayCorrelationBudget);
    monomyth::logger::Log(message);
}

bool TryConsumeInventoryClassDisplayCorrelation(
    InventoryClassDisplayCorrelationWindow* window) noexcept {
    if (window == nullptr) {
        return false;
    }

    *window = {};

    std::uint32_t current = g_inventory_class_display_correlation_remaining.load();
    while (current != 0) {
        if (g_inventory_class_display_correlation_remaining.compare_exchange_weak(
                current,
                current - 1)) {
            window->active = true;
            window->activation = g_inventory_class_display_correlation_activation.load();
            window->notification_code =
                g_inventory_class_display_correlation_notification_code.load();
            window->notification_caller_return =
                g_inventory_class_display_correlation_notification_caller_return.load();
            window->notification_caller_rva =
                g_inventory_class_display_correlation_notification_caller_rva.load();
            window->sender_window = g_inventory_class_display_correlation_sender_window.load();
            window->payload_like = g_inventory_class_display_correlation_payload_like.load();
            window->remaining_before = current;
            window->remaining_after = current - 1;
            return true;
        }
    }

    return false;
}

void LogInventoryClassDisplayCorrelationTrace(
    const wchar_t* surface,
    const wchar_t* input_name,
    std::uintptr_t caller_return_address,
    std::uint32_t input_value,
    const InventoryClassDisplayCorrelationWindow& correlation,
    const void* subject,
    bool override_applied,
    const wchar_t* reason,
    const char* formatted) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;

    std::wstring message = L"InventoryClassDisplayTrace";
    message += L" activation=";
    message += std::to_wstring(correlation.activation);
    message += L" remaining_before=";
    message += std::to_wstring(correlation.remaining_before);
    message += L" remaining_after=";
    message += std::to_wstring(correlation.remaining_after);
    message += L" notification_code=";
    message += Hex32(correlation.notification_code);
    message += L" notification_caller_return=";
    message += HexPtr(correlation.notification_caller_return);
    message += L" notification_caller_rva=";
    message += Hex32(correlation.notification_caller_rva);
    message += L" sender_window=";
    message += HexPtr(correlation.sender_window);
    message += L" payload_like=";
    message += HexPtr(correlation.payload_like);
    message += L" surface=";
    message += (surface == nullptr ? L"unknown" : surface);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" ";
    message += (input_name == nullptr ? L"input" : input_name);
    message += L"=";
    message += Hex32(input_value);
    message += L" subject=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(subject));
    message += L" override_applied=";
    message += (override_applied ? L"true" : L"false");
    if (formatted != nullptr && formatted[0] != '\0') {
        message += L" formatted=\"";
        message += WidenAsciiLossy(formatted);
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    monomyth::logger::Log(message);
}

void LogInventorySummaryCandidateEntryTrace(
    const wchar_t* candidate,
    std::uint32_t target_rva,
    std::uintptr_t caller_return_address,
    void* this_context,
    void* arg_like) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;

    std::wstring message = L"InventorySummaryCandidateEntryTrace candidate=";
    message += (candidate == nullptr ? L"unknown" : candidate);
    message += L" target_rva=";
    message += Hex32(target_rva);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" arg_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(arg_like));
    monomyth::logger::Log(message);
}

void LogInventoryOnProcessFrameTrace(
    std::uint64_t count,
    std::uintptr_t caller_return_address,
    void* this_context,
    std::uint32_t state_2f4,
    bool state_2f4_copied,
    std::uint32_t last_refresh_tick_294,
    bool last_refresh_tick_294_copied,
    std::uint32_t ui_tick_154,
    bool ui_tick_154_copied,
    bool refresh_ready,
    std::uintptr_t field_248,
    bool field_248_copied,
    std::uintptr_t field_24c,
    bool field_24c_copied,
    std::uintptr_t field_250,
    bool field_250_copied,
    std::uintptr_t field_254,
    bool field_254_copied,
    std::uintptr_t field_25c,
    bool field_25c_copied) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;

    std::wstring message = L"InventoryOnProcessFrameTrace count=";
    message += std::to_wstring(count);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" state_2f4_status=";
    message += state_2f4_copied ? L"copied" : L"unavailable";
    if (state_2f4_copied) {
        message += L" state_2f4=";
        message += Hex32(state_2f4);
    }
    message += L" last_refresh_tick_294_status=";
    message += last_refresh_tick_294_copied ? L"copied" : L"unavailable";
    if (last_refresh_tick_294_copied) {
        message += L" last_refresh_tick_294=";
        message += Hex32(last_refresh_tick_294);
    }
    message += L" ui_tick_154_status=";
    message += ui_tick_154_copied ? L"copied" : L"unavailable";
    if (ui_tick_154_copied) {
        message += L" ui_tick_154=";
        message += Hex32(ui_tick_154);
    }
    message += L" refresh_ready=";
    message += refresh_ready ? L"true" : L"false";
    message += L" field_248=";
    if (field_248_copied) {
        message += HexPtr(field_248);
    } else {
        message += L"unavailable";
    }
    message += L" field_24c=";
    if (field_24c_copied) {
        message += HexPtr(field_24c);
    } else {
        message += L"unavailable";
    }
    message += L" field_250=";
    if (field_250_copied) {
        message += HexPtr(field_250);
    } else {
        message += L"unavailable";
    }
    message += L" field_254=";
    if (field_254_copied) {
        message += HexPtr(field_254);
    } else {
        message += L"unavailable";
    }
    message += L" field_25c=";
    if (field_25c_copied) {
        message += HexPtr(field_25c);
    } else {
        message += L"unavailable";
    }
    monomyth::logger::Log(message);
}

void LogCharSelectClassNameFuncTrace(
    const wchar_t* phase,
    std::uint64_t count,
    void* this_context,
    unsigned int selected_index,
    std::uintptr_t caller_return_address,
    std::uintptr_t field_22c,
    bool field_22c_copied,
    std::uintptr_t field_248,
    bool field_248_copied,
    std::uintptr_t field_24c,
    bool field_24c_copied) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();

    std::wstring message = L"CharSelectClassNameFuncTrace count=";
    message += std::to_wstring(count);
    message += L" phase=";
    message += phase == nullptr ? L"unknown" : phase;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" selected_index=";
    message += std::to_wstring(selected_index);
    message += L" field_22c_status=";
    message += field_22c_copied ? L"copied" : L"unavailable";
    if (field_22c_copied) {
        message += L" field_22c=";
        message += HexPtr(field_22c);
    }
    message += L" field_248_status=";
    message += field_248_copied ? L"copied" : L"unavailable";
    if (field_248_copied) {
        message += L" field_248=";
        message += HexPtr(field_248);
        message += L" branch_248_active=";
        message += field_248 != 0 ? L"true" : L"false";
    }
    message += L" field_24c_status=";
    message += field_24c_copied ? L"copied" : L"unavailable";
    if (field_24c_copied) {
        message += L" field_24c=";
        message += HexPtr(field_24c);
        message += L" branch_24c_active=";
        message += field_24c != 0 ? L"true" : L"false";
    }
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogCharSelectLateFullNameTrace(
    const wchar_t* candidate,
    std::uint64_t count,
    void* this_context,
    unsigned int class_id,
    bool override_applied,
    const char* formatted,
    const wchar_t* reason) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::uint8_t local_class_id = 0;
    const bool local_class_copied = TryReadLocalPlayerDisplayedClassId(&local_class_id);

    std::wstring message = L"CharSelectLateFullNameTrace count=";
    message += std::to_wstring(count);
    message += L" candidate=";
    message += candidate == nullptr ? L"unknown" : candidate;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" requested_class_id=";
    message += std::to_wstring(class_id);
    message += L" local_class_copied=";
    message += local_class_copied ? L"true" : L"false";
    message += L" local_class_id=";
    message += std::to_wstring(local_class_id);
    message += L" override_applied=";
    message += override_applied ? L"true" : L"false";
    if (formatted != nullptr && formatted[0] != '\0') {
        message += L" formatted=\"";
        message += WidenAsciiLossy(formatted);
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogCharacterListUpdateListTrace(
    std::uint64_t count,
    void* this_context,
    bool force_update,
    bool manual_refresh_in_progress) noexcept {
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::wstring message = L"CharacterListUpdateListTrace count=";
    message += std::to_wstring(count);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" force_update=";
    message += force_update ? L"true" : L"false";
    message += L" manual_refresh=";
    message += manual_refresh_in_progress ? L"true" : L"false";
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    monomyth::logger::Log(message);
}

bool TryInvokeCharacterListUpdateList(void* character_list_window) noexcept {
    if (character_list_window == nullptr ||
        g_original_character_list_update_list == nullptr) {
        return false;
    }

#if defined(_MSC_VER)
    __try {
        g_original_character_list_update_list(character_list_window, true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    g_original_character_list_update_list(character_list_window, true);
    return true;
#endif
}

bool TryForceCharacterListRefreshForAssignedMask(std::uint32_t assigned_mask) noexcept {
    if (!g_multiclass_ui_display_enabled || g_original_character_list_update_list == nullptr ||
        !g_character_list_update_list_detour.installed ||
        g_character_list_manual_refresh_in_progress) {
        return false;
    }

    void* const character_list_window =
        reinterpret_cast<void*>(g_character_list_window_pointer.load());
    if (character_list_window == nullptr) {
        return false;
    }

    const std::uintptr_t refreshed_window =
        g_character_list_last_forced_refresh_window.load();
    const std::uint32_t refreshed_mask = g_character_list_last_forced_refresh_mask.load();
    if (refreshed_window == reinterpret_cast<std::uintptr_t>(character_list_window) &&
        refreshed_mask == assigned_mask) {
        return false;
    }

    g_character_list_manual_refresh_in_progress = true;
    const bool call_succeeded = TryInvokeCharacterListUpdateList(character_list_window);
    g_character_list_manual_refresh_in_progress = false;

    std::wstring message = L"CharacterListForcedRefresh";
    message += L" window=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(character_list_window));
    message += L" assigned_mask=";
    message += Hex32(assigned_mask);
    message += L" success=";
    message += call_succeeded ? L"true" : L"false";
    if (call_succeeded) {
        const std::uint64_t count = ++g_character_list_forced_refresh_count;
        message += L" count=";
        message += std::to_wstring(count);
        g_character_list_last_forced_refresh_window.store(
            reinterpret_cast<std::uintptr_t>(character_list_window));
        g_character_list_last_forced_refresh_mask.store(assigned_mask);
    }
    monomyth::logger::Log(message);
    return call_succeeded;
}

void ArmCharSelectNativeClassTextSubstitutionWindow() noexcept {
    const std::uint64_t activation =
        g_char_select_native_class_substitution_activation.fetch_add(1) + 1;
    g_char_select_native_class_substitution_remaining.store(
        kCharSelectNativeClassSubstitutionBudget);
    std::wstring message = L"CharSelectNativeClassSubstitutionWindow activation=";
    message += std::to_wstring(activation);
    message += L" budget=";
    message += std::to_wstring(kCharSelectNativeClassSubstitutionBudget);
    monomyth::logger::Log(message);
}

bool TryResolveClassIdFromFullNameAscii(
    std::string_view class_name,
    std::uint8_t* class_id) noexcept {
    if (class_id == nullptr) {
        return false;
    }

    for (unsigned int current = monomyth::multiclass_identity::kFirstPlayableClassId;
         current <= monomyth::multiclass_identity::kLastPlayableClassId;
         ++current) {
        const char* token = monomyth::multiclass_identity::ClassDisplayTokenAscii(
            current,
            monomyth::multiclass_identity::ClassDisplayStyle::kFullName);
        if (token != nullptr && class_name == token) {
            *class_id = static_cast<std::uint8_t>(current);
            return true;
        }
    }

    return false;
}

bool TryBuildCachedCharSelectCaptionReplacement(
    std::string_view text,
    std::string* replacement,
    std::string* character_name,
    std::uint8_t* native_class_id,
    std::uint32_t* cached_mask) noexcept {
    if (replacement == nullptr || character_name == nullptr ||
        native_class_id == nullptr || cached_mask == nullptr) {
        return false;
    }

    replacement->clear();
    character_name->clear();
    *native_class_id = 0;
    *cached_mask = 0;

    const std::size_t open_bracket = text.rfind('[');
    const std::size_t close_bracket = text.rfind(']');
    if (open_bracket == std::string_view::npos ||
        close_bracket == std::string_view::npos ||
        close_bracket != (text.size() - 1) ||
        close_bracket <= open_bracket + 1) {
        return false;
    }

    const std::string parsed_name = TrimAsciiWhitespace(text.substr(0, open_bracket));
    if (parsed_name.empty()) {
        return false;
    }

    const std::string_view bracket_body =
        text.substr(open_bracket + 1, close_bracket - open_bracket - 1);
    std::size_t level_end = 0;
    while (level_end < bracket_body.size() &&
           bracket_body[level_end] >= '0' &&
           bracket_body[level_end] <= '9') {
        ++level_end;
    }

    if (level_end == 0 || level_end >= bracket_body.size() ||
        bracket_body[level_end] != ' ') {
        return false;
    }

    const std::string level_text(bracket_body.substr(0, level_end));
    const std::string native_class_text =
        TrimAsciiWhitespace(bracket_body.substr(level_end + 1));
    if (native_class_text.empty() ||
        !TryResolveClassIdFromFullNameAscii(native_class_text, native_class_id)) {
        return false;
    }

    std::string server_name;
    if (!monomyth::multiclass_cache::TryGetCurrentServerName(&server_name)) {
        return false;
    }

    if (!monomyth::multiclass_cache::TryLookupCharacterClassMask(
            parsed_name.c_str(),
            *native_class_id,
            server_name.c_str(),
            cached_mask) ||
        !monomyth::multiclass_identity::IsPlayableClassMask(*cached_mask) ||
        (*cached_mask & (*cached_mask - 1u)) == 0 ||
        !monomyth::multiclass_identity::HasClass(*cached_mask, *native_class_id)) {
        return false;
    }

    const std::string formatted =
        monomyth::multiclass_identity::FormatClassDisplayAscii(
            *native_class_id,
            true,
            *cached_mask,
            monomyth::multiclass_identity::ClassDisplayStyle::kThreeLetterCode);
    if (formatted.empty()) {
        return false;
    }

    *character_name = parsed_name;
    *replacement = parsed_name;
    replacement->append(" [");
    replacement->append(level_text);
    replacement->append(" ");
    replacement->append(formatted);
    replacement->append("]");
    return *replacement != text;
}

void MONOMYTH_FASTCALL CharacterListWndUpdateListHook(
    void* this_context,
    void*,
    bool force_update) noexcept {
    g_character_list_window_pointer.store(reinterpret_cast<std::uintptr_t>(this_context));
    if (g_original_character_list_update_list != nullptr) {
        g_original_character_list_update_list(this_context, force_update);
    }

    const std::uint64_t count = ++g_character_list_update_list_trace_count;
    if (count <= 8 || g_character_list_manual_refresh_in_progress) {
        LogCharacterListUpdateListTrace(
            count,
            this_context,
            force_update,
            g_character_list_manual_refresh_in_progress);
    }
}

void ArmInventoryClassDisplayCorrelationFromCandidate(
    const wchar_t* candidate,
    std::uint32_t target_rva,
    std::uintptr_t caller_return_address,
    void* this_context,
    void* arg_like) noexcept {
    LogInventorySummaryCandidateEntryTrace(
        candidate,
        target_rva,
        caller_return_address,
        this_context,
        arg_like);
    ArmInventoryClassDisplayCorrelation(
        caller_return_address,
        this_context,
        target_rva,
        arg_like);
}

void ArmItemDisplayClassDisplayCorrelation(
    std::uintptr_t caller_return_address,
    void* window_this,
    void* arg1_like,
    void* arg2_like) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const std::uint64_t activation =
        g_item_display_class_display_correlation_activation.fetch_add(1) + 1;
    g_item_display_class_display_correlation_refresh_caller_return.store(caller_return_address);
    g_item_display_class_display_correlation_refresh_caller_rva.store(caller_rva);
    g_item_display_class_display_correlation_window_this.store(
        reinterpret_cast<std::uintptr_t>(window_this));
    g_item_display_class_display_correlation_arg1_like.store(
        reinterpret_cast<std::uintptr_t>(arg1_like));
    g_item_display_class_display_correlation_arg2_like.store(
        reinterpret_cast<std::uintptr_t>(arg2_like));
    g_item_display_class_display_correlation_remaining.store(
        kItemDisplayClassDisplayCorrelationBudget);

    std::wstring message = L"ItemDisplayClassDisplayCorrelationWindow";
    message += L" activation=";
    message += std::to_wstring(activation);
    message += L" refresh_caller_return=";
    message += HexPtr(caller_return_address);
    message += L" refresh_caller_rva=";
    message += Hex32(caller_rva);
    message += L" window_this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(window_this));
    message += L" arg1_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(arg1_like));
    message += L" arg2_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(arg2_like));
    message += L" row_builder_rva=";
    message += Hex32(kItemDisplayClassRowBuilderTargetRva);
    message += L" budget=";
    message += std::to_wstring(kItemDisplayClassDisplayCorrelationBudget);
    monomyth::logger::Log(message);
}

void LogItemDisplayRefreshWorkerEntryTrace(
    std::uintptr_t caller_return_address,
    void* this_context,
    void* arg1_like,
    void* arg2_like) noexcept {
    const std::uint64_t count = ++g_item_display_refresh_worker_entry_trace_count;
    if (!ShouldLogItemDisplayRefreshWorkerEntryTrace(count)) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;

    std::wstring message = L"ItemDisplayRefreshWorkerEntryTrace";
    message += L" count=";
    message += std::to_wstring(count);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" window_this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" arg1_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(arg1_like));
    message += L" arg2_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(arg2_like));
    message += L" target_rva=";
    message += Hex32(kItemDisplayRefreshWorkerTargetRva);
    monomyth::logger::Log(message);
}

bool TryConsumeItemDisplayClassDisplayCorrelation(
    ItemDisplayClassDisplayCorrelationWindow* window) noexcept {
    if (window == nullptr) {
        return false;
    }

    *window = {};

    std::uint32_t current = g_item_display_class_display_correlation_remaining.load();
    while (current != 0) {
        if (g_item_display_class_display_correlation_remaining.compare_exchange_weak(
                current,
                current - 1)) {
            window->active = true;
            window->activation = g_item_display_class_display_correlation_activation.load();
            window->refresh_caller_return =
                g_item_display_class_display_correlation_refresh_caller_return.load();
            window->refresh_caller_rva =
                g_item_display_class_display_correlation_refresh_caller_rva.load();
            window->window_this = g_item_display_class_display_correlation_window_this.load();
            window->arg1_like = g_item_display_class_display_correlation_arg1_like.load();
            window->arg2_like = g_item_display_class_display_correlation_arg2_like.load();
            window->remaining_before = current;
            window->remaining_after = current - 1;
            return true;
        }
    }

    return false;
}

void LogItemDisplayClassDisplayTrace(
    const wchar_t* surface,
    const wchar_t* input_name,
    std::uintptr_t caller_return_address,
    std::uint32_t input_value,
    const ItemDisplayClassDisplayCorrelationWindow& correlation,
    bool override_applied,
    const wchar_t* reason,
    const char* formatted) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva = module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;

    std::wstring message = L"ItemDisplayClassDisplayTrace";
    message += L" activation=";
    message += std::to_wstring(correlation.activation);
    message += L" remaining_before=";
    message += std::to_wstring(correlation.remaining_before);
    message += L" remaining_after=";
    message += std::to_wstring(correlation.remaining_after);
    message += L" refresh_caller_return=";
    message += HexPtr(correlation.refresh_caller_return);
    message += L" refresh_caller_rva=";
    message += Hex32(correlation.refresh_caller_rva);
    message += L" window_this=";
    message += HexPtr(correlation.window_this);
    message += L" arg1_like=";
    message += HexPtr(correlation.arg1_like);
    message += L" arg2_like=";
    message += HexPtr(correlation.arg2_like);
    message += L" surface=";
    message += (surface == nullptr ? L"unknown" : surface);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" ";
    message += (input_name == nullptr ? L"input" : input_name);
    message += L"=";
    message += Hex32(input_value);
    message += L" override_applied=";
    message += (override_applied ? L"true" : L"false");
    if (formatted != nullptr && formatted[0] != '\0') {
        message += L" formatted=\"";
        message += WidenAsciiLossy(formatted);
        message += L"\"";
    }
    if (reason != nullptr && reason[0] != L'\0') {
        message += L" reason=\"";
        message += reason;
        message += L"\"";
    }
    monomyth::logger::Log(message);
}

bool IsLikelyItemDisplayStringLookupCaller(std::uint32_t caller_rva) noexcept {
    return caller_rva >= kItemDisplayStringLookupCallerRvaMin &&
           caller_rva < kItemDisplayStringLookupCallerRvaMaxExclusive;
}

bool TryCopyBytes(
    const void* source,
    std::size_t length,
    std::uint8_t* destination) noexcept {
    if (source == nullptr || destination == nullptr || length == 0) {
        return false;
    }

#if defined(_MSC_VER)
    __try {
        std::memcpy(destination, source, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(destination, source, length);
#endif
    return true;
}

template <typename T>
bool TryCopyObject(const void* source, T* destination) noexcept {
    return TryCopyBytes(
        source,
        sizeof(T),
        reinterpret_cast<std::uint8_t*>(destination));
}

bool TryWriteBytes(void* destination, const void* source, std::size_t length) noexcept {
    if (destination == nullptr || source == nullptr || length == 0) {
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(destination, length, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    bool copied = true;
#if defined(_MSC_VER)
    __try {
        std::memcpy(destination, source, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        copied = false;
    }
#else
    std::memcpy(destination, source, length);
#endif

    FlushInstructionCache(GetCurrentProcess(), destination, length);

    DWORD ignored = 0;
    VirtualProtect(destination, length, old_protect, &ignored);
    return copied;
}

template <typename T>
bool TryWriteObject(void* destination, const T& value) noexcept {
    return TryWriteBytes(destination, &value, sizeof(T));
}

bool TryReadEqPlayerDisplayedClassId(
    const void* subject,
    std::uint8_t* class_id) noexcept {
    if (subject == nullptr || class_id == nullptr) {
        return false;
    }

    return TryCopyObject(
        reinterpret_cast<const std::uint8_t*>(subject) + kEqPlayerDisplayedClassOffset,
        class_id);
}

bool TryReadEqSpawnName(
    const void* subject,
    std::string* name) noexcept {
    if (subject == nullptr || name == nullptr) {
        return false;
    }

    name->clear();
    name->reserve(kEqSpawnNameMaxBytes);
    for (std::size_t i = 0; i < (kEqSpawnNameMaxBytes - 1); ++i) {
        char ch = '\0';
        if (!TryCopyObject(
                reinterpret_cast<const std::uint8_t*>(subject) +
                    kEqSpawnNameOffset + i,
                &ch)) {
            name->clear();
            return false;
        }

        if (ch == '\0') {
            return !name->empty();
        }

        name->push_back(ch);
    }

    return !name->empty();
}

bool TryReadLocalPlayerPointer(void** local_player) noexcept {
    if (local_player == nullptr) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        return false;
    }

    return TryCopyObject(
        reinterpret_cast<const void*>(module_base + kLocalPlayerGlobalRva),
        local_player);
}

void TryPersistPendingMulticlassCache(const wchar_t* trigger) noexcept {
    if (!g_multiclass_cache_persist_pending.load()) {
        return;
    }

    const std::uint32_t pending_mask = g_multiclass_cache_persist_pending_mask.load();
    if (pending_mask == 0) {
        g_multiclass_cache_persist_pending.store(false);
        return;
    }

    std::uint8_t local_profile_class_id = 0;
    void* local_player = nullptr;
    std::string local_player_name;
    std::string server_name;
    const bool profile_class_ok = TryReadLocalProfileClassId(&local_profile_class_id);
    const bool server_ok = monomyth::multiclass_cache::TryGetCurrentServerName(&server_name);
    const bool local_player_ok =
        TryReadLocalPlayerPointer(&local_player) &&
        local_player != nullptr;
    const bool local_name_ok =
        local_player_ok && TryReadEqSpawnName(local_player, &local_player_name);
    const bool persisted =
        profile_class_ok &&
        server_ok &&
        local_name_ok &&
        monomyth::multiclass_cache::StoreCharacterClassMask(
            local_player_name.c_str(),
            pending_mask,
            local_profile_class_id,
            server_name.c_str());

    std::wstring message = L"MulticlassCachePersistAttempt";
    message += L" trigger=\"";
    message += (trigger == nullptr ? L"" : trigger);
    message += L"\" mask=";
    message += Hex32(pending_mask);
    message += L" profile_class_ok=";
    message += profile_class_ok ? L"true" : L"false";
    message += L" native_class_id=";
    message += std::to_wstring(local_profile_class_id);
    message += L" server_ok=";
    message += server_ok ? L"true" : L"false";
    if (server_ok) {
        message += L" server=\"";
        message += WidenAsciiLossy(server_name);
        message += L"\"";
    }
    message += L" local_player_ok=";
    message += local_player_ok ? L"true" : L"false";
    message += L" local_name_ok=";
    message += local_name_ok ? L"true" : L"false";
    if (local_name_ok) {
        message += L" name=\"";
        message += WidenAsciiLossy(local_player_name);
        message += L"\"";
    }
    message += L" path=\"";
    message += monomyth::multiclass_cache::GetCacheFilePathForLogging();
    message += L"\"";
    message += L" server_config=\"";
    message += monomyth::multiclass_cache::GetServerConfigPathForLogging();
    message += L"\"";
    message += L" persisted=";
    message += persisted ? L"true" : L"false";
    monomyth::logger::Log(message);

    if (persisted) {
        g_multiclass_cache_persist_pending.store(false);
        g_multiclass_cache_persist_pending_mask.store(0);
    }
}

bool TryReadLocalCharDataPointer(void** local_char_data) noexcept {
    if (local_char_data == nullptr) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        return false;
    }

    return TryCopyObject(
        reinterpret_cast<const void*>(module_base + kLocalCharDataGlobalRva),
        local_char_data);
}

bool TryResolveLocalProfileClassStorage(
    void** profile_out,
    std::uint32_t* class_id_out) noexcept {
    if (profile_out == nullptr || class_id_out == nullptr) {
        return false;
    }

    *profile_out = nullptr;
    *class_id_out = 0;

    void* local_char_data = nullptr;
    if (!TryReadLocalCharDataPointer(&local_char_data) || local_char_data == nullptr) {
        return false;
    }

    constexpr std::size_t kCharInfoCi2Offset = 0x31f0;
    constexpr std::size_t kCi2InfoCharInfo2Offset = 0x0004;
    constexpr std::size_t kPcProfileClassOffset = 0x3374;

    void* ci2 = nullptr;
    if (!TryCopyObject(
            reinterpret_cast<const std::uint8_t*>(local_char_data) + kCharInfoCi2Offset,
            &ci2) ||
        ci2 == nullptr) {
        return false;
    }

    void* profile = nullptr;
    if (!TryCopyObject(
            reinterpret_cast<const std::uint8_t*>(ci2) + kCi2InfoCharInfo2Offset,
            &profile) ||
        profile == nullptr) {
        return false;
    }

    std::uint32_t profile_class_id = 0;
    if (!TryCopyObject(
            reinterpret_cast<const std::uint8_t*>(profile) + kPcProfileClassOffset,
            &profile_class_id) ||
        !monomyth::multiclass_identity::IsPlayableClassId(profile_class_id)) {
        return false;
    }

    *profile_out = profile;
    *class_id_out = profile_class_id;
    return true;
}

bool TryReadInventoryWindowPointer(void** inventory_window) noexcept {
    if (inventory_window == nullptr) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        return false;
    }

    return TryCopyObject(
        reinterpret_cast<const void*>(module_base + kInventoryWindowGlobalRva),
        inventory_window);
}

bool TryReadInventoryClassTitleControlPointer(void** class_title_control) noexcept {
    if (class_title_control == nullptr) {
        return false;
    }

    void* inventory_window = nullptr;
    if (!TryReadInventoryWindowPointer(&inventory_window) || inventory_window == nullptr) {
        return false;
    }

    return TryCopyObject(
        reinterpret_cast<const std::uint8_t*>(inventory_window) + kInventoryClassTitleControlOffset,
        class_title_control);
}

bool TryReadLocalProfileClassId(std::uint8_t* class_id) noexcept {
    if (class_id == nullptr) {
        return false;
    }

    void* profile = nullptr;
    std::uint32_t profile_class_id = 0;
    if (!TryResolveLocalProfileClassStorage(&profile, &profile_class_id)) {
        return false;
    }

    *class_id = static_cast<std::uint8_t>(profile_class_id);
    return true;
}

bool TryReadLocalPlayerDisplayedClassId(std::uint8_t* class_id) noexcept {
    void* local_player = nullptr;
    if (!TryReadLocalPlayerPointer(&local_player) || local_player == nullptr) {
        return TryReadLocalProfileClassId(class_id);
    }

    std::uint8_t displayed_class_id = 0;
    if (TryReadEqPlayerDisplayedClassId(local_player, &displayed_class_id) &&
        monomyth::multiclass_identity::IsPlayableClassId(displayed_class_id)) {
        *class_id = displayed_class_id;
        return true;
    }

    return TryReadLocalProfileClassId(class_id);
}

void RestoreGuildTrainerSessionClassOverride(const wchar_t* trigger) noexcept {
    if (!g_guild_trainer_session_class_override.active) {
        return;
    }

    bool displayed_restore_ok = false;
    if (g_guild_trainer_session_class_override.displayed_original_copied &&
        g_guild_trainer_session_class_override.local_player != 0) {
        displayed_restore_ok = TryWriteObject(
            reinterpret_cast<void*>(
                g_guild_trainer_session_class_override.local_player +
                kEqPlayerDisplayedClassOffset),
            g_guild_trainer_session_class_override.displayed_original_class_id);
    }

    bool profile_restore_ok = false;
    if (g_guild_trainer_session_class_override.profile_original_copied &&
        g_guild_trainer_session_class_override.profile != 0) {
        profile_restore_ok = TryWriteObject(
            reinterpret_cast<void*>(
                g_guild_trainer_session_class_override.profile + 0x3374),
            g_guild_trainer_session_class_override.profile_original_class_id);
    }

    std::wstring message = L"GuildTrainerSessionClassOverride";
    message += L" action=restore";
    message += L" trigger=\"";
    message += (trigger == nullptr ? L"" : trigger);
    message += L"\" active_class_id=";
    message += std::to_wstring(g_guild_trainer_session_class_override.active_class_id);
    message += L" displayed_restore_ok=";
    message += displayed_restore_ok ? L"true" : L"false";
    message += L" profile_restore_ok=";
    message += profile_restore_ok ? L"true" : L"false";
    monomyth::logger::Log(message);

    g_guild_trainer_session_class_override = {};
}

void ApplyGuildTrainerSessionClassOverride(
    std::uint8_t class_id,
    const wchar_t* trigger) noexcept {
    if (!monomyth::multiclass_identity::IsPlayableClassId(class_id)) {
        return;
    }

    if (g_guild_trainer_session_class_override.active &&
        g_guild_trainer_session_class_override.active_class_id == class_id) {
        return;
    }

    if (g_guild_trainer_session_class_override.active) {
        RestoreGuildTrainerSessionClassOverride(L"switch_trainer_session");
    }

    GuildTrainerSessionClassOverrideState state = {};
    state.active = true;
    state.active_class_id = class_id;

    void* local_player = nullptr;
    std::uint8_t displayed_class_id = 0;
    const bool displayed_ok =
        TryReadLocalPlayerPointer(&local_player) &&
        local_player != nullptr &&
        TryReadEqPlayerDisplayedClassId(local_player, &displayed_class_id) &&
        monomyth::multiclass_identity::IsPlayableClassId(displayed_class_id);
    if (displayed_ok) {
        state.local_player = reinterpret_cast<std::uintptr_t>(local_player);
        state.displayed_original_copied = true;
        state.displayed_original_class_id = displayed_class_id;
    }

    void* profile = nullptr;
    std::uint32_t profile_class_id = 0;
    const bool profile_ok =
        TryResolveLocalProfileClassStorage(&profile, &profile_class_id);
    if (profile_ok) {
        state.profile = reinterpret_cast<std::uintptr_t>(profile);
        state.profile_original_copied = true;
        state.profile_original_class_id = profile_class_id;
    }

    bool displayed_write_ok = false;
    if (state.displayed_original_copied && state.local_player != 0) {
        displayed_write_ok = TryWriteObject(
            reinterpret_cast<void*>(state.local_player + kEqPlayerDisplayedClassOffset),
            class_id);
    }

    bool profile_write_ok = false;
    if (state.profile_original_copied && state.profile != 0) {
        const std::uint32_t widened_class_id = class_id;
        profile_write_ok = TryWriteObject(
            reinterpret_cast<void*>(state.profile + 0x3374),
            widened_class_id);
    }

    if (!displayed_write_ok && !profile_write_ok) {
        state = {};
    }

    std::wstring message = L"GuildTrainerSessionClassOverride";
    message += L" action=apply";
    message += L" trigger=\"";
    message += (trigger == nullptr ? L"" : trigger);
    message += L"\" requested_class_id=";
    message += std::to_wstring(class_id);
    message += L" displayed_original_ok=";
    message += state.displayed_original_copied ? L"true" : L"false";
    message += L" profile_original_ok=";
    message += state.profile_original_copied ? L"true" : L"false";
    message += L" displayed_write_ok=";
    message += displayed_write_ok ? L"true" : L"false";
    message += L" profile_write_ok=";
    message += profile_write_ok ? L"true" : L"false";
    monomyth::logger::Log(message);

    g_guild_trainer_session_class_override = state;
}

const char* BuildLocalPlayerClassDisplayAscii(
    monomyth::multiclass_identity::ClassDisplayStyle style,
    const wchar_t* surface) noexcept;

void TryPersistPendingMulticlassCache(const wchar_t* trigger) noexcept;

bool TryResolvePreferredAuthoritativePrimaryClassId(
    unsigned int requested_class_id,
    const monomyth::server_auth_stats::Snapshot& snapshot,
    std::uint8_t* primary_class_id) noexcept {
    if (primary_class_id == nullptr) {
        return false;
    }

    if (snapshot.has_classes_bitmask &&
        snapshot.classes_bitmask != 0 &&
        monomyth::multiclass_identity::IsPlayableClassMask(snapshot.classes_bitmask)) {
        if (monomyth::multiclass_identity::IsPlayableClassId(requested_class_id) &&
            monomyth::multiclass_identity::HasClass(
                snapshot.classes_bitmask,
                requested_class_id)) {
            *primary_class_id = static_cast<std::uint8_t>(requested_class_id);
            return true;
        }

        std::uint8_t local_class_id = 0;
        if (TryReadLocalPlayerDisplayedClassId(&local_class_id) &&
            monomyth::multiclass_identity::HasClass(
                snapshot.classes_bitmask,
                local_class_id)) {
            *primary_class_id = local_class_id;
            return true;
        }

        for (unsigned int class_id = monomyth::multiclass_identity::kFirstPlayableClassId;
             class_id <= monomyth::multiclass_identity::kLastPlayableClassId;
             ++class_id) {
            if (monomyth::multiclass_identity::HasClass(
                    snapshot.classes_bitmask,
                    class_id)) {
                *primary_class_id = static_cast<std::uint8_t>(class_id);
                return true;
            }
        }
    }

    return TryReadLocalPlayerDisplayedClassId(primary_class_id);
}

const char* BuildLocalPlayerClassDisplayAscii(
    monomyth::multiclass_identity::ClassDisplayStyle style,
    const wchar_t* surface) noexcept {
    TryPersistPendingMulticlassCache(surface);

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    if (!g_multiclass_ui_display_enabled) {
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            nullptr,
            nullptr,
            false,
            false,
            0,
            snapshot,
            nullptr,
            L"multiclass_ui_display_disabled");
        return nullptr;
    }

    std::uint8_t primary_class_id = 0;
    if (!TryReadLocalPlayerDisplayedClassId(&primary_class_id) ||
        !monomyth::multiclass_identity::IsPlayableClassId(primary_class_id)) {
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            nullptr,
            nullptr,
            false,
            false,
            primary_class_id,
            snapshot,
            nullptr,
            L"local_player_displayed_class_unavailable_or_invalid");
        return nullptr;
    }

    const std::string formatted = monomyth::multiclass_identity::FormatClassDisplayAscii(
        primary_class_id,
        snapshot.has_classes_bitmask,
        snapshot.classes_bitmask,
        style);
    if (formatted.empty()) {
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            nullptr,
            nullptr,
            false,
            true,
            primary_class_id,
            snapshot,
            nullptr,
            L"formatter_returned_empty");
        return nullptr;
    }

    static thread_local std::string buffer;
    buffer = formatted;
    LogUiClassDisplayTrace(
        surface,
        L"override",
        style,
        nullptr,
        nullptr,
        false,
        true,
        primary_class_id,
        snapshot,
        buffer.c_str(),
        L"local_player_multiclass_display_applied");
    return buffer.c_str();
}

const char* BuildPreferredLocalPlayerClassDisplayAscii(
    unsigned int requested_class_id,
    monomyth::multiclass_identity::ClassDisplayStyle style,
    const wchar_t* surface) noexcept {
    TryPersistPendingMulticlassCache(surface);

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    if (!g_multiclass_ui_display_enabled) {
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            nullptr,
            nullptr,
            false,
            false,
            0,
            snapshot,
            nullptr,
            L"multiclass_ui_display_disabled");
        return nullptr;
    }

    std::uint8_t primary_class_id = 0;
    if (!TryResolvePreferredAuthoritativePrimaryClassId(
            requested_class_id,
            snapshot,
            &primary_class_id) ||
        !monomyth::multiclass_identity::IsPlayableClassId(primary_class_id)) {
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            nullptr,
            nullptr,
            false,
            false,
            primary_class_id,
            snapshot,
            nullptr,
            L"preferred_primary_class_unavailable_or_invalid");
        return nullptr;
    }

    const std::string formatted = monomyth::multiclass_identity::FormatClassDisplayAscii(
        primary_class_id,
        snapshot.has_classes_bitmask,
        snapshot.classes_bitmask,
        style);
    if (formatted.empty()) {
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            nullptr,
            nullptr,
            false,
            true,
            primary_class_id,
            snapshot,
            nullptr,
            L"formatter_returned_empty");
        return nullptr;
    }

    static thread_local std::string buffer;
    buffer = formatted;
    LogUiClassDisplayTrace(
        surface,
        L"override",
        style,
        nullptr,
        nullptr,
        false,
        true,
        primary_class_id,
        snapshot,
        buffer.c_str(),
        L"preferred_primary_multiclass_display_applied");
    return buffer.c_str();
}

const char* BuildLocalSubjectClassDisplayAscii(
    const void* subject,
    monomyth::multiclass_identity::ClassDisplayStyle style,
    const wchar_t* surface) noexcept {
    TryPersistPendingMulticlassCache(surface);

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    if (subject == nullptr || !g_multiclass_ui_display_enabled) {
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            subject,
            nullptr,
            false,
            false,
            0,
            snapshot,
            nullptr,
            subject == nullptr
                ? L"subject_null"
                : L"multiclass_ui_display_disabled");
        return nullptr;
    }

    void* local_player = nullptr;
    const bool local_player_copied =
        TryReadLocalPlayerPointer(&local_player) && local_player != nullptr;
    bool subject_matches_local = local_player_copied && local_player == subject;
    if (!subject_matches_local && local_player_copied) {
        std::string subject_name;
        std::string local_name;
        if (TryReadEqSpawnName(subject, &subject_name) &&
            TryReadEqSpawnName(local_player, &local_name) &&
            subject_name == local_name) {
            subject_matches_local = true;
        }
    }

    if (!subject_matches_local) {
        std::uint8_t primary_class_id = 0;
        const bool primary_class_copied =
            TryReadEqPlayerDisplayedClassId(subject, &primary_class_id);
        LogUiClassDisplayTrace(
            surface,
            L"fallback",
            style,
            subject,
            local_player,
            false,
            primary_class_copied,
            primary_class_id,
            snapshot,
            nullptr,
            L"subject_does_not_match_local_player_pointer_or_name");
        return nullptr;
    }

    return BuildLocalPlayerClassDisplayAscii(style, surface);
}

bool TryResolveClientItemDataFromWrapper(
    std::uintptr_t item_wrapper_like,
    std::uintptr_t* item_data_like) noexcept {
    if (item_wrapper_like == 0 || item_data_like == nullptr) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        return false;
    }

    const auto get_item_data =
        reinterpret_cast<ItemWrapperGetDataThunkFn>(module_base + kClientItemWrapperGetDataThunkRva);
    if (get_item_data == nullptr) {
        return false;
    }

    void* resolved_item_data = nullptr;
#if defined(_MSC_VER)
    __try {
        resolved_item_data = get_item_data(reinterpret_cast<void*>(item_wrapper_like));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    resolved_item_data = get_item_data(reinterpret_cast<void*>(item_wrapper_like));
#endif

    *item_data_like = reinterpret_cast<std::uintptr_t>(resolved_item_data);
    return resolved_item_data != nullptr;
}

bool TryReadClientItemClassMaskFromWrapper(
    std::uintptr_t item_wrapper_like,
    std::uint32_t* item_class_mask,
    std::uintptr_t* item_data_like) noexcept {
    if (item_class_mask == nullptr) {
        return false;
    }

    std::uintptr_t resolved_item_data = 0;
    if (!TryResolveClientItemDataFromWrapper(item_wrapper_like, &resolved_item_data)) {
        return false;
    }

    if (item_data_like != nullptr) {
        *item_data_like = resolved_item_data;
    }

    return TryCopyObject(
        reinterpret_cast<const std::uint8_t*>(resolved_item_data) + kClientItemInfoClassesOffset,
        item_class_mask);
}

template <typename T>
bool TryReadClientItemFieldFromWrapper(
    std::uintptr_t item_wrapper_like,
    std::size_t offset,
    T* value,
    std::uintptr_t* item_data_like) noexcept {
    if (value == nullptr) {
        return false;
    }

    std::uintptr_t resolved_item_data = 0;
    if (!TryResolveClientItemDataFromWrapper(item_wrapper_like, &resolved_item_data)) {
        return false;
    }

    if (item_data_like != nullptr) {
        *item_data_like = resolved_item_data;
    }

    return TryCopyObject(
        reinterpret_cast<const std::uint8_t*>(resolved_item_data) + offset,
        value);
}

bool TryReadLocalPlayerCharacterPointer(void** character) noexcept {
    if (character == nullptr) {
        return false;
    }

    void* local_player = nullptr;
    if (!TryReadLocalPlayerPointer(&local_player) || local_player == nullptr) {
        return false;
    }

    return TryCopyObject(
        reinterpret_cast<const std::uint8_t*>(local_player) + kLocalPlayerCharacterOffset,
        character);
}

bool TryReadLocalPlayerHoldingAnimation(std::uint8_t* holding_animation) noexcept {
    if (holding_animation == nullptr) {
        return false;
    }

    void* local_player = nullptr;
    if (!TryReadLocalPlayerPointer(&local_player) || local_player == nullptr) {
        return false;
    }

    return TryCopyObject(
        reinterpret_cast<const std::uint8_t*>(local_player) + kLocalPlayerHoldingAnimationOffset,
        holding_animation);
}

bool TryReadLocalPlayerLevel(std::uint32_t* level) noexcept {
    if (level == nullptr) {
        return false;
    }

    void* profile = nullptr;
    std::uint32_t profile_class_id = 0;
    if (!TryResolveLocalProfileClassStorage(&profile, &profile_class_id) || profile == nullptr) {
        return false;
    }

    constexpr std::size_t kPcProfileLevelOffset = 0x3388;
    return TryCopyObject(
        reinterpret_cast<const std::uint8_t*>(profile) + kPcProfileLevelOffset,
        level);
}

bool TryResolveDualWieldMatrixEntitlement(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint32_t local_player_level,
    unsigned int* matched_class_id,
    std::uint32_t* required_level) noexcept {
    if (matched_class_id == nullptr || required_level == nullptr) {
        return false;
    }

    *matched_class_id = 0;
    *required_level = 0;

    if (!has_class_mask ||
        authoritative_class_mask == 0 ||
        !monomyth::multiclass_identity::IsPlayableClassMask(authoritative_class_mask)) {
        return false;
    }

    struct DualWieldMatrixEntry {
        unsigned int class_id;
        std::uint32_t level;
    };

    constexpr std::array<DualWieldMatrixEntry, 6> kDualWieldMatrix = {{
        {7u, 1u},   // Monk
        {1u, 13u},  // Warrior
        {9u, 13u},  // Rogue
        {4u, 17u},  // Ranger
        {8u, 17u},  // Bard
        {15u, 17u}, // Beastlord
    }};

    for (const DualWieldMatrixEntry& entry : kDualWieldMatrix) {
        if (monomyth::multiclass_identity::HasClass(authoritative_class_mask, entry.class_id) &&
            local_player_level >= entry.level) {
            *matched_class_id = entry.class_id;
            *required_level = entry.level;
            return true;
        }
    }

    return false;
}

bool IsWeaponItemClass(std::uint8_t item_class) noexcept {
    switch (item_class) {
    case 0:   // 1H slashing
    case 1:   // 2H slashing
    case 2:   // piercing
    case 3:   // 1H blunt
    case 4:   // 2H blunt
    case 5:   // bow
    case 7:   // thrown
    case 35:  // 2H piercing
    case 36:  // fishing pole
    case 45:  // martial
        return true;
    default:
        return false;
    }
}

bool HoldingAnimationBlocksSecondaryWeapon(std::uint8_t holding_animation) noexcept {
    return holding_animation == kHoldingAnimationTwoHandedWeapon ||
        holding_animation == kHoldingAnimationBow;
}

bool TryHasLocalPlayerSkill(int skill_id, bool* has_skill) noexcept {
    if (has_skill == nullptr ||
        g_character_zone_client_has_skill == nullptr ||
        skill_id < 0) {
        return false;
    }

    void* character = nullptr;
    if (!TryReadLocalPlayerCharacterPointer(&character) || character == nullptr) {
        return false;
    }

#if defined(_MSC_VER)
    __try {
        *has_skill = g_character_zone_client_has_skill(character, skill_id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    *has_skill = g_character_zone_client_has_skill(character, skill_id);
#endif

    return true;
}

void LogActivatedSkillVisibilityOverride(
    int skill_id,
    std::uintptr_t caller_return_address,
    bool native_result,
    const monomyth::server_auth_stats::Snapshot& snapshot) noexcept {
    ++g_skill_visibility_override_count;
    if (g_skill_visibility_override_count > kSkillVisibilityOverrideInitialLogCount &&
        (g_skill_visibility_override_count % kSkillVisibilityOverrideLogInterval) != 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_rva =
        (module_base != 0 && caller_return_address >= module_base)
            ? static_cast<std::uint32_t>(caller_return_address - module_base)
            : 0;

    std::wstring message = L"hook_manager: activated skill visibility override";
    message += L" skill_id=";
    message += std::to_wstring(skill_id);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_rva=";
    message += Hex32(caller_rva);
    message += L" native_result=";
    message += native_result ? L"true" : L"false";
    message += L" has_statActivatedSkillMaskLow=";
    message += snapshot.has_activated_skill_mask_low ? L"true" : L"false";
    message += L" statActivatedSkillMaskLow=";
    message += Hex64(snapshot.activated_skill_mask_low);
    message += L" has_statActivatedSkillMaskHigh=";
    message += snapshot.has_activated_skill_mask_high ? L"true" : L"false";
    message += L" statActivatedSkillMaskHigh=";
    message += Hex64(snapshot.activated_skill_mask_high);
    monomyth::logger::Log(message);
}

bool MONOMYTH_FASTCALL CharacterZoneClientHasSkillVisibilityHook(
    void* this_context,
    void*,
    int skill_id) noexcept {
    bool native_result = false;
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();

    if (g_original_character_zone_client_has_skill == nullptr) {
        return false;
    }

#if defined(_MSC_VER)
    __try {
        native_result = g_original_character_zone_client_has_skill(this_context, skill_id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    native_result = g_original_character_zone_client_has_skill(this_context, skill_id);
#endif

    if (native_result) {
        return true;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    if (!monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            snapshot,
            skill_id)) {
        return false;
    }

    LogActivatedSkillVisibilityOverride(skill_id, caller_return_address, native_result, snapshot);
    return true;
}

OffhandWeaponPolicySnapshot CaptureOffhandWeaponPolicySnapshot(
    std::uintptr_t item_wrapper_like,
    const monomyth::server_auth_stats::Snapshot& authoritative_snapshot) noexcept {
    OffhandWeaponPolicySnapshot snapshot = {};
    snapshot.item_class_mask_copied =
        TryReadClientItemClassMaskFromWrapper(
            item_wrapper_like,
            &snapshot.item_class_mask,
            &snapshot.item_data_like);
    snapshot.equip_slots_copied =
        TryReadClientItemFieldFromWrapper(
            item_wrapper_like,
            kClientItemInfoEquipSlotsOffset,
            &snapshot.equip_slots,
            nullptr);
    snapshot.item_class_copied =
        TryReadClientItemFieldFromWrapper(
            item_wrapper_like,
            kClientItemInfoItemClassOffset,
            &snapshot.item_class,
            nullptr);
    snapshot.local_player_level_copied =
        TryReadLocalPlayerLevel(&snapshot.local_player_level);
    snapshot.holding_animation_copied =
        TryReadLocalPlayerHoldingAnimation(&snapshot.holding_animation);
    snapshot.is_weapon = snapshot.item_class_copied && IsWeaponItemClass(snapshot.item_class);
    snapshot.can_wear_secondary =
        snapshot.equip_slots_copied &&
        (snapshot.equip_slots & kSecondaryEquipmentSlotBit) != 0;
    snapshot.item_matches_assigned_class =
        monomyth::multiclass_identity::HasAnyAuthoritativeClientItemClass(
            authoritative_snapshot.has_classes_bitmask,
            authoritative_snapshot.classes_bitmask,
            snapshot.item_class_mask);
    snapshot.dual_wield_skill_checked =
        TryHasLocalPlayerSkill(kSkillDualWield, &snapshot.has_dual_wield_skill);
    snapshot.authoritative_dual_wield_class_present =
        monomyth::multiclass_identity::HasAnyAuthoritativeDualWieldClass(
            authoritative_snapshot.has_classes_bitmask,
            authoritative_snapshot.classes_bitmask);
    unsigned int matched_dual_wield_class_id = 0;
    snapshot.dual_wield_matrix_matched =
        snapshot.local_player_level_copied &&
        TryResolveDualWieldMatrixEntitlement(
            authoritative_snapshot.has_classes_bitmask,
            authoritative_snapshot.classes_bitmask,
            snapshot.local_player_level,
            &matched_dual_wield_class_id,
            &snapshot.dual_wield_required_level);
    if (snapshot.dual_wield_matrix_matched &&
        monomyth::multiclass_identity::IsPlayableClassId(matched_dual_wield_class_id)) {
        snapshot.dual_wield_entitlement_class_id =
            static_cast<std::uint8_t>(matched_dual_wield_class_id);
    }
    snapshot.has_dual_wield_entitlement =
        snapshot.has_dual_wield_skill ||
        (snapshot.dual_wield_skill_checked &&
         !snapshot.has_dual_wield_skill &&
         snapshot.dual_wield_matrix_matched);
    snapshot.dual_wield_entitlement_fallback_used =
        snapshot.has_dual_wield_entitlement && !snapshot.has_dual_wield_skill;
    snapshot.primary_blocks_secondary_weapon =
        snapshot.holding_animation_copied &&
        HoldingAnimationBlocksSecondaryWeapon(snapshot.holding_animation);
    snapshot.eligible =
        snapshot.is_weapon &&
        snapshot.can_wear_secondary &&
        (snapshot.dual_wield_skill_checked || snapshot.dual_wield_matrix_matched) &&
        !snapshot.primary_blocks_secondary_weapon &&
        monomyth::multiclass_identity::HasAuthoritativeOffhandWeaponClassAndDualWield(
            authoritative_snapshot.has_classes_bitmask,
            authoritative_snapshot.classes_bitmask,
            snapshot.item_class_mask,
            snapshot.has_dual_wield_entitlement);
    return snapshot;
}

SpellbookMemStateSnapshot CaptureSpellbookMemState(const void* this_window) noexcept {
    SpellbookMemStateSnapshot snapshot = {};
    const auto* state = reinterpret_cast<const std::uint8_t*>(this_window);
    if (state == nullptr) {
        return snapshot;
    }

    snapshot.state_234_copied = TryCopyBytes(
        state + 0x234,
        sizeof(snapshot.state_234),
        reinterpret_cast<std::uint8_t*>(&snapshot.state_234));
    snapshot.state_238_copied = TryCopyBytes(
        state + 0x238,
        sizeof(snapshot.state_238),
        reinterpret_cast<std::uint8_t*>(&snapshot.state_238));
    snapshot.state_23c_copied = TryCopyBytes(
        state + 0x23c,
        sizeof(snapshot.state_23c),
        reinterpret_cast<std::uint8_t*>(&snapshot.state_23c));
    snapshot.state_240_copied = TryCopyBytes(
        state + 0x240,
        sizeof(snapshot.state_240),
        reinterpret_cast<std::uint8_t*>(&snapshot.state_240));
    snapshot.state_244_copied = TryCopyBytes(
        state + 0x244,
        sizeof(snapshot.state_244),
        reinterpret_cast<std::uint8_t*>(&snapshot.state_244));
    return snapshot;
}

void ResetStartSpellScribePrecheckClassMaskSnapshot(
    std::uint32_t correlation_id) noexcept {
    g_start_spell_scribe_precheck_class_mask_snapshot = {};
    g_start_spell_scribe_precheck_class_mask_snapshot.active = true;
    g_start_spell_scribe_precheck_class_mask_snapshot.correlation_id = correlation_id;
}

void ClearStartSpellScribePrecheckClassMaskSnapshot() noexcept {
    g_start_spell_scribe_precheck_class_mask_snapshot = {};
}

void ClearPendingMemorizeSendObservation() noexcept {
    g_memorize_send_pending_correlation_id = 0;
    g_memorize_send_pending_wrapper_sends = 0;
    g_memorize_send_pending_window = nullptr;
}

void CaptureStartSpellScribePrecheckClassResolverFromGateContext(
    void* this_context) noexcept {
    auto& snapshot = g_start_spell_scribe_precheck_class_mask_snapshot;
    if (!snapshot.active || snapshot.class_resolver_called || this_context == nullptr) {
        return;
    }

    std::uintptr_t descriptor_table = 0;
    if (!TryCopyObject(
            reinterpret_cast<const std::uint8_t*>(this_context) +
                kScribeGateDescriptorTableOffset,
            &descriptor_table) ||
        descriptor_table == 0) {
        return;
    }

    std::uint32_t relative_offset = 0;
    if (!TryCopyObject(
            reinterpret_cast<const void*>(descriptor_table + kScribeGateRelativeOffsetField),
            &relative_offset) ||
        relative_offset == 0 ||
        relative_offset > kScribeGateMaxRelativeLookupOffset) {
        return;
    }

    const std::uintptr_t this_context_address =
        reinterpret_cast<std::uintptr_t>(this_context);
    if (this_context_address >
        (std::numeric_limits<std::uintptr_t>::max() - relative_offset -
         kScribeGateLookupContextBias)) {
        return;
    }

    const std::uintptr_t lookup_context =
        this_context_address + relative_offset + kScribeGateLookupContextBias;
    snapshot.class_lookup_context = lookup_context;
    snapshot.class_lookup_context_copied = true;

    std::uintptr_t node = 0;
    if (!TryCopyObject(reinterpret_cast<const void*>(lookup_context), &node)) {
        return;
    }

    snapshot.class_lookup_key_copied = TryCopyObject(
        reinterpret_cast<const void*>(lookup_context + kScribeGateLookupKeyOffset),
        &snapshot.class_lookup_key);
    if (!snapshot.class_lookup_key_copied) {
        return;
    }

    for (std::size_t visited_nodes = 0;
         node != 0 && visited_nodes < kScribeGateMaxLookupNodes;
         ++visited_nodes) {
        std::uint32_t node_key = 0;
        if (!TryCopyObject(reinterpret_cast<const void*>(node), &node_key)) {
            return;
        }

        if (node_key == snapshot.class_lookup_key) {
            std::uintptr_t class_record = 0;
            if (!TryCopyObject(
                    reinterpret_cast<const void*>(node + kScribeGateNodeRecordOffset),
                    &class_record)) {
                return;
            }

            snapshot.class_resolver_called = true;
            snapshot.class_resolver_emulated = true;
            snapshot.class_resolver_this = this_context;
            snapshot.class_record = reinterpret_cast<void*>(class_record);
            if (class_record != 0) {
                snapshot.class_id_copied = TryCopyObject(
                    reinterpret_cast<const void*>(
                        class_record + kScribeGateResolvedClassIdOffset),
                    &snapshot.class_id);
            }
            return;
        }

        if (!TryCopyObject(
                reinterpret_cast<const void*>(node + kScribeGateNodeNextOffset),
                &node)) {
            return;
        }
    }

    snapshot.class_resolver_called = true;
    snapshot.class_resolver_emulated = true;
    snapshot.class_resolver_this = this_context;
    snapshot.class_record = nullptr;
}

void AppendStartSpellScribePrecheckClassMaskFields(std::wstring* message) {
    if (message == nullptr) {
        return;
    }

    const auto& snapshot = g_start_spell_scribe_precheck_class_mask_snapshot;
    message->append(L" class_resolver_called=");
    message->append(snapshot.class_resolver_called ? L"true" : L"false");
    if (snapshot.class_resolver_called) {
        message->append(L" class_resolver_source=");
        message->append(snapshot.class_resolver_emulated ? L"emulated_gate_context" : L"hook");
        message->append(L" class_resolver_this=");
        message->append(HexPtr(reinterpret_cast<std::uintptr_t>(snapshot.class_resolver_this)));
        message->append(L" class_lookup_context_status=");
        message->append(snapshot.class_lookup_context_copied ? L"copied" : L"unavailable");
        if (snapshot.class_lookup_context_copied) {
            message->append(L" class_lookup_context=");
            message->append(HexPtr(snapshot.class_lookup_context));
        }
        message->append(L" class_lookup_key_status=");
        message->append(snapshot.class_lookup_key_copied ? L"copied" : L"unavailable");
        if (snapshot.class_lookup_key_copied) {
            message->append(L" class_lookup_key=");
            message->append(Hex32(snapshot.class_lookup_key));
        }
        message->append(L" class_record=");
        message->append(HexPtr(reinterpret_cast<std::uintptr_t>(snapshot.class_record)));
        message->append(L" class_id_status=");
        message->append(snapshot.class_id_copied ? L"copied" : L"unreadable");
        if (snapshot.class_id_copied) {
            message->append(L" class_id=");
            message->append(std::to_wstring(snapshot.class_id));
        }
    }

    message->append(L" assigned_mask_getter_called=");
    message->append(snapshot.assigned_mask_getter_called ? L"true" : L"false");
    if (snapshot.assigned_mask_getter_called) {
        message->append(L" assigned_mask_getter_this=");
        message->append(HexPtr(reinterpret_cast<std::uintptr_t>(
            snapshot.assigned_mask_getter_this)));
        message->append(L" assigned_mask_flag_like=");
        message->append(std::to_wstring(snapshot.assigned_mask_flag_like));
        message->append(L" assigned_mask_extra_like=");
        message->append(std::to_wstring(snapshot.assigned_mask_extra_like));
        message->append(L" assigned_mask=");
        message->append(Hex32(snapshot.assigned_mask));
    }

    message->append(L" late_rule_4462c0_called=");
    message->append(snapshot.rule_4462c0_called ? L"true" : L"false");
    message->append(L" late_rule_446190_called=");
    message->append(snapshot.rule_446190_called ? L"true" : L"false");
    message->append(L" late_rule_446200_called=");
    message->append(snapshot.rule_446200_called ? L"true" : L"false");
    message->append(L" late_rule_446380_called=");
    message->append(snapshot.rule_446380_called ? L"true" : L"false");
    message->append(L" authoritative_mask_status=");
    message->append(snapshot.authoritative_mask_present ? L"present" : L"absent");
    if (snapshot.authoritative_mask_present) {
        message->append(L" authoritative_mask=");
        message->append(Hex32(snapshot.authoritative_mask));
        message->append(L" authoritative_mask_intersection=");
        message->append(Hex32(snapshot.authoritative_mask_intersection));
        message->append(L" authoritative_mask_intersects_spell_mask=");
        message->append(
            snapshot.authoritative_mask_intersects_spell_mask ? L"true" : L"false");
    }

    if (snapshot.class_id_copied && snapshot.class_id != 0 && snapshot.class_id <= 32 &&
        snapshot.assigned_mask_getter_called) {
        const std::uint32_t class_bit = 1u << (snapshot.class_id - 1);
        message->append(L" class_bit_status=derived class_bit=");
        message->append(Hex32(class_bit));
        message->append(L" class_mask_match=");
        message->append((snapshot.assigned_mask & class_bit) != 0 ? L"true" : L"false");
    } else if (snapshot.class_id_copied) {
        message->append(L" class_bit_status=invalid_class_id");
    } else {
        message->append(L" class_bit_status=unavailable");
    }

    message->append(L" behavior_override_applied=");
    message->append(snapshot.behavior_override_applied ? L"true" : L"false");
}

void AppendSpellbookMemStateFields(
    std::wstring* message,
    const wchar_t* prefix,
    const SpellbookMemStateSnapshot& snapshot) {
    if (message == nullptr || prefix == nullptr) {
        return;
    }

    const struct {
        const wchar_t* field;
        bool copied;
        std::uint32_t value;
        bool render_hex;
    } fields[] = {
        {L"234", snapshot.state_234_copied, snapshot.state_234, true},
        {L"238", snapshot.state_238_copied, snapshot.state_238, true},
        {L"23c", snapshot.state_23c_copied, snapshot.state_23c, true},
        {L"240", snapshot.state_240_copied, snapshot.state_240, true},
        {L"244", snapshot.state_244_copied, snapshot.state_244, false},
    };

    for (const auto& field : fields) {
        message->append(L" ");
        message->append(prefix);
        message->append(L"_state_");
        message->append(field.field);
        message->append(L"_status=");
        message->append(field.copied ? L"copied" : L"unreadable");
        if (field.copied) {
            message->append(L" ");
            message->append(prefix);
            message->append(L"_state_");
            message->append(field.field);
            message->append(L"=");
            message->append(field.render_hex ? Hex32(field.value) : std::to_wstring(field.value));
        }
    }
}

std::wstring HexBytes(const std::uint8_t* bytes, std::size_t length) {
    if (bytes == nullptr || length == 0) {
        return L"";
    }

    std::wstringstream stream;
    stream << std::hex << std::setfill(L'0');
    for (std::size_t i = 0; i < length; ++i) {
        if (i != 0) {
            stream << L' ';
        }
        stream << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return stream.str();
}

RelativeCallResolution ResolveRelativeCall(
    const std::uint8_t* code,
    std::size_t available,
    std::uintptr_t instruction_address) noexcept {
    RelativeCallResolution resolution = {};
    resolution.call_site = instruction_address;
    if (code == nullptr || available < 5 || code[0] != 0xe8 || instruction_address == 0) {
        return resolution;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, code + 1, sizeof(relative));
    resolution.target = instruction_address + 5 + relative;
    resolution.resolved = true;
    return resolution;
}

bool TryResolveRelativeJumpTarget(
    std::uintptr_t instruction_address,
    std::uintptr_t* target_out) noexcept {
    if (instruction_address == 0 || target_out == nullptr) {
        return false;
    }

    std::array<std::uint8_t, 5> bytes = {};
    if (!TryCopyBytes(
            reinterpret_cast<const void*>(instruction_address),
            bytes.size(),
            bytes.data()) ||
        bytes[0] != 0xe9) {
        return false;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, bytes.data() + 1, sizeof(relative));
    *target_out = instruction_address + bytes.size() + relative;
    return true;
}

JumpThunkResolution ResolveJumpThunkTarget(std::uintptr_t target_address) noexcept {
    JumpThunkResolution resolution = {};
    if (target_address == 0) {
        return resolution;
    }

    constexpr std::size_t kThunkPrefixLength = 8;
    constexpr std::array<std::uint8_t, kThunkPrefixLength> kHotpatchJumpThunkPrefix = {{
        0x8b, 0xff, 0x55, 0x8b, 0xec, 0x5d, 0xe9, 0x00,
    }};

    std::uintptr_t current = target_address;
    for (std::uint32_t hop = 0; hop < 2; ++hop) {
        std::array<std::uint8_t, kThunkPrefixLength> bytes = {};
        if (!TryCopyBytes(
                reinterpret_cast<const void*>(current),
                bytes.size(),
                bytes.data())) {
            return resolution;
        }

        if (std::memcmp(
                bytes.data(),
                kHotpatchJumpThunkPrefix.data(),
                kHotpatchJumpThunkPrefix.size() - 1) != 0 ||
            bytes[6] != 0xe9) {
            if (hop > 0) {
                resolution.terminal_target = current;
                resolution.hop_count = hop;
                resolution.resolved = true;
            }
            return resolution;
        }

        std::uintptr_t jump_target = 0;
        if (!TryResolveRelativeJumpTarget(current + 6, &jump_target)) {
            return resolution;
        }

        current = jump_target;
        resolution.terminal_target = current;
        resolution.hop_count = hop + 1;
        resolution.resolved = true;
    }

    return resolution;
}

KnownMemorizeFollowupCallsiteMatch MatchKnownMemorizeFollowupCallsite(
    std::uint32_t caller_return_rva,
    std::uint32_t mode_like,
    const std::uint8_t* caller_context_bytes,
    std::size_t caller_context_length,
    bool caller_context_available) noexcept {
    for (const auto& candidate : kKnownMemorizeFollowupCallsites) {
        if (candidate.return_rva != caller_return_rva || candidate.mode_like != mode_like) {
            continue;
        }

        const bool bytes_match =
            caller_context_available &&
            caller_context_bytes != nullptr &&
            caller_context_length >= candidate.caller_context_bytes.size() &&
            std::memcmp(
                caller_context_bytes,
                candidate.caller_context_bytes.data(),
                candidate.caller_context_bytes.size()) == 0;
        return {&candidate, bytes_match};
    }

    return {};
}

void AppendResolvedCallFields(
    std::wstring* message,
    const wchar_t* prefix,
    const RelativeCallResolution& call,
    std::uintptr_t module_base,
    std::uintptr_t wrapper_address) {
    if (message == nullptr || prefix == nullptr) {
        return;
    }

    message->append(L" ");
    message->append(prefix);
    message->append(L"_site=");
    message->append(HexPtr(call.call_site));
    if (module_base != 0 && call.call_site >= module_base) {
        message->append(L" ");
        message->append(prefix);
        message->append(L"_site_rva=");
        message->append(Hex32(static_cast<std::uint32_t>(call.call_site - module_base)));
    }
    message->append(L" ");
    message->append(prefix);
    message->append(L"_target=");
    message->append(HexPtr(call.target));
    if (module_base != 0 && call.target >= module_base) {
        message->append(L" ");
        message->append(prefix);
        message->append(L"_target_rva=");
        message->append(Hex32(static_cast<std::uint32_t>(call.target - module_base)));
    }
    message->append(L" ");
    message->append(prefix);
    message->append(L"_role=");
    if (wrapper_address != 0 && call.target == wrapper_address) {
        message->append(L"wrapper_call");
    } else {
        message->append(L"followup_call");
    }

    const JumpThunkResolution thunk = ResolveJumpThunkTarget(call.target);
    if (thunk.resolved && thunk.terminal_target != 0 && thunk.terminal_target != call.target) {
        message->append(L" ");
        message->append(prefix);
        message->append(L"_terminal_target=");
        message->append(HexPtr(thunk.terminal_target));
        if (module_base != 0 && thunk.terminal_target >= module_base) {
            message->append(L" ");
            message->append(prefix);
            message->append(L"_terminal_target_rva=");
            message->append(
                Hex32(static_cast<std::uint32_t>(thunk.terminal_target - module_base)));
        }
        message->append(L" ");
        message->append(prefix);
        message->append(L"_terminal_role=jump_thunk_target");
        message->append(L" ");
        message->append(prefix);
        message->append(L"_terminal_hop_count=");
        message->append(std::to_wstring(thunk.hop_count));
    }
}

std::uintptr_t GetHostModuleBase() noexcept {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

bool TryReadPacketOpcode(
    const void* packet,
    std::uint16_t* opcode_out) noexcept {
    if (packet == nullptr || opcode_out == nullptr) {
        return false;
    }

#if defined(_MSC_VER)
    __try {
        std::memcpy(opcode_out, packet, sizeof(*opcode_out));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(opcode_out, packet, sizeof(*opcode_out));
#endif
    return true;
}

std::wstring FormatDiscoveryDetails(
    const wchar_t* target,
    const std::wstring& evidence_source,
    const std::wstring& failure_reason) {
    std::wstring message = target == nullptr ? L"target" : target;
    message += L" evidence_source=";
    message += evidence_source.empty() ? L"unknown" : evidence_source;
    message += L" failure_reason=";
    message += failure_reason.empty() ? L"unknown" : failure_reason;
    return message;
}

bool RelativeOffsetFits(std::int64_t offset) noexcept {
    return offset >= std::numeric_limits<std::int32_t>::min() &&
        offset <= std::numeric_limits<std::int32_t>::max();
}

bool BuildRelativeJump(
    const void* patch_site,
    const void* destination,
    std::array<std::uint8_t, kJmpPatchBytes>* patch) noexcept {
    if (patch_site == nullptr || destination == nullptr || patch == nullptr) {
        return false;
    }

    const auto site = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(patch_site));
    const auto dest = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(destination));
    const std::int64_t relative = dest - (site + static_cast<std::int64_t>(kJmpPatchBytes));
    if (!RelativeOffsetFits(relative)) {
        return false;
    }

    const auto relative32 = static_cast<std::int32_t>(relative);
    (*patch)[0] = 0xe9;
    std::memcpy(patch->data() + 1, &relative32, sizeof(relative32));
    return true;
}

bool BuildRelativeCall(
    const void* patch_site,
    const void* destination,
    std::array<std::uint8_t, kJmpPatchBytes>* patch) noexcept {
    if (patch_site == nullptr || destination == nullptr || patch == nullptr) {
        return false;
    }

    const auto site = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(patch_site));
    const auto dest = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(destination));
    const std::int64_t relative = dest - (site + static_cast<std::int64_t>(kJmpPatchBytes));
    if (!RelativeOffsetFits(relative)) {
        return false;
    }

    const auto relative32 = static_cast<std::int32_t>(relative);
    (*patch)[0] = 0xe8;
    std::memcpy(patch->data() + 1, &relative32, sizeof(relative32));
    return true;
}

bool DecodeModRmInstructionLength(
    const std::uint8_t* code,
    std::size_t available,
    std::size_t immediate_bytes,
    std::size_t* length) noexcept {
    if (code == nullptr || length == nullptr || available < 2) {
        return false;
    }

    std::size_t decoded = 2;
    const std::uint8_t modrm = code[1];
    const std::uint8_t mod = (modrm >> 6) & 0x03;
    const std::uint8_t rm = modrm & 0x07;

    if (mod != 0x03 && rm == 0x04) {
        if (available < decoded + 1) {
            return false;
        }

        const std::uint8_t sib = code[decoded];
        const std::uint8_t base = sib & 0x07;
        ++decoded;
        if (mod == 0x00 && base == 0x05) {
            decoded += 4;
        }
    } else if (mod == 0x00 && rm == 0x05) {
        decoded += 4;
    }

    if (mod == 0x01) {
        decoded += 1;
    } else if (mod == 0x02) {
        decoded += 4;
    }

    decoded += immediate_bytes;
    if (decoded > available) {
        return false;
    }

    *length = decoded;
    return true;
}

bool DecodeSupportedInstructionLength(
    const std::uint8_t* code,
    std::size_t available,
    std::size_t* length) noexcept {
    if (code == nullptr || length == nullptr || available == 0) {
        return false;
    }

    const std::uint8_t opcode = code[0];
    if (opcode == 0x64 || opcode == 0x65) {
        std::size_t prefixed_length = 0;
        if (available < 2 ||
            !DecodeSupportedInstructionLength(code + 1, available - 1, &prefixed_length)) {
            return false;
        }

        *length = 1 + prefixed_length;
        return true;
    }

    if ((opcode >= 0x40 && opcode <= 0x5f) || opcode == 0x90) {
        *length = 1;
        return true;
    }

    if (opcode == 0x68) {
        if (available < 5) {
            return false;
        }
        *length = 5;
        return true;
    }

    if (opcode == 0xa1 || opcode == 0xa3) {
        if (available < 5) {
            return false;
        }
        *length = 5;
        return true;
    }

    if (opcode == 0x6a) {
        if (available < 2) {
            return false;
        }
        *length = 2;
        return true;
    }

    if (opcode >= 0xb8 && opcode <= 0xbf) {
        if (available < 5) {
            return false;
        }
        *length = 5;
        return true;
    }

    switch (opcode) {
        case 0x81:
        case 0xc7:
            return DecodeModRmInstructionLength(code, available, 4, length);
        case 0x83:
            return DecodeModRmInstructionLength(code, available, 1, length);
        case 0x85:
        case 0x89:
        case 0x8b:
        case 0x8d:
            return DecodeModRmInstructionLength(code, available, 0, length);
        default:
            return false;
    }
}

bool CalculatePatchLength(const std::uint8_t* target, std::size_t* patch_length) noexcept {
    if (target == nullptr || patch_length == nullptr) {
        return false;
    }

    std::size_t decoded = 0;
    while (decoded < kJmpPatchBytes) {
        std::size_t instruction_length = 0;
        if (!DecodeSupportedInstructionLength(
                target + decoded,
                kMaxStolenBytes - decoded,
                &instruction_length)) {
            return false;
        }

        decoded += instruction_length;
        if (decoded > kMaxStolenBytes) {
            return false;
        }
    }

    *patch_length = decoded;
    return true;
}

bool InstallInlineDetour(
    void* target,
    void* hook,
    InlineDetour* detour,
    void** original_out,
    const wchar_t* failure_label) noexcept {
    if (target == nullptr || hook == nullptr || detour == nullptr ||
        original_out == nullptr || detour->installed) {
        return false;
    }

    auto* target_bytes = reinterpret_cast<std::uint8_t*>(target);
    std::size_t patch_length = 0;
    if (!CalculatePatchLength(target_bytes, &patch_length)) {
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" prologue unsupported; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    auto* trampoline = reinterpret_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        patch_length + kJmpPatchBytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" trampoline allocation failed; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    std::array<std::uint8_t, kJmpPatchBytes> trampoline_jump = {};
    if (!BuildRelativeJump(
            trampoline + patch_length,
            target_bytes + patch_length,
            &trampoline_jump)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" trampoline jump out of range; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    std::array<std::uint8_t, kJmpPatchBytes> target_jump = {};
    if (!BuildRelativeJump(target_bytes, hook, &target_jump)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" target jump out of range; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    std::memcpy(trampoline, target_bytes, patch_length);
    std::memcpy(trampoline + patch_length, trampoline_jump.data(), trampoline_jump.size());
    FlushInstructionCache(GetCurrentProcess(), trampoline, patch_length + kJmpPatchBytes);

    DWORD old_protect = 0;
    if (!VirtualProtect(target_bytes, patch_length, PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        *original_out = nullptr;
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" target memory protection failed; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    detour->target = target_bytes;
    detour->hook = hook;
    detour->trampoline = trampoline;
    detour->patch_length = patch_length;
    detour->patch = target_jump;
    *original_out = trampoline;

    std::memcpy(detour->original.data(), target_bytes, patch_length);
    std::memcpy(target_bytes, target_jump.data(), target_jump.size());
    for (std::size_t i = kJmpPatchBytes; i < patch_length; ++i) {
        target_bytes[i] = 0x90;
    }

    FlushInstructionCache(GetCurrentProcess(), target_bytes, patch_length);

    DWORD ignored = 0;
    VirtualProtect(target_bytes, patch_length, old_protect, &ignored);

    detour->installed = true;
    return true;
}

bool InstallValidatedInlineDetour(
    void* target,
    void* hook,
    std::size_t patch_length,
    InlineDetour* detour,
    void** original_out,
    const wchar_t* failure_label) noexcept {
    if (target == nullptr || hook == nullptr || detour == nullptr ||
        original_out == nullptr || detour->installed || patch_length < kJmpPatchBytes ||
        patch_length > kMaxStolenBytes) {
        return false;
    }

    auto* target_bytes = reinterpret_cast<std::uint8_t*>(target);
    auto* trampoline = reinterpret_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        patch_length + kJmpPatchBytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" trampoline allocation failed; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    std::array<std::uint8_t, kJmpPatchBytes> trampoline_jump = {};
    if (!BuildRelativeJump(
            trampoline + patch_length,
            target_bytes + patch_length,
            &trampoline_jump)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" trampoline jump out of range; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    std::array<std::uint8_t, kJmpPatchBytes> target_jump = {};
    if (!BuildRelativeJump(target_bytes, hook, &target_jump)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" target jump out of range; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    std::memcpy(trampoline, target_bytes, patch_length);
    std::memcpy(trampoline + patch_length, trampoline_jump.data(), trampoline_jump.size());
    FlushInstructionCache(GetCurrentProcess(), trampoline, patch_length + kJmpPatchBytes);

    DWORD old_protect = 0;
    if (!VirtualProtect(target_bytes, patch_length, PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        *original_out = nullptr;
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" target memory protection failed; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    detour->target = target_bytes;
    detour->hook = hook;
    detour->trampoline = trampoline;
    detour->patch_length = patch_length;
    detour->patch = target_jump;
    *original_out = trampoline;

    std::memcpy(detour->original.data(), target_bytes, patch_length);
    std::memcpy(target_bytes, target_jump.data(), target_jump.size());
    for (std::size_t i = kJmpPatchBytes; i < patch_length; ++i) {
        target_bytes[i] = 0x90;
    }

    FlushInstructionCache(GetCurrentProcess(), target_bytes, patch_length);

    DWORD ignored = 0;
    VirtualProtect(target_bytes, patch_length, old_protect, &ignored);

    detour->installed = true;
    return true;
}

bool InstallCallsitePatch(
    void* callsite,
    void* hook,
    std::uintptr_t expected_original_target,
    CallsitePatch* patch,
    const wchar_t* failure_label) noexcept {
    if (callsite == nullptr || hook == nullptr || patch == nullptr || patch->installed) {
        return false;
    }

    auto* callsite_bytes = reinterpret_cast<std::uint8_t*>(callsite);
    const RelativeCallResolution resolution =
        ResolveRelativeCall(callsite_bytes, kJmpPatchBytes, reinterpret_cast<std::uintptr_t>(callsite));
    if (!resolution.resolved || resolution.target != expected_original_target) {
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" callsite validation failed; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    std::array<std::uint8_t, kJmpPatchBytes> call_patch = {};
    if (!BuildRelativeCall(callsite, hook, &call_patch)) {
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" callsite out of range; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(callsite_bytes, kJmpPatchBytes, PAGE_EXECUTE_READWRITE, &old_protect)) {
        std::wstring message = L"hook_manager: ";
        message += failure_label;
        message += L" callsite memory protection failed; hook disabled";
        monomyth::logger::Log(message);
        return false;
    }

    patch->address = callsite_bytes;
    std::memcpy(patch->original.data(), callsite_bytes, kJmpPatchBytes);
    patch->patch = call_patch;
    std::memcpy(callsite_bytes, call_patch.data(), kJmpPatchBytes);
    FlushInstructionCache(GetCurrentProcess(), callsite_bytes, kJmpPatchBytes);

    DWORD ignored = 0;
    VirtualProtect(callsite_bytes, kJmpPatchBytes, old_protect, &ignored);

    patch->installed = true;
    std::wstring message = L"hook_manager: ";
    message += failure_label;
    message += L" callsite hook installed address=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(callsite));
    message += L" hook=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(hook));
    message += L" original_target=";
    message += HexPtr(expected_original_target);
    monomyth::logger::Log(message);
    return true;
}

bool RemoveInlineDetour(InlineDetour* detour) noexcept {
    if (detour == nullptr || !detour->installed) {
        return true;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(detour->target, detour->patch_length, PAGE_EXECUTE_READWRITE, &old_protect)) {
        monomyth::logger::Log(L"hook_manager: failed to make receive dispatcher writable for uninstall");
        return false;
    }

    std::memcpy(detour->target, detour->original.data(), detour->patch_length);
    FlushInstructionCache(GetCurrentProcess(), detour->target, detour->patch_length);

    DWORD ignored = 0;
    VirtualProtect(detour->target, detour->patch_length, old_protect, &ignored);

    if (detour->trampoline != nullptr) {
        VirtualFree(detour->trampoline, 0, MEM_RELEASE);
    }

    *detour = {};
    return true;
}

bool RemoveCallsitePatch(CallsitePatch* patch) noexcept {
    if (patch == nullptr || !patch->installed) {
        return true;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(patch->address, kJmpPatchBytes, PAGE_EXECUTE_READWRITE, &old_protect)) {
        monomyth::logger::Log(
            L"hook_manager: failed to make callsite writable for uninstall");
        return false;
    }

    std::memcpy(patch->address, patch->original.data(), kJmpPatchBytes);
    FlushInstructionCache(GetCurrentProcess(), patch->address, kJmpPatchBytes);

    DWORD ignored = 0;
    VirtualProtect(patch->address, kJmpPatchBytes, old_protect, &ignored);

    *patch = {};
    return true;
}

const char* MONOMYTH_FASTCALL GetClassDescHook(
    void* this_context,
    void*,
    unsigned int class_id) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const bool bypass_local_player_override =
        ShouldBypassLocalPlayerClassOverrideForCaller(caller_return_address);
    monomyth::packet_observer::WhoAllClassDisplayCorrelationWindow correlation = {};
    const bool correlation_active =
        monomyth::packet_observer::TryConsumeWhoAllClassDisplayCorrelation(&correlation);
    InventoryClassDisplayCorrelationWindow inventory_correlation = {};
    const bool inventory_correlation_active =
        TryConsumeInventoryClassDisplayCorrelation(&inventory_correlation);
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();

    if (bypass_local_player_override) {
        const char* cached_display = TryBuildCachedCharSelectFullNameDisplayAscii(
            class_id,
            L"GetClassDesc");
        if (cached_display != nullptr) {
            LogUiClassHelperTrace(
                L"GetClassDesc",
                caller_return_address,
                class_id,
                false,
                0,
                snapshot,
                true,
                cached_display,
                L"char_select_cached_full_name_display");
            return cached_display;
        }
    }

    monomyth::multiclass_identity::ClassDisplayStyle semantic_style =
        monomyth::multiclass_identity::ClassDisplayStyle::kFullName;
    const wchar_t* semantic_reason = nullptr;
    if (!bypass_local_player_override &&
        TryResolveSemanticClassDisplayStyleForCaller(
            caller_return_address,
            &semantic_style,
            &semantic_reason)) {
        const char* display = BuildPreferredLocalPlayerClassDisplayAscii(
            class_id,
            semantic_style,
            L"GetClassDesc");
        if (display != nullptr) {
            if (correlation_active) {
                LogWhoAllClassDisplayCorrelationTrace(
                    L"GetClassDesc",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    correlation,
                    nullptr,
                    true,
                    semantic_reason,
                    display);
            }
            if (inventory_correlation_active) {
                LogInventoryClassDisplayCorrelationTrace(
                    L"GetClassDesc",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    inventory_correlation,
                    nullptr,
                    true,
                    semantic_reason,
                    display);
            }
            LogUiClassHelperTrace(
                L"GetClassDesc",
                caller_return_address,
                class_id,
                true,
                static_cast<std::uint8_t>(class_id),
                snapshot,
                true,
                display,
                semantic_reason);
            if (semantic_reason != nullptr &&
                std::wcscmp(
                    semantic_reason,
                    L"known_local_self_three_letter_caller_rva") == 0) {
                ArmCharSelectNativeClassTextSubstitutionWindow();
            }
            return display;
        }
    }

    std::uint8_t local_class_id = 0;
    const bool local_class_copied = TryReadLocalPlayerDisplayedClassId(&local_class_id);
    if (!bypass_local_player_override &&
        local_class_copied &&
        class_id == local_class_id) {
        const char* display = BuildLocalPlayerClassDisplayAscii(
            monomyth::multiclass_identity::ClassDisplayStyle::kFullName,
            L"GetClassDesc");
        if (display != nullptr) {
            if (correlation_active) {
                LogWhoAllClassDisplayCorrelationTrace(
                    L"GetClassDesc",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    correlation,
                    nullptr,
                    true,
                    L"requested_class_matches_local_primary",
                    display);
            }
            if (inventory_correlation_active) {
                LogInventoryClassDisplayCorrelationTrace(
                    L"GetClassDesc",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    inventory_correlation,
                    nullptr,
                    true,
                    L"requested_class_matches_local_primary",
                    display);
            }
            LogUiClassHelperTrace(
                L"GetClassDesc",
                caller_return_address,
                class_id,
                local_class_copied,
                local_class_id,
                snapshot,
                true,
                display,
                L"requested_class_matches_local_primary");
            return display;
        }
    }

    const char* result = g_original_get_class_desc == nullptr
        ? nullptr
        : g_original_get_class_desc(this_context, class_id);
    const wchar_t* const fallback_reason =
        local_class_copied && class_id == local_class_id
            ? L"local_primary_match_but_override_unavailable"
            : (local_class_copied ? L"requested_class_does_not_match_local_primary"
                                  : L"local_primary_unavailable");
    if (correlation_active) {
        LogWhoAllClassDisplayCorrelationTrace(
            L"GetClassDesc",
            L"class_id",
            caller_return_address,
            class_id,
            correlation,
            nullptr,
            false,
            fallback_reason,
            result);
    }
    if (inventory_correlation_active) {
        LogInventoryClassDisplayCorrelationTrace(
            L"GetClassDesc",
            L"class_id",
            caller_return_address,
            class_id,
            inventory_correlation,
            nullptr,
            false,
            fallback_reason,
            result);
    }
    LogUiClassHelperTrace(
        L"GetClassDesc",
        caller_return_address,
        class_id,
        local_class_copied,
        local_class_id,
        snapshot,
        false,
        result,
        fallback_reason);
    return result;
}

const char* MONOMYTH_FASTCALL GetClassThreeLetterCodeHook(
    void* this_context,
    void*,
    unsigned int class_id) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    monomyth::packet_observer::WhoAllClassDisplayCorrelationWindow correlation = {};
    const bool correlation_active =
        monomyth::packet_observer::TryConsumeWhoAllClassDisplayCorrelation(&correlation);
    InventoryClassDisplayCorrelationWindow inventory_correlation = {};
    const bool inventory_correlation_active =
        TryConsumeInventoryClassDisplayCorrelation(&inventory_correlation);
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();

    monomyth::multiclass_identity::ClassDisplayStyle semantic_style =
        monomyth::multiclass_identity::ClassDisplayStyle::kThreeLetterCode;
    const wchar_t* semantic_reason = nullptr;
    if (TryResolveSemanticClassDisplayStyleForCaller(
            caller_return_address,
            &semantic_style,
            &semantic_reason)) {
        const char* display = BuildPreferredLocalPlayerClassDisplayAscii(
            class_id,
            semantic_style,
            L"GetClassThreeLetterCode");
        if (display != nullptr) {
            if (correlation_active) {
                LogWhoAllClassDisplayCorrelationTrace(
                    L"GetClassThreeLetterCode",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    correlation,
                    nullptr,
                    true,
                    semantic_reason,
                    display);
            }
            if (inventory_correlation_active) {
                LogInventoryClassDisplayCorrelationTrace(
                    L"GetClassThreeLetterCode",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    inventory_correlation,
                    nullptr,
                    true,
                    semantic_reason,
                    display);
            }
            LogUiClassHelperTrace(
                L"GetClassThreeLetterCode",
                caller_return_address,
                class_id,
                true,
                static_cast<std::uint8_t>(class_id),
                snapshot,
                true,
                display,
                semantic_reason);
            return display;
        }
    }

    std::uint8_t local_class_id = 0;
    const bool local_class_copied = TryReadLocalPlayerDisplayedClassId(&local_class_id);
    if (local_class_copied && class_id == local_class_id) {
        const char* display = BuildLocalPlayerClassDisplayAscii(
            monomyth::multiclass_identity::ClassDisplayStyle::kThreeLetterCode,
            L"GetClassThreeLetterCode");
        if (display != nullptr) {
            if (correlation_active) {
                LogWhoAllClassDisplayCorrelationTrace(
                    L"GetClassThreeLetterCode",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    correlation,
                    nullptr,
                    true,
                    L"requested_class_matches_local_primary",
                    display);
            }
            if (inventory_correlation_active) {
                LogInventoryClassDisplayCorrelationTrace(
                    L"GetClassThreeLetterCode",
                    L"class_id",
                    caller_return_address,
                    class_id,
                    inventory_correlation,
                    nullptr,
                    true,
                    L"requested_class_matches_local_primary",
                    display);
            }
            LogUiClassHelperTrace(
                L"GetClassThreeLetterCode",
                caller_return_address,
                class_id,
                local_class_copied,
                local_class_id,
                snapshot,
                true,
                display,
                L"requested_class_matches_local_primary");
            return display;
        }
    }

    const char* result = g_original_get_class_three_letter_code == nullptr
        ? nullptr
        : g_original_get_class_three_letter_code(this_context, class_id);
    const wchar_t* const fallback_reason =
        local_class_copied && class_id == local_class_id
            ? L"local_primary_match_but_override_unavailable"
            : (local_class_copied ? L"requested_class_does_not_match_local_primary"
                                  : L"local_primary_unavailable");
    if (correlation_active) {
        LogWhoAllClassDisplayCorrelationTrace(
            L"GetClassThreeLetterCode",
            L"class_id",
            caller_return_address,
            class_id,
            correlation,
            nullptr,
            false,
            fallback_reason,
            result);
    }
    if (inventory_correlation_active) {
        LogInventoryClassDisplayCorrelationTrace(
            L"GetClassThreeLetterCode",
            L"class_id",
            caller_return_address,
            class_id,
            inventory_correlation,
            nullptr,
            false,
            fallback_reason,
            result);
    }
    LogUiClassHelperTrace(
        L"GetClassThreeLetterCode",
        caller_return_address,
        class_id,
        local_class_copied,
        local_class_id,
        snapshot,
        false,
        result,
        fallback_reason);
    return result;
}

const char* CDECL GetDeityDescHook(unsigned int deity_id) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    InventoryClassDisplayCorrelationWindow inventory_correlation = {};
    const bool inventory_correlation_active =
        TryConsumeInventoryClassDisplayCorrelation(&inventory_correlation);
    const char* result = g_original_get_deity_desc == nullptr
        ? nullptr
        : g_original_get_deity_desc(deity_id);
    if (inventory_correlation_active) {
        LogInventoryClassDisplayCorrelationTrace(
            L"GetDeityDesc",
            L"deity_id",
            caller_return_address,
            deity_id,
            inventory_correlation,
            nullptr,
            false,
            L"inventory_summary_deity_observed",
            result);
    }
    return result;
}

const char* CDECL ProgressionSelectionClassLookupCallsiteHook(
    unsigned int class_id) noexcept {
    const std::uint64_t count = ++g_progression_selection_trace_count;
    const char* display = BuildLocalPlayerClassDisplayAscii(
        monomyth::multiclass_identity::ClassDisplayStyle::kThreeLetterCode,
        L"ProgressionSelection");
    if (display != nullptr) {
        if (ShouldLogProgressionSelectionTrace(count)) {
            const monomyth::server_auth_stats::Snapshot snapshot =
                monomyth::server_auth_stats::GetSnapshot();
            std::wstring message = L"ProgressionSelectionTrace count=";
            message += std::to_wstring(count);
            message += L" class_id=";
            message += std::to_wstring(class_id);
            message += L" override_applied=true formatted=\"";
            message += WidenAsciiLossy(display);
            message += L"\" assigned_mask=";
            message += FormatAssignedMask(snapshot);
            message += L" has_assigned_mask=";
            message += snapshot.has_classes_bitmask ? L"true" : L"false";
            monomyth::logger::Log(message);
        }
        return display;
    }

    if (g_original_progression_selection_class_lookup == nullptr) {
        if (ShouldLogProgressionSelectionTrace(count)) {
            std::wstring message = L"ProgressionSelectionTrace count=";
            message += std::to_wstring(count);
            message += L" class_id=";
            message += std::to_wstring(class_id);
            message += L" override_applied=false reason=\"original_lookup_unavailable\"";
            monomyth::logger::Log(message);
        }
        return nullptr;
    }

    const char* result = g_original_progression_selection_class_lookup(class_id);
    if (ShouldLogProgressionSelectionTrace(count)) {
        const monomyth::server_auth_stats::Snapshot snapshot =
            monomyth::server_auth_stats::GetSnapshot();
        std::wstring message = L"ProgressionSelectionTrace count=";
        message += std::to_wstring(count);
        message += L" class_id=";
        message += std::to_wstring(class_id);
        message += L" override_applied=false";
        if (result != nullptr && result[0] != '\0') {
            message += L" formatted=\"";
            message += WidenAsciiLossy(result);
            message += L"\"";
        }
        message += L" assigned_mask=";
        message += FormatAssignedMask(snapshot);
        message += L" has_assigned_mask=";
        message += snapshot.has_classes_bitmask ? L"true" : L"false";
        monomyth::logger::Log(message);
    }
    return result;
}

const char* InvokeCharSelectLateFullNameCallsiteHook(
    const wchar_t* candidate,
    void* this_context,
    unsigned int class_id) noexcept {
    const std::uint64_t count = ++g_char_select_late_full_name_trace_count;
    const char* display = BuildPreferredLocalPlayerClassDisplayAscii(
        class_id,
        monomyth::multiclass_identity::ClassDisplayStyle::kFullName,
        candidate);
    if (display != nullptr) {
        if (ShouldLogCharSelectLateFullNameTrace(count)) {
            LogCharSelectLateFullNameTrace(
                candidate,
                count,
                this_context,
                class_id,
                true,
                display,
                L"preferred_primary_multiclass_display_applied");
        }
        return display;
    }

    const char* result = g_original_get_class_desc == nullptr
        ? nullptr
        : g_original_get_class_desc(this_context, class_id);
    if (ShouldLogCharSelectLateFullNameTrace(count)) {
        LogCharSelectLateFullNameTrace(
            candidate,
            count,
            this_context,
            class_id,
            false,
            result,
            g_original_get_class_desc == nullptr
                ? L"original_get_class_desc_unavailable"
                : L"fallback_to_original_get_class_desc");
    }
    return result;
}

const char* MONOMYTH_FASTCALL CharSelectLateFullNameCallsiteAHook(
    void* this_context,
    void*,
    unsigned int class_id) noexcept {
    return InvokeCharSelectLateFullNameCallsiteHook(
        L"CharSelectLateFullNameCallsiteA",
        this_context,
        class_id);
}

const char* MONOMYTH_FASTCALL CharSelectLateFullNameCallsiteBHook(
    void* this_context,
    void*,
    unsigned int class_id) noexcept {
    return InvokeCharSelectLateFullNameCallsiteHook(
        L"CharSelectLateFullNameCallsiteB",
        this_context,
        class_id);
}

const char* MONOMYTH_FASTCALL CharSelectLateFullNameCallsiteCHook(
    void* this_context,
    void*,
    unsigned int class_id) noexcept {
    return InvokeCharSelectLateFullNameCallsiteHook(
        L"CharSelectLateFullNameCallsiteC",
        this_context,
        class_id);
}

void MONOMYTH_FASTCALL CharSelectClassNameFuncHook(
    void* this_context,
    void*,
    unsigned int selected_index) noexcept {
    const std::uint64_t count = ++g_char_select_class_name_func_trace_count;
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::uintptr_t field_22c_before = 0;
    std::uintptr_t field_248_before = 0;
    std::uintptr_t field_24c_before = 0;
    const bool field_22c_before_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x22c, &field_22c_before);
    const bool field_248_before_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x248, &field_248_before);
    const bool field_24c_before_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x24c, &field_24c_before);
    if (ShouldLogCharSelectClassNameFuncTrace(count)) {
        LogCharSelectClassNameFuncTrace(
            L"before",
            count,
            this_context,
            selected_index,
            caller_return_address,
            field_22c_before,
            field_22c_before_copied,
            field_248_before,
            field_248_before_copied,
            field_24c_before,
            field_24c_before_copied);
    }

    if (g_original_char_select_class_name_func != nullptr) {
        g_original_char_select_class_name_func(this_context, selected_index);
    }

    if (!ShouldLogCharSelectClassNameFuncTrace(count)) {
        return;
    }

    std::uintptr_t field_22c_after = 0;
    std::uintptr_t field_248_after = 0;
    std::uintptr_t field_24c_after = 0;
    const bool field_22c_after_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x22c, &field_22c_after);
    const bool field_248_after_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x248, &field_248_after);
    const bool field_24c_after_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x24c, &field_24c_after);
    LogCharSelectClassNameFuncTrace(
        L"after",
        count,
        this_context,
        selected_index,
        caller_return_address,
        field_22c_after,
        field_22c_after_copied,
        field_248_after,
        field_248_after_copied,
        field_24c_after,
        field_24c_after_copied);
}

bool IsWhoClassNameLookupCaller(std::uintptr_t caller_return_address) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0 || caller_return_address < module_base) {
        return false;
    }

    const std::uint32_t caller_return_rva =
        static_cast<std::uint32_t>(caller_return_address - module_base);
    return caller_return_rva == kWhoClassNameClassLookupCallerReturnARva ||
           caller_return_rva == kWhoClassNameClassLookupCallerReturnBRva ||
           caller_return_rva == kWhoClassNameClassLookupCallerReturnCRva;
}

void MONOMYTH_FASTCALL WhoClassNameHook(
    void* this_context,
    void*,
    void* subject) noexcept {
    const void* const previous_subject = g_active_who_class_name_subject;
    g_active_who_class_name_subject = subject;
    LogWhoClassNameEntryTrace(this_context, subject);
    if (g_original_who_class_name != nullptr) {
        g_original_who_class_name(this_context, subject);
    }
    g_active_who_class_name_subject = previous_subject;
}

const char* MONOMYTH_FASTCALL WhoClassNameClassLookupHook(
    void* this_context,
    void*,
    std::uint32_t string_id,
    void* found_flag_out) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_return_rva =
        module_base != 0 && caller_return_address >= module_base
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    monomyth::packet_observer::WhoAllClassDisplayCorrelationWindow correlation = {};
    const bool correlation_active =
        monomyth::packet_observer::TryConsumeWhoAllClassDisplayCorrelation(&correlation);
    ItemDisplayClassDisplayCorrelationWindow item_display_correlation = {};
    const bool item_display_correlation_active =
        IsLikelyItemDisplayStringLookupCaller(caller_return_rva) &&
        TryConsumeItemDisplayClassDisplayCorrelation(&item_display_correlation);
    const bool who_all_class_label_caller =
        caller_return_rva == kWhoAllClassLabelLookupCallerRva;
    const bool caller_matches = IsWhoClassNameLookupCaller(caller_return_address);
    InventoryClassDisplayCorrelationWindow inventory_correlation = {};
    const bool inventory_correlation_active =
        (who_all_class_label_caller || caller_matches) &&
        TryConsumeInventoryClassDisplayCorrelation(&inventory_correlation);
    if (who_all_class_label_caller && correlation_active) {
        const char* display = BuildLocalPlayerClassDisplayAscii(
            monomyth::multiclass_identity::ClassDisplayStyle::kFullName,
            L"WhoClassNameClassLookup");
        if (display != nullptr) {
            if (found_flag_out != nullptr) {
                *reinterpret_cast<std::uint8_t*>(found_flag_out) = 1;
            }
            LogWhoAllClassDisplayCorrelationTrace(
                L"WhoClassNameClassLookup",
                L"string_id",
                caller_return_address,
                string_id,
                correlation,
                nullptr,
                true,
                L"who_all_class_label_local_player_display_applied",
                display);
            if (inventory_correlation_active) {
                LogInventoryClassDisplayCorrelationTrace(
                    L"WhoClassNameClassLookup",
                    L"string_id",
                    caller_return_address,
                    string_id,
                    inventory_correlation,
                    nullptr,
                    true,
                    L"who_all_class_label_local_player_display_applied",
                    display);
            }
            return display;
        }

        LogWhoAllClassDisplayCorrelationTrace(
            L"WhoClassNameClassLookup",
            L"string_id",
            caller_return_address,
            string_id,
            correlation,
            nullptr,
            false,
            L"who_all_class_label_override_unavailable",
            nullptr);
        if (inventory_correlation_active) {
            LogInventoryClassDisplayCorrelationTrace(
                L"WhoClassNameClassLookup",
                L"string_id",
                caller_return_address,
                string_id,
                inventory_correlation,
                nullptr,
                false,
                L"who_all_class_label_override_unavailable",
                nullptr);
        }
    }

    if (caller_matches) {
        const char* display = BuildLocalSubjectClassDisplayAscii(
            g_active_who_class_name_subject,
            monomyth::multiclass_identity::ClassDisplayStyle::kFullName,
            L"WhoClassName");
        if (display != nullptr) {
            if (found_flag_out != nullptr) {
                *reinterpret_cast<std::uint8_t*>(found_flag_out) = 1;
            }
            if (correlation_active) {
                LogWhoAllClassDisplayCorrelationTrace(
                    L"WhoClassNameClassLookup",
                    L"string_id",
                    caller_return_address,
                    string_id,
                    correlation,
                    g_active_who_class_name_subject,
                    true,
                    L"local_subject_multiclass_display_applied",
                    display);
            }
            if (inventory_correlation_active) {
                LogInventoryClassDisplayCorrelationTrace(
                    L"WhoClassNameClassLookup",
                    L"string_id",
                    caller_return_address,
                    string_id,
                    inventory_correlation,
                    g_active_who_class_name_subject,
                    true,
                    L"local_subject_multiclass_display_applied",
                    display);
            }
            return display;
        }
        if (correlation_active) {
            LogWhoAllClassDisplayCorrelationTrace(
                L"WhoClassNameClassLookup",
                L"string_id",
                caller_return_address,
                string_id,
                correlation,
                g_active_who_class_name_subject,
                false,
                L"caller_matched_but_override_unavailable",
                nullptr);
        }
        if (inventory_correlation_active) {
            LogInventoryClassDisplayCorrelationTrace(
                L"WhoClassNameClassLookup",
                L"string_id",
                caller_return_address,
                string_id,
                inventory_correlation,
                g_active_who_class_name_subject,
                false,
                L"caller_matched_but_override_unavailable",
                nullptr);
        }
    }

    if (g_original_who_class_name_class_lookup == nullptr) {
        if (correlation_active) {
            LogWhoAllClassDisplayCorrelationTrace(
                L"WhoClassNameClassLookup",
                L"string_id",
                caller_return_address,
                string_id,
                correlation,
                g_active_who_class_name_subject,
                false,
                L"original_lookup_unavailable",
                nullptr);
        }
        if (inventory_correlation_active) {
            LogInventoryClassDisplayCorrelationTrace(
                L"WhoClassNameClassLookup",
                L"string_id",
                caller_return_address,
                string_id,
                inventory_correlation,
                g_active_who_class_name_subject,
                false,
                L"original_lookup_unavailable",
                nullptr);
        }
        if (item_display_correlation_active) {
            LogItemDisplayClassDisplayTrace(
                L"ItemDisplaySharedStringLookup",
                L"string_id",
                caller_return_address,
                string_id,
                item_display_correlation,
                false,
                L"original_lookup_unavailable",
                nullptr);
        }
        return nullptr;
    }

    const char* result =
        g_original_who_class_name_class_lookup(this_context, string_id, found_flag_out);
    if (correlation_active) {
        LogWhoAllClassDisplayCorrelationTrace(
            L"WhoClassNameClassLookup",
            L"string_id",
            caller_return_address,
            string_id,
            correlation,
            g_active_who_class_name_subject,
            false,
            who_all_class_label_caller
                ? L"who_all_class_label_fell_back_to_original"
                : (caller_matches ? L"caller_matched_but_fell_back_to_original"
                                  : L"caller_did_not_match_who_class_name_branch"),
            result);
    }
    if (inventory_correlation_active) {
        LogInventoryClassDisplayCorrelationTrace(
            L"WhoClassNameClassLookup",
            L"string_id",
            caller_return_address,
            string_id,
            inventory_correlation,
            g_active_who_class_name_subject,
            false,
            who_all_class_label_caller
                ? L"who_all_class_label_fell_back_to_original"
                : (caller_matches ? L"caller_matched_but_fell_back_to_original"
                                  : L"caller_did_not_match_who_class_name_branch"),
            result);
    }
    if (item_display_correlation_active) {
        LogItemDisplayClassDisplayTrace(
            L"ItemDisplaySharedStringLookup",
            L"string_id",
            caller_return_address,
            string_id,
            item_display_correlation,
            false,
            L"item_display_refresh_window_lookup_observed",
            result);
    }
    return result;
}

void* MONOMYTH_FASTCALL CXStrAssignHook(
    void* this_context,
    void*,
    void* src_cxstr_temp) noexcept {
    void* const original_result =
        g_original_cxstr_assign == nullptr
            ? nullptr
            : g_original_cxstr_assign(this_context, src_cxstr_temp);

    void* class_title_control = nullptr;
    if (g_multiclass_ui_display_enabled &&
        g_original_cxwnd_set_window_text_a != nullptr &&
        TryReadInventoryClassTitleControlPointer(&class_title_control) &&
        class_title_control != nullptr &&
        reinterpret_cast<const std::uint8_t*>(class_title_control) + kCxWndTextFieldOffset ==
            this_context) {
        const char* display = BuildLocalPlayerClassDisplayAscii(
            monomyth::multiclass_identity::ClassDisplayStyle::kFullName,
            L"InventoryClassTitle1");
        if (display != nullptr && display[0] != '\0') {
            g_original_cxwnd_set_window_text_a(class_title_control, display);

            const std::uint64_t count = ++g_inventory_class_title_trace_count;
            if (count <= 20 || (count % 100) == 0) {
                std::wstring message = L"InventoryClassTitleTrace count=";
                message += std::to_wstring(count);
                message += L" control=";
                message += HexPtr(reinterpret_cast<std::uintptr_t>(class_title_control));
                message += L" text_field=";
                message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
                message += L" formatted=\"";
                message += WidenAsciiLossy(display);
                message += L"\"";
                monomyth::logger::Log(message);
            }
        }
    }

    return original_result;
}

bool TryConsumeCharSelectNativeClassSubstitutionWindow(
    std::uint32_t* remaining_before,
    std::uint32_t* remaining_after,
    std::uint64_t* activation) noexcept {
    if (remaining_before == nullptr || remaining_after == nullptr || activation == nullptr) {
        return false;
    }

    std::uint32_t current = g_char_select_native_class_substitution_remaining.load();
    while (current != 0) {
        if (g_char_select_native_class_substitution_remaining.compare_exchange_weak(
                current,
                current - 1)) {
            *remaining_before = current;
            *remaining_after = current - 1;
            *activation = g_char_select_native_class_substitution_activation.load();
            return true;
        }
    }

    return false;
}

void MONOMYTH_FASTCALL CXWndSetWindowTextAHook(
    void* this_context,
    void*,
    const char* text) noexcept {
    std::uint32_t remaining_before = 0;
    std::uint32_t remaining_after = 0;
    std::uint64_t activation = 0;
    const bool substitution_window_active =
        TryConsumeCharSelectNativeClassSubstitutionWindow(
            &remaining_before,
            &remaining_after,
            &activation);

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::uint8_t local_class_id = 0;
    const bool local_class_copied = TryReadLocalPlayerDisplayedClassId(&local_class_id);
    const char* native_class_name =
        local_class_copied && monomyth::multiclass_identity::IsPlayableClassId(local_class_id)
        ? monomyth::multiclass_identity::ClassDisplayTokenAscii(
              local_class_id,
              monomyth::multiclass_identity::ClassDisplayStyle::kFullName)
        : nullptr;
    const char* replacement = nullptr;
    const bool native_text_matched =
        substitution_window_active &&
        text != nullptr &&
        native_class_name != nullptr &&
        native_class_name[0] != '\0' &&
        std::strcmp(text, native_class_name) == 0;
    if (native_text_matched) {
        replacement = BuildLocalPlayerClassDisplayAscii(
            monomyth::multiclass_identity::ClassDisplayStyle::kThreeLetterCode,
            L"CharSelectNativeClassTextSubstitute");
    }

    std::string cached_caption_replacement;
    std::string cached_character_name;
    std::uint8_t cached_native_class_id = 0;
    std::uint32_t cached_mask = 0;
    const bool cached_caption_substituted =
        replacement == nullptr &&
        text != nullptr &&
        TryBuildCachedCharSelectCaptionReplacement(
            text,
            &cached_caption_replacement,
            &cached_character_name,
            &cached_native_class_id,
            &cached_mask);
    if (cached_caption_substituted) {
        replacement = cached_caption_replacement.c_str();
    }

    const char* applied_text =
        (replacement != nullptr && replacement[0] != '\0') ? replacement : text;
    if (g_original_cxwnd_set_window_text_a != nullptr) {
        g_original_cxwnd_set_window_text_a(this_context, applied_text);
    }

    if (!substitution_window_active && !cached_caption_substituted) {
        return;
    }

    if (cached_caption_substituted) {
        std::wstring cache_message = L"CharSelectCachedClassCaptionTrace name=\"";
        cache_message += WidenAsciiLossy(cached_character_name);
        cache_message += L"\" native_class_id=";
        cache_message += std::to_wstring(cached_native_class_id);
        cache_message += L" cached_mask=";
        cache_message += Hex32(cached_mask);
        cache_message += L" original=\"";
        cache_message += WidenAsciiLossy(text == nullptr ? "" : text);
        cache_message += L"\" replacement=\"";
        cache_message += WidenAsciiLossy(cached_caption_replacement);
        cache_message += L"\"";
        monomyth::logger::Log(cache_message);
    }

    if (!substitution_window_active) {
        return;
    }

    const std::uint64_t count = ++g_char_select_native_class_substitution_trace_count;
    if (count > 20 && (count % 50) != 0 && replacement == nullptr) {
        return;
    }

    std::wstring message = L"CharSelectNativeClassSubstitutionTrace count=";
    message += std::to_wstring(count);
    message += L" activation=";
    message += std::to_wstring(activation);
    message += L" control=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" remaining_before=";
    message += std::to_wstring(remaining_before);
    message += L" remaining_after=";
    message += std::to_wstring(remaining_after);
    message += L" text=\"";
    message += WidenAsciiLossy(text == nullptr ? "" : text);
    message += L"\"";
    message += L" local_class_copied=";
    message += local_class_copied ? L"true" : L"false";
    message += L" local_class_id=";
    message += std::to_wstring(local_class_id);
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    message += L" substituted=";
    message += (replacement != nullptr && replacement[0] != '\0') ? L"true" : L"false";
    if (native_class_name != nullptr && native_class_name[0] != '\0') {
        message += L" native_class_name=\"";
        message += WidenAsciiLossy(native_class_name);
        message += L"\"";
    }
    if (replacement != nullptr && replacement[0] != '\0') {
        message += L" replacement=\"";
        message += WidenAsciiLossy(replacement);
        message += L"\"";
    }
    monomyth::logger::Log(message);

}
void MONOMYTH_FASTCALL ReceiveDispatchHook(
    void* this_context,
    void*,
    void* source_context,
    std::uint32_t opcode,
    const void* payload,
    std::uint32_t payload_length) noexcept {
    monomyth::packet_observer::ObserveReceiveMetadata(
        opcode,
        payload_length,
        payload,
        reinterpret_cast<std::uintptr_t>(source_context));

    g_original_receive_dispatch(this_context, source_context, opcode, payload, payload_length);
}

bool ShouldLogSpellTrace(std::uint64_t count) noexcept {
    return count <= kSpellTraceInitialLogCount || (count % kSpellTraceLogInterval) == 0;
}

bool ShouldLogScrollScribeTrace(std::uint64_t count) noexcept {
    constexpr std::uint64_t kInitialLogCount = 100;
    constexpr std::uint64_t kLogInterval = 25;
    return count <= kInitialLogCount || (count % kLogInterval) == 0;
}

std::wstring FormatAssignedMask(const monomyth::server_auth_stats::Snapshot& snapshot) {
    return Hex32(snapshot.has_classes_bitmask ? snapshot.classes_bitmask : 0);
}

bool NormalizeGuildTrainerClassId(unsigned int trainer_class_id, unsigned int* normalized_class_id) noexcept {
    if (normalized_class_id == nullptr) {
        return false;
    }

    unsigned int normalized = trainer_class_id;
    if (trainer_class_id >= 20 && trainer_class_id <= 35) {
        normalized = trainer_class_id - 19;
    }

    if (!monomyth::multiclass_identity::IsPlayableClassId(normalized)) {
        return false;
    }

    *normalized_class_id = normalized;
    return true;
}

std::uint8_t MONOMYTH_FASTCALL DragDropSilentPrecheckCallsiteHook(
    void* this_context,
    void*,
    void* probe_like) noexcept;
void MONOMYTH_FASTCALL MoveItemFromSlotResolveCallsiteHook(
    void* this_context,
    void*,
    void* slot_like,
    void* resolved_slot_like) noexcept;
void MONOMYTH_FASTCALL MoveItemToSlotResolveCallsiteHook(
    void* this_context,
    void*,
    void* slot_like,
    void* resolved_slot_like) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCorePrecheckCallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsACallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsBCallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsCCallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsDCallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsECallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLocalGateCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer) noexcept;
void* MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolveObjectCallsiteHook(
    void* this_context,
    void*,
    void* output_pointer) noexcept;
int MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedKindACallsiteHook(
    void* this_context,
    void*) noexcept;
int MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedKindBCallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedFlagACallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedFlagBCallsiteHook(
    void* this_context,
    void*) noexcept;
std::uint32_t WINAPI InvSlotHandleLButtonCorePostResolveGateCallsiteHook(
    void* slot_context_like,
    std::uint32_t mode_like) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCorePostLookupModeGateCallsiteHook(
    void* this_context,
    void*) noexcept;
void MONOMYTH_FASTCALL InvSlotHandleLButtonCorePostLookupUiPulseCallsiteHook(
    void* this_context,
    void*,
    std::uint32_t target_id,
    std::int32_t value_a,
    std::int32_t value_b,
    std::int32_t x,
    std::int32_t y,
    std::uint32_t one_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b) noexcept;
void MONOMYTH_FASTCALL InvSlotHandleLButtonCoreAltDispatchCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer,
    std::uint32_t mode_flag) noexcept;
std::uint8_t CDECL InvSlotHandleLButtonCoreItemRangeGateCallsiteHook(
    std::uint32_t item_id_like) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer) noexcept;
void MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateSlot17ApplyCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateBranchGateACallsiteHook(
    void* slot_record_like,
    void*) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateBranchPrepCallsiteHook(
    void* this_context,
    void*,
    std::int32_t slot_like,
    void* lookup_result_like) noexcept;
std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateBranchDispatchCallsiteHook(
    void* this_context,
    void*,
    void* lookup_result_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b) noexcept;

void InstallDetailedItemUsabilityTraceCallsites(
    const monomyth::runtime::Manifest& manifest,
    std::uintptr_t module_base) noexcept {
    if (module_base == 0) {
        return;
    }

    g_original_equip_record_lookup = reinterpret_cast<EquipRecordLookupFn>(
        module_base + kEquipRecordLookupTargetRva);
    g_original_equip_requirement_lookup = reinterpret_cast<EquipRequirementLookupFn>(
        module_base + kEquipRequirementLookupTargetRva);
    g_original_equip_nested_inventory_gate = reinterpret_cast<EquipNestedInventoryGateFn>(
        module_base + kEquipNestedInventoryGateTargetRva);
    g_original_equip_nested_validator = reinterpret_cast<EquipNestedValidatorFn>(
        module_base + kEquipNestedValidatorTargetRva);
    g_original_equip_message_resolver = reinterpret_cast<EquipMessageResolverFn>(
        module_base + kEquipMessageResolverTargetRva);
    g_original_drag_drop_silent_precheck = reinterpret_cast<DragDropSilentPrecheckFn>(
        module_base + kDragDropSilentPrecheckTargetRva);
    g_original_move_item_slot_resolve = reinterpret_cast<MoveItemSlotResolveFn>(
        module_base + kMoveItemSlotResolveTargetRva);
    g_original_move_item_branch_kind = reinterpret_cast<MoveItemBranchKindFn>(
        module_base + kMoveItemBranchKindTargetRva);
    g_original_move_item_branch_bool = reinterpret_cast<MoveItemBranchBoolFn>(
        module_base + kMoveItemBranchBoolTargetRva);
    g_original_move_item_stack_local_gate = reinterpret_cast<MoveItemStackLocalGateFn>(
        module_base + kMoveItemStackLocalGateTargetRva);
    g_original_invslot_handle_lbutton_core_precheck =
        reinterpret_cast<InvSlotHandleLButtonCorePrecheckFn>(
            module_base + kInvSlotHandleLButtonCorePrecheckTargetRva);
    g_original_invslot_handle_lbutton_core_mode_bits =
        reinterpret_cast<InvSlotHandleLButtonCoreModeBitsFn>(
            module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva);
    g_original_invslot_handle_lbutton_core_local_gate =
        reinterpret_cast<InvSlotHandleLButtonCoreLocalGateFn>(
            module_base + kInvSlotHandleLButtonCoreLocalGateTargetRva);
    g_original_invslot_handle_lbutton_core_resolve_object =
        reinterpret_cast<InvSlotHandleLButtonCoreResolveObjectFn>(
            module_base + kInvSlotHandleLButtonCoreResolveObjectTargetRva);
    g_original_invslot_handle_lbutton_core_resolved_kind =
        reinterpret_cast<MoveItemBranchKindFn>(
            module_base + kInvSlotHandleLButtonCoreResolvedKindTargetRva);
    g_original_invslot_handle_lbutton_core_resolved_flag =
        reinterpret_cast<InvSlotHandleLButtonCoreResolvedFlagFn>(
            module_base + kInvSlotHandleLButtonCoreResolvedFlagTargetRva);
    g_original_invslot_handle_lbutton_core_alt_dispatch =
        reinterpret_cast<InvSlotHandleLButtonCoreAltDispatchFn>(
            module_base + kInvSlotHandleLButtonCoreAltDispatchTargetRva);
    g_original_invslot_handle_lbutton_core_slot17_message =
        reinterpret_cast<InvSlotHandleLButtonCoreSlot17MessageFn>(
            module_base + kInvSlotHandleLButtonCoreSlot17MessageTargetRva);
    g_original_invslot_handle_lbutton_core_global_action49 =
        reinterpret_cast<InvSlotHandleLButtonCoreGlobalAction49Fn>(
            module_base + kInvSlotHandleLButtonCoreGlobalAction49TargetRva);
    g_original_invslot_handle_lbutton_core_item_range_gate =
        reinterpret_cast<InvSlotHandleLButtonCoreItemRangeGateFn>(
            module_base + kInvSlotHandleLButtonCoreItemRangeGateTargetRva);
    g_original_invslot_handle_lbutton_core_post_resolve_gate =
        reinterpret_cast<InvSlotHandleLButtonCorePostResolveGateFn>(
            module_base + kInvSlotHandleLButtonCorePostResolveGateTargetRva);
    g_original_invslot_handle_lbutton_core_post_lookup_mode_gate =
        reinterpret_cast<InvSlotHandleLButtonCorePostLookupModeGateFn>(
            module_base + kInvSlotHandleLButtonCorePostLookupModeGateTargetRva);
    g_original_invslot_handle_lbutton_core_post_lookup_ui_pulse =
        reinterpret_cast<InvSlotHandleLButtonCorePostLookupUiPulseFn>(
            module_base + kInvSlotHandleLButtonCorePostLookupUiPulseTargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_gate_a =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchGateAFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchGateATargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_prep =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchPrepFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchPrepTargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_gate_b =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchGateBFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchGateBTargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_dispatch =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchDispatchFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchDispatchTargetRva);
    g_original_invslot_handle_lbutton_core_late_slot17_gate =
        reinterpret_cast<InvSlotHandleLButtonCoreLateSlot17GateFn>(
            module_base + kInvSlotHandleLButtonCoreLateSlot17GateTargetRva);
    g_original_invslot_handle_lbutton_core_late_slot17_apply =
        reinterpret_cast<InvSlotHandleLButtonCoreLateSlot17ApplyFn>(
            module_base + kInvSlotHandleLButtonCoreLateSlot17ApplyTargetRva);

    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kDragDropSilentPrecheckCallsiteRva),
        reinterpret_cast<void*>(&DragDropSilentPrecheckCallsiteHook),
        module_base + kDragDropSilentPrecheckTargetRva,
        &g_drag_drop_silent_precheck_callsite_patch,
        L"DragDropSilentPrecheckCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemFromSlotResolveCallsiteRva),
        reinterpret_cast<void*>(&MoveItemFromSlotResolveCallsiteHook),
        module_base + kMoveItemSlotResolveTargetRva,
        &g_move_item_from_slot_resolve_callsite_patch,
        L"MoveItemFromSlotResolveCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemToSlotResolveCallsiteRva),
        reinterpret_cast<void*>(&MoveItemToSlotResolveCallsiteHook),
        module_base + kMoveItemSlotResolveTargetRva,
        &g_move_item_to_slot_resolve_callsite_patch,
        L"MoveItemToSlotResolveCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePrecheckCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePrecheckCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_precheck_callsite_patch,
        L"InvSlotHandleLButtonCorePrecheckCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_a_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsBCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_b_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsCCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsCCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_c_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsCCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsDCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsDCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_d_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsDCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsECallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsECallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_e_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsECallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLocalGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLocalGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLocalGateTargetRva,
        &g_invslot_handle_lbutton_core_local_gate_callsite_patch,
        L"InvSlotHandleLButtonCoreLocalGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolveObjectCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolveObjectCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolveObjectTargetRva,
        &g_invslot_handle_lbutton_core_resolve_object_callsite_patch,
        L"InvSlotHandleLButtonCoreResolveObjectCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedKindACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedKindACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedKindTargetRva,
        &g_invslot_handle_lbutton_core_resolved_kind_a_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedKindACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedKindBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedKindBCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedKindTargetRva,
        &g_invslot_handle_lbutton_core_resolved_kind_b_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedKindBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedFlagACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedFlagACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedFlagTargetRva,
        &g_invslot_handle_lbutton_core_resolved_flag_a_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedFlagACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedFlagBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedFlagBCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedFlagTargetRva,
        &g_invslot_handle_lbutton_core_resolved_flag_b_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedFlagBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePostResolveGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePostResolveGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePostResolveGateTargetRva,
        &g_invslot_handle_lbutton_core_post_resolve_gate_callsite_patch,
        L"InvSlotHandleLButtonCorePostResolveGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePostLookupModeGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePostLookupModeGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePostLookupModeGateTargetRva,
        &g_invslot_handle_lbutton_core_post_lookup_mode_gate_callsite_patch,
        L"InvSlotHandleLButtonCorePostLookupModeGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePostLookupUiPulseCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePostLookupUiPulseCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePostLookupUiPulseTargetRva,
        &g_invslot_handle_lbutton_core_post_lookup_ui_pulse_callsite_patch,
        L"InvSlotHandleLButtonCorePostLookupUiPulseCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreAltDispatchCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreAltDispatchCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreAltDispatchTargetRva,
        &g_invslot_handle_lbutton_core_alt_dispatch_callsite_patch,
        L"InvSlotHandleLButtonCoreAltDispatchCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreItemRangeGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreItemRangeGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreItemRangeGateTargetRva,
        &g_invslot_handle_lbutton_core_item_range_gate_callsite_patch,
        L"InvSlotHandleLButtonCoreItemRangeGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateSlot17GateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateSlot17GateTargetRva,
        &g_invslot_handle_lbutton_core_late_slot17_gate_callsite_patch,
        L"InvSlotHandleLButtonCoreLateSlot17GateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateSlot17ApplyCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateSlot17ApplyCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateSlot17ApplyTargetRva,
        &g_invslot_handle_lbutton_core_late_slot17_apply_callsite_patch,
        L"InvSlotHandleLButtonCoreLateSlot17ApplyCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchGateACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchGateACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateBranchGateATargetRva,
        &g_invslot_handle_lbutton_core_late_branch_gate_a_callsite_patch,
        L"InvSlotHandleLButtonCoreLateBranchGateACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchPrepCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchPrepCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateBranchPrepTargetRva,
        &g_invslot_handle_lbutton_core_late_branch_prep_callsite_patch,
        L"InvSlotHandleLButtonCoreLateBranchPrepCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchDispatchCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchDispatchCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateBranchDispatchTargetRva,
        &g_invslot_handle_lbutton_core_late_branch_dispatch_callsite_patch,
        L"InvSlotHandleLButtonCoreLateBranchDispatchCallsite");
}

bool TryReadGuildTrainerClassFromSpawn(
    const void* trainer_like,
    GuildTrainerClassResolution* resolution) noexcept {
    if (resolution == nullptr) {
        return false;
    }

    constexpr std::size_t kEqActorClassOffset = 0x013c;

    const std::uintptr_t trainer_spawn =
        reinterpret_cast<std::uintptr_t>(trainer_like);
    if (trainer_spawn == 0) {
        return false;
    }

    resolution->trainer_spawn_like = trainer_spawn;
    resolution->active_trainer_present = true;

    std::uint8_t raw_class_id = 0;
    if (TryReadEqPlayerDisplayedClassId(reinterpret_cast<const void*>(trainer_spawn), &raw_class_id)) {
        resolution->displayed_class_copied = true;
    } else if (TryCopyObject(
                   reinterpret_cast<const void*>(trainer_spawn + kEqActorClassOffset),
                   &raw_class_id)) {
        resolution->fallback_class_copied = true;
    } else {
        return false;
    }

    unsigned int normalized = 0;
    if (!NormalizeGuildTrainerClassId(raw_class_id, &normalized)) {
        return false;
    }

    resolution->trainer_class_id = raw_class_id;
    resolution->normalized_class_id = normalized;
    resolution->normalized_class_copied = true;
    return true;
}

bool TryReadGuildTrainerClassFromCallerFrame(
    std::uintptr_t caller_ebp,
    GuildTrainerClassResolution* resolution) noexcept {
    if (resolution == nullptr) {
        return false;
    }

    resolution->caller_ebp = caller_ebp;
    resolution->caller_ebp_copied = (caller_ebp != 0);
    resolution->caller_context_used = true;
    if (caller_ebp == 0) {
        return false;
    }

    std::uint8_t raw_class_id = 0;
    if (!TryCopyObject(
            reinterpret_cast<const void*>(caller_ebp + kGuildTrainerCallerClassOffset),
            &raw_class_id)) {
        return false;
    }

    resolution->caller_class_copied = true;

    unsigned int normalized = 0;
    if (!NormalizeGuildTrainerClassId(raw_class_id, &normalized)) {
        return false;
    }

    resolution->trainer_spawn_like = caller_ebp;
    resolution->trainer_class_id = raw_class_id;
    resolution->normalized_class_id = normalized;
    resolution->normalized_class_copied = true;
    return true;
}

bool TryReadActiveGuildTrainerClass(
    GuildTrainerClassResolution* resolution) noexcept {
    if (resolution == nullptr) {
        return false;
    }

    *resolution = {};
    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        return false;
    }
    resolution->module_base_available = true;

    std::uintptr_t trainer_spawn = 0;
    if (!TryCopyObject(
            reinterpret_cast<const void*>(module_base + kActiveGuildTrainerGlobalRva),
            &trainer_spawn)) {
        resolution->active_trainer_global_copied = false;
        return false;
    }

    resolution->active_trainer_global_copied = true;
    if (trainer_spawn == 0) {
        return false;
    }

    resolution->active_trainer_present = true;

    return TryReadGuildTrainerClassFromSpawn(
        reinterpret_cast<const void*>(trainer_spawn),
        resolution);
}

void LogGuildTrainerOverride(
    const GuildTrainerClassResolution& resolution,
    bool resolution_success,
    bool original_result_present,
    std::uintptr_t original_result,
    bool assigned_match,
    bool override_applied,
    std::uintptr_t final_result,
    void* this_context,
    std::uintptr_t trainer_spawn_like,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassTrainerHook target=GuildTrainerClassLookupCallsite";
    message += L" call_count=";
    message += std::to_wstring(g_guild_trainer_class_lookup_call_count);
    message += L" callsite_rva=";
    message += Hex32(kGuildTrainerClassLookupCallsiteRva);
    if (module_base != 0) {
        message += L" callsite_address=";
        message += HexPtr(module_base + kGuildTrainerClassLookupCallsiteRva);
    }
    message += L" this_context=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" current_frame=";
    message += HexPtr(resolution.current_frame);
    message += L" caller_ebp=";
    message += HexPtr(resolution.caller_ebp);
    message += L" trainer_spawn_like=";
    message += HexPtr(trainer_spawn_like);
    message += L" module_base_available=";
    message += resolution.module_base_available ? L"true" : L"false";
    message += L" current_frame_copied=";
    message += resolution.current_frame_copied ? L"true" : L"false";
    message += L" caller_ebp_copied=";
    message += resolution.caller_ebp_copied ? L"true" : L"false";
    message += L" active_trainer_global_copied=";
    message += resolution.active_trainer_global_copied ? L"true" : L"false";
    message += L" active_trainer_present=";
    message += resolution.active_trainer_present ? L"true" : L"false";
    message += L" caller_context_used=";
    message += resolution.caller_context_used ? L"true" : L"false";
    message += L" caller_class_copied=";
    message += resolution.caller_class_copied ? L"true" : L"false";
    message += L" displayed_class_copied=";
    message += resolution.displayed_class_copied ? L"true" : L"false";
    message += L" fallback_class_copied=";
    message += resolution.fallback_class_copied ? L"true" : L"false";
    message += L" normalized_class_copied=";
    message += resolution.normalized_class_copied ? L"true" : L"false";
    message += L" resolution_success=";
    message += resolution_success ? L"true" : L"false";
    message += L" trainer_class_id=";
    message += std::to_wstring(resolution.trainer_class_id);
    message += L" normalized_class_id=";
    message += std::to_wstring(resolution.normalized_class_id);
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    message += L" original_result_present=";
    message += original_result_present ? L"true" : L"false";
    message += L" original_result=";
    message += HexPtr(original_result);
    message += L" assigned_match=";
    message += assigned_match ? L"true" : L"false";
    message += L" override_applied=";
    message += override_applied ? L"true" : L"false";
    message += L" final_result=";
    message += HexPtr(final_result);
    message += L" override_count=";
    message += std::to_wstring(g_guild_trainer_class_lookup_override_count);
    monomyth::logger::Log(message);
}

extern "C" void* MONOMYTH_FASTCALL GuildTrainerClassLookupCallsiteHookImpl(
    void* this_context,
    void* caller_ebp_like) noexcept {
    ++g_guild_trainer_class_lookup_call_count;
    void* original_result =
        g_original_guild_trainer_class_lookup != nullptr
            ? g_original_guild_trainer_class_lookup(this_context)
            : nullptr;
    void* result = original_result;

    GuildTrainerClassResolution resolution = {};
    bool trainer_class_copied = TryReadActiveGuildTrainerClass(&resolution);
    if (!trainer_class_copied) {
        trainer_class_copied = TryReadGuildTrainerClassFromCallerFrame(
            reinterpret_cast<std::uintptr_t>(caller_ebp_like),
            &resolution);
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    const bool assigned_match =
        trainer_class_copied &&
        monomyth::multiclass_identity::HasAuthoritativeClass(
            snapshot.has_classes_bitmask,
            snapshot.classes_bitmask,
            resolution.normalized_class_id);
    const bool override_applied = assigned_match;
    if (override_applied) {
        ++g_guild_trainer_class_lookup_override_count;
        g_guild_trainer_class_lookup_override_record.fill(0);
        g_guild_trainer_class_lookup_override_record[kGuildTrainerClassLookupResolvedClassOffset] =
            static_cast<std::uint8_t>(resolution.normalized_class_id);
        g_pending_guild_trainer_session_class_id =
            static_cast<std::uint8_t>(resolution.normalized_class_id);
        result = g_guild_trainer_class_lookup_override_record.data();
    }

    LogGuildTrainerOverride(
        resolution,
        trainer_class_copied,
        g_original_guild_trainer_class_lookup != nullptr,
        reinterpret_cast<std::uintptr_t>(original_result),
        assigned_match,
        override_applied,
        reinterpret_cast<std::uintptr_t>(result),
        this_context,
        resolution.trainer_spawn_like,
        snapshot);

    return result;
}

#if defined(_MSC_VER)
extern "C" __declspec(naked) void GuildTrainerClassLookupCallsiteHookBridge() {
    __asm {
        mov edx, ebp
        jmp GuildTrainerClassLookupCallsiteHookImpl
    }
}
#else
extern "C" __attribute__((naked)) void GuildTrainerClassLookupCallsiteHookBridge() {
    asm volatile(
        "movl %%ebp, %%edx\n\t"
        "jmp *%0\n\t"
        :
        : "r"(&GuildTrainerClassLookupCallsiteHookImpl)
        : "edx");
}
#endif

void AppendActiveScrollCorrelation(std::wstring* message) {
    if (message == nullptr) {
        return;
    }

    if (g_scroll_scribe_active_correlation_id != 0) {
        message->append(L" scroll_scribe_correlation=");
        message->append(std::to_wstring(g_scroll_scribe_active_correlation_id));
    }

    if (g_spellbook_ui_active_correlation_id == 0) {
        return;
    }

    message->append(L" spellbook_ui_correlation=");
    message->append(std::to_wstring(g_spellbook_ui_active_correlation_id));
}

void LogSpellbookDispatcherCall(
    std::uint32_t correlation_id,
    void* this_window,
    int slot_like,
    std::uintptr_t caller_return_address) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"SpellUsabilityTrace target=SpellbookDispatcher correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(g_spellbook_dispatcher_address);
    if (module_base != 0 && g_spellbook_dispatcher_address >= module_base) {
        message += L" dispatcher_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_spellbook_dispatcher_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_window));
    message += L" slot_like=";
    message += std::to_wstring(slot_like);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribePathCall(
    std::uint32_t correlation_id,
    void* this_window,
    int spellbook_entry_like,
    std::uintptr_t caller_return_address,
    const SpellbookMemStateSnapshot& before_state,
    const SpellbookMemStateSnapshot& after_state,
    int original_result) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"SpellUsabilityTrace target=StartSpellScribePath correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(g_start_spell_scribe_path_address);
    if (module_base != 0 && g_start_spell_scribe_path_address >= module_base) {
        message += L" path_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_start_spell_scribe_path_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_window));
    message += L" spellbook_entry_like=";
    message += std::to_wstring(spellbook_entry_like);
    AppendSpellbookMemStateFields(&message, L"before", before_state);
    AppendSpellbookMemStateFields(&message, L"after", after_state);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
        message += L" caller_site_label=";
        message += (caller_return_address - module_base) == 0x35e7d0
            ? L"SpellbookDispatcherScribeBranch"
            : L"unknown";
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribePrecheckModeGetterCall(
    std::uint32_t correlation_id,
    void* this_context,
    std::uintptr_t caller_return_address,
    int original_result) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"SpellUsabilityTrace target=StartSpellScribePrecheckModeGetter correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(g_start_spell_scribe_precheck_mode_getter_address);
    if (module_base != 0 && g_start_spell_scribe_precheck_mode_getter_address >= module_base) {
        message += L" target_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_start_spell_scribe_precheck_mode_getter_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        const std::uint32_t caller_return_rva = static_cast<std::uint32_t>(
            caller_return_address - module_base);
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
        message += L" caller_site_label=";
        message += caller_return_rva == 0x35df4c
            ? L"StartSpellScribePathModeGate"
            : caller_return_rva == 0x44c4c3
            ? L"StartSpellScribePrecheckGateFallbackModeGate"
            : L"unknown";
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribePrecheckGateCall(
    std::uint32_t correlation_id,
    void* this_context,
    void* descriptor_like,
    int require_known_like,
    int allow_recheck_like,
    std::uintptr_t caller_return_address,
    bool original_result,
    bool returned_result) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"SpellUsabilityTrace target=StartSpellScribePrecheckGate correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(g_start_spell_scribe_precheck_gate_address);
    if (module_base != 0 && g_start_spell_scribe_precheck_gate_address >= module_base) {
        message += L" target_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_start_spell_scribe_precheck_gate_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" descriptor_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(descriptor_like));
    message += L" require_known_like=";
    message += std::to_wstring(require_known_like);
    message += L" allow_recheck_like=";
    message += std::to_wstring(allow_recheck_like);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        const std::uint32_t caller_return_rva = static_cast<std::uint32_t>(
            caller_return_address - module_base);
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
        message += L" caller_site_label=";
        message += caller_return_rva == 0x35df6f
            ? L"StartSpellScribePathPrecheckGate"
            : L"unknown";
    }
    message += L" original_result=";
    message += original_result ? L"true" : L"false";
    message += L" returned_result=";
    message += returned_result ? L"true" : L"false";
    AppendStartSpellScribePrecheckClassMaskFields(&message);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribePrecheckLookupCall(
    std::uint32_t correlation_id,
    void* this_context,
    void* spell_or_scroll_like,
    std::uintptr_t caller_return_address,
    int original_result) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"SpellUsabilityTrace target=StartSpellScribePrecheckLookup correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(g_start_spell_scribe_precheck_lookup_address);
    if (module_base != 0 && g_start_spell_scribe_precheck_lookup_address >= module_base) {
        message += L" target_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_start_spell_scribe_precheck_lookup_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" spell_or_scroll_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(spell_or_scroll_like));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        const std::uint32_t caller_return_rva = static_cast<std::uint32_t>(
            caller_return_address - module_base);
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
        message += L" caller_site_label=";
        message += caller_return_rva == 0x35df94
            ? L"StartSpellScribePathPrecheckLookup"
            : L"unknown";
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribeNestedPrecheckIntCall(
    const wchar_t* target_name,
    std::uintptr_t target_address,
    std::uint32_t correlation_id,
    void* this_context,
    std::uintptr_t caller_return_address,
    const wchar_t* caller_site_label,
    int original_result) {
    if (correlation_id == 0 || target_name == nullptr) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"SpellUsabilityTrace target=";
    message += target_name;
    message += L" correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(target_address);
    if (module_base != 0 && target_address >= module_base) {
        message += L" target_rva=";
        message += Hex32(static_cast<std::uint32_t>(target_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" caller_site_label=";
    message += caller_site_label == nullptr ? L"unknown" : caller_site_label;
    message += L" original_result=";
    message += std::to_wstring(original_result);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribePrecheckClassResolverCall(
    std::uint32_t correlation_id,
    void* this_context,
    std::uintptr_t caller_return_address,
    void* original_result,
    bool class_id_copied,
    std::uint8_t class_id) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"SpellUsabilityTrace target=StartSpellScribePrecheckClassResolver correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(g_start_spell_scribe_precheck_class_resolver_address);
    if (module_base != 0 &&
        g_start_spell_scribe_precheck_class_resolver_address >= module_base) {
        message += L" target_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_start_spell_scribe_precheck_class_resolver_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" caller_site_label=StartSpellScribePrecheckGateClassResolver";
    message += L" original_result=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" class_id_status=";
    message += class_id_copied ? L"copied" : L"unreadable";
    if (class_id_copied) {
        message += L" class_id=";
        message += std::to_wstring(class_id);
    }
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribePrecheckAssignedMaskGetterCall(
    std::uint32_t correlation_id,
    void* this_context,
    int flag_like,
    int extra_like,
    std::uintptr_t caller_return_address,
    std::uint32_t original_result) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"SpellUsabilityTrace target=StartSpellScribePrecheckAssignedMaskGetter correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(g_start_spell_scribe_precheck_assigned_mask_getter_address);
    if (module_base != 0 &&
        g_start_spell_scribe_precheck_assigned_mask_getter_address >= module_base) {
        message += L" target_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_start_spell_scribe_precheck_assigned_mask_getter_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" flag_like=";
    message += std::to_wstring(flag_like);
    message += L" extra_like=";
    message += std::to_wstring(extra_like);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" caller_site_label=StartSpellScribePrecheckGateAssignedMaskGetter";
    message += L" original_result=";
    message += Hex32(original_result);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellScribeNestedPrecheckBoolCall(
    const wchar_t* target_name,
    std::uintptr_t target_address,
    std::uint32_t correlation_id,
    void* this_context,
    void* descriptor_like,
    int flag_like,
    int extra_like,
    bool has_extra_like,
    std::uintptr_t caller_return_address,
    const wchar_t* caller_site_label,
    bool original_result) {
    if (correlation_id == 0 || target_name == nullptr) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"SpellUsabilityTrace target=";
    message += target_name;
    message += L" correlation=";
    message += std::to_wstring(correlation_id);
    message += L" address=";
    message += HexPtr(target_address);
    if (module_base != 0 && target_address >= module_base) {
        message += L" target_rva=";
        message += Hex32(static_cast<std::uint32_t>(target_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" descriptor_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(descriptor_like));
    message += L" flag_like=";
    message += std::to_wstring(flag_like);
    if (has_extra_like) {
        message += L" extra_like=";
        message += std::to_wstring(extra_like);
    }
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" caller_site_label=";
    message += caller_site_label == nullptr ? L"unknown" : caller_site_label;
    message += L" original_result=";
    message += original_result ? L"true" : L"false";
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogPendingMemorizeSendGap(const wchar_t* reason) {
    if (g_memorize_send_pending_correlation_id == 0) {
        return;
    }

    std::wstring message = L"SpellUsabilityTrace target=MemorizeSend status=not_observed correlation=";
    message += std::to_wstring(g_memorize_send_pending_correlation_id);
    message += L" reason=";
    message += reason == nullptr ? L"unknown" : reason;
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
    ClearPendingMemorizeSendObservation();
}

void LogMemorizeSendDecodeFailure(
    std::uint32_t correlation_id,
    const wchar_t* reason) {
    if (correlation_id == 0) {
        return;
    }

    std::wstring message = L"SpellUsabilityTrace target=MemorizeSend status=not_decoded correlation=";
    message += std::to_wstring(correlation_id);
    message += L" reason=";
    message += reason == nullptr ? L"unknown" : reason;
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogMemorizeSendIntermediateSend(
    std::uint32_t correlation_id,
    std::uint32_t opcode,
    std::uint32_t mode_like,
    std::uint32_t total_length,
    const void* packet,
    void* this_context,
    std::uintptr_t caller_return_address,
    bool original_result,
    std::uint32_t wrapper_send_index,
    std::uint32_t wrapper_send_budget) {
    if (correlation_id == 0) {
        return;
    }

    constexpr std::size_t kCallerContextPrefixBytes = 8;
    constexpr std::size_t kCallerContextBytes = 16;
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_return_rva =
        (module_base != 0 && caller_return_address >= module_base)
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const std::uintptr_t caller_context_address =
        caller_return_address >= kCallerContextPrefixBytes
        ? caller_return_address - kCallerContextPrefixBytes
        : caller_return_address;
    std::array<std::uint8_t, kCallerContextBytes> caller_bytes = {};
    const bool caller_bytes_copied = TryCopyBytes(
        reinterpret_cast<const void*>(caller_context_address),
        caller_bytes.size(),
        caller_bytes.data());
    std::array<std::uint8_t, kIntermediateSendPrefixByteCap> packet_prefix = {};
    const std::size_t packet_prefix_length = total_length < kIntermediateSendPrefixByteCap
        ? static_cast<std::size_t>(total_length)
        : kIntermediateSendPrefixByteCap;
    const bool packet_prefix_copied =
        packet != nullptr &&
        packet_prefix_length != 0 &&
        TryCopyBytes(packet, packet_prefix_length, packet_prefix.data());
    std::array<RelativeCallResolution, 2> resolved_calls = {};
    std::size_t resolved_call_count = 0;
    if (caller_bytes_copied && caller_context_address != 0) {
        for (std::size_t i = 0; i + 5 <= caller_bytes.size(); ++i) {
            if (caller_bytes[i] != 0xe8) {
                continue;
            }

            const RelativeCallResolution call = ResolveRelativeCall(
                caller_bytes.data() + i,
                caller_bytes.size() - i,
                caller_context_address + i);
            if (!call.resolved) {
                continue;
            }

            resolved_calls[resolved_call_count++] = call;
            if (resolved_call_count >= resolved_calls.size()) {
                break;
            }
        }
    }
    const KnownMemorizeFollowupCallsiteMatch caller_site = MatchKnownMemorizeFollowupCallsite(
        caller_return_rva,
        mode_like,
        caller_bytes.data(),
        caller_bytes.size(),
        caller_bytes_copied);

    std::wstring message = L"SpellUsabilityTrace target=MemorizeSend status=intermediate_send correlation=";
    message += std::to_wstring(correlation_id);
    message += L" observed_opcode=";
    message += std::to_wstring(opcode);
    message += L" observed_opcode_hex=";
    message += Hex32(opcode);
    message += L" observed_opcode_name=";
    message += monomyth::opcode_reference::LookupRof2OpcodeName(opcode);
    message += L" wrapper_send_index=";
    message += std::to_wstring(wrapper_send_index);
    message += L" wrapper_send_budget=";
    message += std::to_wstring(wrapper_send_budget);
    message += L" mode_like=";
    message += std::to_wstring(mode_like);
    message += L" total_length=";
    message += std::to_wstring(total_length);
    message += L" packet_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(packet));
    message += L" packet_prefix_status=";
    message += packet_prefix_copied ? L"copied" : L"unavailable";
    if (packet_prefix_copied) {
        message += L" packet_prefix_len=";
        message += std::to_wstring(packet_prefix_length);
        message += L" packet_prefix_hex=\"";
        message += HexBytes(packet_prefix.data(), packet_prefix_length);
        message += L"\"";
    }
    message += L" wrapper_this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (caller_return_rva != 0) {
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
    }
    message += L" caller_bytes_status=";
    message += caller_bytes_copied ? L"copied" : L"unavailable";
    if (caller_bytes_copied) {
        message += L" caller_bytes_address=";
        message += HexPtr(caller_context_address);
        message += L" caller_bytes_hex=\"";
        message += HexBytes(caller_bytes.data(), caller_bytes.size());
        message += L"\"";
    }
    message += L" caller_resolved_call_count=";
    message += std::to_wstring(resolved_call_count);
    for (std::size_t i = 0; i < resolved_call_count; ++i) {
        AppendResolvedCallFields(
            &message,
            i == 0 ? L"caller_call_1" : L"caller_call_2",
            resolved_calls[i],
            module_base,
            g_memorize_send_packet_wrapper_address);
    }
    if (caller_site.site != nullptr) {
        message += L" caller_site_label=";
        message += caller_site.site->label;
        message += L" caller_site_validation=";
        message += caller_site.bytes_match ? L"matched" : L"drifted";
        message += L" caller_site_expected_return_rva=";
        message += Hex32(caller_site.site->return_rva);
        message += L" caller_site_expected_mode_like=";
        message += std::to_wstring(caller_site.site->mode_like);
        if (!caller_site.bytes_match) {
            message += L" caller_site_expected_bytes_hex=\"";
            message += HexBytes(
                caller_site.site->caller_context_bytes.data(),
                caller_site.site->caller_context_bytes.size());
            message += L"\"";
        }
    } else if (caller_return_rva != 0) {
        message += L" caller_site_label=unknown";
    }
    message += L" original_result=";
    message += original_result ? L"true" : L"false";
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogMemorizeSendBudgetExhausted(
    std::uint32_t correlation_id,
    std::uint32_t wrapper_send_budget) {
    if (correlation_id == 0) {
        return;
    }

    std::wstring message = L"SpellUsabilityTrace target=MemorizeSend status=not_observed correlation=";
    message += std::to_wstring(correlation_id);
    message += L" reason=wrapper_send_budget_exhausted wrapper_send_budget=";
    message += std::to_wstring(wrapper_send_budget);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogPendingSpellbookScribeGap(const wchar_t* reason) {
    if (g_spellbook_scribe_pending_correlation_id == 0) {
        return;
    }

    std::wstring message =
        L"SpellUsabilityTrace target=SpellbookScribeSend status=not_observed correlation=";
    message += std::to_wstring(g_spellbook_scribe_pending_correlation_id);
    message += L" reason=";
    message += reason == nullptr ? L"unknown" : reason;
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
    g_spellbook_scribe_pending_correlation_id = 0;
    g_spellbook_scribe_pending_wrapper_sends = 0;
}

void LogSpellbookScribeSendEvent(
    const wchar_t* status,
    std::uint32_t correlation_id,
    std::uint32_t opcode,
    std::uint32_t mode_like,
    std::uint32_t total_length,
    const void* packet,
    void* this_context,
    std::uintptr_t caller_return_address,
    bool original_result,
    std::uint32_t wrapper_send_index,
    std::uint32_t wrapper_send_budget) {
    if (status == nullptr || correlation_id == 0) {
        return;
    }

    constexpr std::size_t kCallerContextPrefixBytes = 8;
    constexpr std::size_t kCallerContextBytes = 16;
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_return_rva =
        (module_base != 0 && caller_return_address >= module_base)
        ? static_cast<std::uint32_t>(caller_return_address - module_base)
        : 0;
    const std::uintptr_t caller_context_address =
        caller_return_address >= kCallerContextPrefixBytes
        ? caller_return_address - kCallerContextPrefixBytes
        : caller_return_address;
    std::array<std::uint8_t, kCallerContextBytes> caller_bytes = {};
    const bool caller_bytes_copied = TryCopyBytes(
        reinterpret_cast<const void*>(caller_context_address),
        caller_bytes.size(),
        caller_bytes.data());
    std::array<std::uint8_t, kIntermediateSendPrefixByteCap> packet_prefix = {};
    const std::size_t packet_prefix_length = total_length < kIntermediateSendPrefixByteCap
        ? static_cast<std::size_t>(total_length)
        : kIntermediateSendPrefixByteCap;
    const bool packet_prefix_copied =
        packet != nullptr &&
        packet_prefix_length != 0 &&
        TryCopyBytes(packet, packet_prefix_length, packet_prefix.data());
    std::array<RelativeCallResolution, 2> resolved_calls = {};
    std::size_t resolved_call_count = 0;
    if (caller_bytes_copied && caller_context_address != 0) {
        for (std::size_t i = 0; i + 5 <= caller_bytes.size(); ++i) {
            if (caller_bytes[i] != 0xe8) {
                continue;
            }

            const RelativeCallResolution call = ResolveRelativeCall(
                caller_bytes.data() + i,
                caller_bytes.size() - i,
                caller_context_address + i);
            if (!call.resolved) {
                continue;
            }

            resolved_calls[resolved_call_count++] = call;
            if (resolved_call_count >= resolved_calls.size()) {
                break;
            }
        }
    }
    const KnownMemorizeFollowupCallsiteMatch caller_site = MatchKnownMemorizeFollowupCallsite(
        caller_return_rva,
        mode_like,
        caller_bytes.data(),
        caller_bytes.size(),
        caller_bytes_copied);

    std::wstring message = L"SpellUsabilityTrace target=SpellbookScribeSend status=";
    message += status;
    message += L" correlation=";
    message += std::to_wstring(correlation_id);
    message += L" observed_opcode=";
    message += std::to_wstring(opcode);
    message += L" observed_opcode_hex=";
    message += Hex32(opcode);
    message += L" observed_opcode_name=";
    message += monomyth::opcode_reference::LookupRof2OpcodeName(opcode);
    message += L" wrapper_send_index=";
    message += std::to_wstring(wrapper_send_index);
    message += L" wrapper_send_budget=";
    message += std::to_wstring(wrapper_send_budget);
    message += L" mode_like=";
    message += std::to_wstring(mode_like);
    message += L" total_length=";
    message += std::to_wstring(total_length);
    message += L" packet_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(packet));
    message += L" packet_prefix_status=";
    message += packet_prefix_copied ? L"copied" : L"unavailable";
    if (packet_prefix_copied) {
        message += L" packet_prefix_len=";
        message += std::to_wstring(packet_prefix_length);
        message += L" packet_prefix_hex=\"";
        message += HexBytes(packet_prefix.data(), packet_prefix_length);
        message += L"\"";
    }
    message += L" wrapper_this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (caller_return_rva != 0) {
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
    }
    message += L" caller_bytes_status=";
    message += caller_bytes_copied ? L"copied" : L"unavailable";
    if (caller_bytes_copied) {
        message += L" caller_bytes_address=";
        message += HexPtr(caller_context_address);
        message += L" caller_bytes_hex=\"";
        message += HexBytes(caller_bytes.data(), caller_bytes.size());
        message += L"\"";
    }
    message += L" caller_resolved_call_count=";
    message += std::to_wstring(resolved_call_count);
    for (std::size_t i = 0; i < resolved_call_count; ++i) {
        AppendResolvedCallFields(
            &message,
            i == 0 ? L"caller_call_1" : L"caller_call_2",
            resolved_calls[i],
            module_base,
            g_memorize_send_packet_wrapper_address);
    }
    if (caller_site.site != nullptr) {
        message += L" caller_site_label=";
        message += caller_site.site->label;
        message += L" caller_site_validation=";
        message += caller_site.bytes_match ? L"matched" : L"drifted";
    } else if (caller_return_rva != 0) {
        message += L" caller_site_label=unknown";
    }
    message += L" original_result=";
    message += original_result ? L"true" : L"false";
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogSpellbookScribeBudgetExhausted(
    std::uint32_t correlation_id,
    std::uint32_t wrapper_send_budget) {
    if (correlation_id == 0) {
        return;
    }

    std::wstring message =
        L"SpellUsabilityTrace target=SpellbookScribeSend status=not_observed correlation=";
    message += std::to_wstring(correlation_id);
    message += L" reason=wrapper_send_budget_exhausted wrapper_send_budget=";
    message += std::to_wstring(wrapper_send_budget);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogStartSpellMemorizationPathCall(
    std::uint32_t correlation_id,
    void* controller_this,
    void* pending_window,
    std::uint32_t pending_slot_state,
    std::uint32_t mode_like,
    std::int32_t spell_or_book_index,
    std::int32_t slot_like,
    std::uintptr_t caller_return_address,
    const SpellbookMemStateSnapshot& controller_before_state,
    const SpellbookMemStateSnapshot& controller_after_state,
    const SpellbookMemStateSnapshot& pending_window_before_state,
    const SpellbookMemStateSnapshot& pending_window_after_state,
    int original_result) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"SpellUsabilityTrace target=StartSpellMemorizationPath correlation=";
    message += std::to_wstring(correlation_id);
    message += L" path_address=";
    message += HexPtr(g_start_spell_memorization_path_address);
    if (module_base != 0 && g_start_spell_memorization_path_address >= module_base) {
        message += L" path_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_start_spell_memorization_path_address - module_base));
    }
    message += L" controller_this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(controller_this));
    message += L" pending_window=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(pending_window));
    message += L" controller_matches_pending_window=";
    message += controller_this == pending_window ? L"true" : L"false";
    message += L" pending_slot_state=";
    message += Hex32(pending_slot_state);
    message += L" mode_like=";
    message += std::to_wstring(mode_like);
    message += L" spell_or_book_index=";
    message += std::to_wstring(spell_or_book_index);
    message += L" slot_like=";
    message += std::to_wstring(slot_like);
    AppendSpellbookMemStateFields(&message, L"controller_before", controller_before_state);
    AppendSpellbookMemStateFields(&message, L"controller_after", controller_after_state);
    AppendSpellbookMemStateFields(
        &message,
        L"pending_window_before",
        pending_window_before_state);
    AppendSpellbookMemStateFields(
        &message,
        L"pending_window_after",
        pending_window_after_state);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
        message += L" caller_site_label=";
        if (caller_return_address - module_base == 0x35e802) {
            message += L"SpellbookDispatcherMemStartCallsite";
        } else {
            message += L"unknown";
        }
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogSpellbookMemorizeSendPathCall(
    std::uint32_t correlation_id,
    void* this_window,
    std::uintptr_t caller_return_address,
    const SpellbookMemStateSnapshot& before_state,
    const SpellbookMemStateSnapshot& after_state,
    int original_result) {
    if (correlation_id == 0) {
        return;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"SpellUsabilityTrace target=SpellbookMemorizeSendPath correlation=";
    message += std::to_wstring(correlation_id);
    message += L" path_address=";
    message += HexPtr(g_spellbook_memorize_send_path_address);
    if (module_base != 0 && g_spellbook_memorize_send_path_address >= module_base) {
        message += L" path_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_spellbook_memorize_send_path_address - module_base));
    }
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_window));
    AppendSpellbookMemStateFields(&message, L"before", before_state);
    AppendSpellbookMemStateFields(&message, L"after", after_state);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

const wchar_t* DescribeMemorizeSendCallerSite(std::uint32_t caller_return_rva) noexcept {
    if (caller_return_rva == 0x35dd4f) {
        return L"SpellbookMemorizeSendPathWrapperCallsite";
    }
    if (caller_return_rva == 0x35e700) {
        return L"MemSpellCommitPathWrapperCallsite";
    }
    if (caller_return_rva == 0x35d1eb) {
        return L"MemorizeSendCaller35d1eb";
    }
    return L"unknown";
}

void LogMemorizeSendObserved(
    std::uint32_t correlation_id,
    std::uint32_t wrapper_send_index,
    std::uint32_t mode_like,
    std::uint32_t total_length,
    const void* packet,
    void* this_context,
    std::uintptr_t caller_return_address,
    bool original_result) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"SpellUsabilityTrace target=MemorizeSendObserved";
    if (correlation_id != 0) {
        message += L" correlation=";
        message += std::to_wstring(correlation_id);
    }
    message += L" wrapper_send_index=";
    message += std::to_wstring(wrapper_send_index);
    message += L" mode_like=";
    message += std::to_wstring(mode_like);
    message += L" total_length=";
    message += std::to_wstring(total_length);
    message += L" packet_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(packet));
    message += L" wrapper_this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        const std::uint32_t caller_return_rva = static_cast<std::uint32_t>(
            caller_return_address - module_base);
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
        message += L" caller_site_label=";
        message += DescribeMemorizeSendCallerSite(caller_return_rva);
    }
    message += L" original_result=";
    message += original_result ? L"true" : L"false";
    AppendActiveScrollCorrelation(&message);
    monomyth::logger::Log(message);
}

void LogMoveItemSendObserved(
    std::uint32_t mode_like,
    std::uint32_t total_length,
    std::uint32_t payload_length,
    const void* packet,
    void* this_context,
    std::uintptr_t caller_return_address,
    bool original_result) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemSend";
    message += L" wrapper_address=";
    message += HexPtr(g_memorize_send_packet_wrapper_address);
    if (module_base != 0 && g_memorize_send_packet_wrapper_address >= module_base) {
        message += L" wrapper_rva=";
        message += Hex32(static_cast<std::uint32_t>(
            g_memorize_send_packet_wrapper_address - module_base));
    }
    message += L" wrapper_this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" packet_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(packet));
    message += L" opcode=";
    message += std::to_wstring(kMoveItemOpcode);
    message += L" opcode_hex=";
    message += Hex32(kMoveItemOpcode);
    message += L" opcode_name=OP_MoveItem";
    message += L" mode_like=";
    message += std::to_wstring(mode_like);
    message += L" total_length=";
    message += std::to_wstring(total_length);
    message += L" payload_length=";
    message += std::to_wstring(payload_length);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += original_result ? L"true" : L"false";

    ClientMoveItemWire move_item = {};
    if (payload_length >= sizeof(move_item) &&
        total_length >= (kPacketOpcodeBytes + sizeof(move_item)) &&
        TryCopyBytes(
            static_cast<const std::uint8_t*>(packet) + kPacketOpcodeBytes,
            sizeof(move_item),
            reinterpret_cast<std::uint8_t*>(&move_item))) {
        message += L" move_item_payload_status=copied";
        message += L" from_type=";
        message += std::to_wstring(move_item.from_slot.type);
        message += L" from_slot=";
        message += std::to_wstring(move_item.from_slot.slot);
        message += L" from_subindex=";
        message += std::to_wstring(move_item.from_slot.subindex);
        message += L" from_augindex=";
        message += std::to_wstring(move_item.from_slot.augindex);
        message += L" to_type=";
        message += std::to_wstring(move_item.to_slot.type);
        message += L" to_slot=";
        message += std::to_wstring(move_item.to_slot.slot);
        message += L" to_subindex=";
        message += std::to_wstring(move_item.to_slot.subindex);
        message += L" to_augindex=";
        message += std::to_wstring(move_item.to_slot.augindex);
        message += L" number_in_stack=";
        message += std::to_wstring(move_item.number_in_stack);
    } else {
        message += L" move_item_payload_status=unavailable";
    }
    monomyth::logger::Log(message);
}

void AppendClientInventorySlotFields(
    std::wstring* message,
    const wchar_t* prefix,
    const ClientInventorySlotWire& slot) {
    if (message == nullptr || prefix == nullptr) {
        return;
    }

    message->append(L" ");
    message->append(prefix);
    message->append(L"_type=");
    message->append(std::to_wstring(slot.type));
    message->append(L" ");
    message->append(prefix);
    message->append(L"_unknown02=");
    message->append(std::to_wstring(slot.unknown02));
    message->append(L" ");
    message->append(prefix);
    message->append(L"_slot=");
    message->append(std::to_wstring(slot.slot));
    message->append(L" ");
    message->append(prefix);
    message->append(L"_subindex=");
    message->append(std::to_wstring(slot.subindex));
    message->append(L" ");
    message->append(prefix);
    message->append(L"_augindex=");
    message->append(std::to_wstring(slot.augindex));
    message->append(L" ");
    message->append(prefix);
    message->append(L"_unknown01=");
    message->append(std::to_wstring(slot.unknown01));
}

void AppendOffhandWeaponPolicyFields(
    std::wstring* message,
    const OffhandWeaponPolicySnapshot& snapshot) {
    if (message == nullptr) {
        return;
    }

    message->append(L" item_class_mask_status=");
    message->append(snapshot.item_class_mask_copied ? L"copied" : L"unavailable");
    if (snapshot.item_class_mask_copied) {
        message->append(L" item_class_mask_client=");
        message->append(Hex32(snapshot.item_class_mask));
    }
    message->append(L" equip_slots_status=");
    message->append(snapshot.equip_slots_copied ? L"copied" : L"unavailable");
    if (snapshot.equip_slots_copied) {
        message->append(L" equip_slots=");
        message->append(Hex32(snapshot.equip_slots));
    }
    message->append(L" local_player_level_status=");
    message->append(snapshot.local_player_level_copied ? L"copied" : L"unavailable");
    if (snapshot.local_player_level_copied) {
        message->append(L" local_player_level=");
        message->append(std::to_wstring(snapshot.local_player_level));
    }
    message->append(L" item_class_status=");
    message->append(snapshot.item_class_copied ? L"copied" : L"unavailable");
    if (snapshot.item_class_copied) {
        message->append(L" item_class=");
        message->append(std::to_wstring(snapshot.item_class));
    }
    message->append(L" offhand_weapon_candidate=");
    message->append(snapshot.is_weapon && snapshot.can_wear_secondary ? L"true" : L"false");
    message->append(L" holding_animation_status=");
    message->append(snapshot.holding_animation_copied ? L"copied" : L"unavailable");
    if (snapshot.holding_animation_copied) {
        message->append(L" holding_animation=");
        message->append(std::to_wstring(snapshot.holding_animation));
    }
    message->append(L" item_matches_assigned_class=");
    message->append(snapshot.item_matches_assigned_class ? L"true" : L"false");
    message->append(L" dual_wield_skill_checked=");
    message->append(snapshot.dual_wield_skill_checked ? L"true" : L"false");
    message->append(L" has_dual_wield_skill=");
    message->append(snapshot.has_dual_wield_skill ? L"true" : L"false");
    message->append(L" authoritative_dual_wield_class_present=");
    message->append(snapshot.authoritative_dual_wield_class_present ? L"true" : L"false");
    message->append(L" dual_wield_matrix_matched=");
    message->append(snapshot.dual_wield_matrix_matched ? L"true" : L"false");
    message->append(L" dual_wield_entitlement_class_id=");
    message->append(std::to_wstring(snapshot.dual_wield_entitlement_class_id));
    if (snapshot.dual_wield_entitlement_class_id != 0) {
        const wchar_t* class_name = monomyth::multiclass_identity::ClassDisplayToken(
            snapshot.dual_wield_entitlement_class_id,
            monomyth::multiclass_identity::ClassDisplayStyle::kFullName);
        if (class_name != nullptr) {
            message->append(L" dual_wield_entitlement_class=\"");
            message->append(class_name);
            message->append(L"\"");
        }
    }
    message->append(L" dual_wield_required_level=");
    message->append(std::to_wstring(snapshot.dual_wield_required_level));
    message->append(L" dual_wield_entitlement_fallback_used=");
    message->append(snapshot.dual_wield_entitlement_fallback_used ? L"true" : L"false");
    message->append(L" has_dual_wield_entitlement=");
    message->append(snapshot.has_dual_wield_entitlement ? L"true" : L"false");
    message->append(L" primary_blocks_secondary_weapon=");
    message->append(snapshot.primary_blocks_secondary_weapon ? L"true" : L"false");
    message->append(L" eligible_secondary_weapon_override=");
    message->append(snapshot.eligible ? L"true" : L"false");
}

void LogAutoEquipClassGateDecision(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* item_wrapper_holder_like,
    unsigned long arg2,
    unsigned long arg3,
    std::uint8_t original_result,
    std::uint8_t returned_result,
    std::uintptr_t item_like,
    const OffhandWeaponPolicySnapshot& offhand_policy,
    bool candidate_slot_proven,
    bool candidate_slot_is_secondary,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        returned_result != original_result
            ? L"MulticlassItemUsability target=AutoEquipClassGate"
            : L"MulticlassItemTrace target=AutoEquipClassGateObserved";
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" item_wrapper_holder=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(item_wrapper_holder_like));
    message += L" arg2=";
    message += std::to_wstring(arg2);
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=";
    message += std::to_wstring(returned_result);
    message += L" item_like=";
    message += HexPtr(item_like);
    message += L" item_data_like=";
    message += HexPtr(offhand_policy.item_data_like);
    AppendOffhandWeaponPolicyFields(&message, offhand_policy);
    message += L" candidate_slot_proven=";
    message += candidate_slot_proven ? L"true" : L"false";
    message += L" candidate_slot_is_secondary=";
    message += candidate_slot_is_secondary ? L"true" : L"false";
    message += L" override_mode=autoequip_hot_class_gate_trace_only";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    if (returned_result != original_result) {
        message += L" override_count=";
        message += std::to_wstring(g_auto_equip_class_gate_override_count);
    }
    monomyth::logger::Log(message);
}

InvSlotHandleLButtonCoreSlotContextSnapshot CaptureInvSlotHandleLButtonCoreSlotContext(
    void* this_context) {
    InvSlotHandleLButtonCoreSlotContextSnapshot snapshot = {};
    if (this_context == nullptr) {
        return snapshot;
    }

    snapshot.this_bytes_copied =
        TryCopyBytes(this_context, snapshot.this_bytes.size(), snapshot.this_bytes.data());
    snapshot.child_pointer_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x4,
                      &snapshot.child_pointer);
    if (snapshot.child_pointer_copied && snapshot.child_pointer != 0) {
        snapshot.slot_record_copied = TryCopyObject(
            reinterpret_cast<const std::uint8_t*>(snapshot.child_pointer) + 0x264,
            &snapshot.slot_record);
        snapshot.child_bytes_copied =
            TryCopyBytes(reinterpret_cast<const void*>(snapshot.child_pointer),
                         snapshot.child_bytes.size(),
                         snapshot.child_bytes.data());
    }

    return snapshot;
}

InvSlotHandleLButtonCoreLateManagerSnapshot CaptureInvSlotHandleLButtonCoreLateManagerContext(
    void* this_context) {
    InvSlotHandleLButtonCoreLateManagerSnapshot snapshot = {};
    if (this_context == nullptr) {
        return snapshot;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(this_context);
    snapshot.field_23c_copied = TryCopyObject(bytes + 0x23c, &snapshot.field_23c);
    snapshot.field_240_copied = TryCopyObject(bytes + 0x240, &snapshot.field_240);
    snapshot.field_244_copied = TryCopyObject(bytes + 0x244, &snapshot.field_244);
    snapshot.field_248_copied = TryCopyObject(bytes + 0x248, &snapshot.field_248);
    snapshot.field_254_copied = TryCopyObject(bytes + 0x254, &snapshot.field_254);
    snapshot.field_258_copied = TryCopyObject(bytes + 0x258, &snapshot.field_258);
    snapshot.field_25c_copied = TryCopyObject(bytes + 0x25c, &snapshot.field_25c);
    snapshot.field_260_copied = TryCopyObject(bytes + 0x260, &snapshot.field_260);
    snapshot.field_264_copied = TryCopyObject(bytes + 0x264, &snapshot.field_264);
    snapshot.field_2a8_copied = TryCopyObject(bytes + 0x2a8, &snapshot.field_2a8);
    return snapshot;
}

InvSlotHandleLButtonCorePostResolveGateSnapshot CaptureInvSlotHandleLButtonCorePostResolveGateContext(
    void* slot_context_like) {
    InvSlotHandleLButtonCorePostResolveGateSnapshot snapshot = {};
    if (slot_context_like != nullptr) {
        const auto* slot_bytes = reinterpret_cast<const std::uint8_t*>(slot_context_like);
        snapshot.slot_context_field_244_copied =
            TryCopyObject(slot_bytes + 0x244, &snapshot.slot_context_field_244);
        snapshot.slot_context_bytes_copied = TryCopyBytes(
            slot_context_like,
            snapshot.slot_context_bytes.size(),
            snapshot.slot_context_bytes.data());
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        return snapshot;
    }

    snapshot.drag_context_root_copied = TryCopyObject(
        reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
        &snapshot.drag_context_root);
    if (snapshot.drag_context_root_copied && snapshot.drag_context_root != 0) {
        snapshot.drag_context_manager = snapshot.drag_context_root + 0x2dc8;
        snapshot.drag_context_manager_copied = true;
        snapshot.drag_context_manager_bytes_copied = TryCopyBytes(
            reinterpret_cast<const void*>(snapshot.drag_context_manager),
            snapshot.drag_context_manager_bytes.size(),
            snapshot.drag_context_manager_bytes.data());
    }

    snapshot.state_root_copied = TryCopyObject(
        reinterpret_cast<const void*>(module_base + kInvSlotHandleLButtonCoreStateRootRva),
        &snapshot.state_root);
    if (snapshot.state_root_copied && snapshot.state_root != 0) {
        const auto* state_bytes = reinterpret_cast<const std::uint8_t*>(snapshot.state_root);
        snapshot.state_field_448_copied =
            TryCopyObject(state_bytes + 0x448, &snapshot.state_field_448);
        snapshot.state_field_eb8_copied =
            TryCopyObject(state_bytes + 0xeb8, &snapshot.state_field_eb8);
    }

    snapshot.dispatcher_object_copied = TryCopyObject(
        reinterpret_cast<const void*>(module_base + kInvSlotHandleLButtonCoreDispatcherObjectRva),
        &snapshot.dispatcher_object);
    if (snapshot.dispatcher_object_copied && snapshot.dispatcher_object != 0) {
        snapshot.dispatcher_vtable_copied = TryCopyObject(
            reinterpret_cast<const void*>(snapshot.dispatcher_object),
            &snapshot.dispatcher_vtable);
        if (snapshot.dispatcher_vtable_copied && snapshot.dispatcher_vtable != 0) {
            snapshot.dispatcher_virtual_0c_copied = TryCopyObject(
                reinterpret_cast<const std::uint8_t*>(snapshot.dispatcher_vtable) + 0xc,
                &snapshot.dispatcher_virtual_0c);
        }
    }

    return snapshot;
}

void LogMoveItemLocalSlotResolveTrace(
    const wchar_t* callsite_label,
    const wchar_t* packet_role,
    std::uint32_t callsite_rva,
    void* this_context,
    void* slot_like,
    void* resolved_slot_like,
    std::uintptr_t caller_return_address) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemLocalSlotResolve";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" packet_role=";
    message += packet_role == nullptr ? L"unknown" : packet_role;
    message += L" helper_address=";
    message += HexPtr(module_base + kMoveItemSlotResolveTargetRva);
    message += L" helper_rva=";
    message += Hex32(kMoveItemSlotResolveTargetRva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" input_slot_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_like));
    message += L" resolved_slot_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(resolved_slot_like));

    ClientInventorySlotWire input_slot = {};
    const bool input_slot_copied = TryCopyObject(slot_like, &input_slot);
    message += L" input_slot_status=";
    message += input_slot_copied ? L"copied" : L"unavailable";
    if (input_slot_copied) {
        AppendClientInventorySlotFields(&message, L"input", input_slot);
    }

    ClientInventorySlotWire resolved_slot = {};
    const bool resolved_slot_copied = TryCopyObject(resolved_slot_like, &resolved_slot);
    message += L" resolved_slot_status=";
    message += resolved_slot_copied ? L"copied" : L"unavailable";
    if (resolved_slot_copied) {
        AppendClientInventorySlotFields(&message, L"resolved", resolved_slot);
    }

    monomyth::logger::Log(message);
}

void LogMoveItemOwnerPrimarySetterTrace(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* primary_object_like,
    std::uintptr_t owner_field_98_before,
    bool owner_field_98_before_copied,
    std::uintptr_t owner_field_98_after,
    bool owner_field_98_after_copied,
    std::uintptr_t primary_pointer_9c_before,
    bool primary_pointer_9c_before_copied,
    std::uintptr_t primary_pointer_9c_after,
    bool primary_pointer_9c_after_copied,
    std::uint32_t owner_field_a0_before,
    bool owner_field_a0_before_copied,
    std::uint32_t owner_field_a0_after,
    bool owner_field_a0_after_copied,
    std::uintptr_t fallback_pointer_144_before,
    bool fallback_pointer_144_before_copied,
    std::uintptr_t fallback_pointer_144_after,
    bool fallback_pointer_144_after_copied,
    std::uintptr_t resolved_object_before,
    std::uintptr_t resolved_object_after,
    std::uint8_t resolved_flag_11a_after,
    bool resolved_flag_11a_after_copied,
    std::uint8_t resolved_field_521_after,
    bool resolved_field_521_after_copied,
    const std::array<std::uint8_t, 24>& context_bytes_before,
    bool context_bytes_before_copied,
    const std::array<std::uint8_t, 24>& context_bytes_after,
    bool context_bytes_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemOwnerPrimarySetter";
    message += L" probe_address=";
    message += HexPtr(module_base + kMoveItemOwnerPrimarySetterTargetRva);
    message += L" probe_rva=";
    message += Hex32(kMoveItemOwnerPrimarySetterTargetRva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" primary_object_arg=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(primary_object_like));
    message += L" owner_field_98_before_status=";
    message += owner_field_98_before_copied ? L"copied" : L"unavailable";
    if (owner_field_98_before_copied) {
        message += L" owner_field_98_before=";
        message += HexPtr(owner_field_98_before);
    }
    message += L" owner_field_98_after_status=";
    message += owner_field_98_after_copied ? L"copied" : L"unavailable";
    if (owner_field_98_after_copied) {
        message += L" owner_field_98_after=";
        message += HexPtr(owner_field_98_after);
    }
    message += L" primary_pointer_9c_before=";
    message += HexPtr(primary_pointer_9c_before);
    message += L" primary_pointer_9c_before_status=";
    message += primary_pointer_9c_before_copied ? L"copied" : L"unavailable";
    message += L" primary_pointer_9c_after=";
    message += HexPtr(primary_pointer_9c_after);
    message += L" primary_pointer_9c_after_status=";
    message += primary_pointer_9c_after_copied ? L"copied" : L"unavailable";
    message += L" owner_field_a0_before_status=";
    message += owner_field_a0_before_copied ? L"copied" : L"unavailable";
    if (owner_field_a0_before_copied) {
        message += L" owner_field_a0_before=";
        message += Hex32(owner_field_a0_before);
    }
    message += L" owner_field_a0_after_status=";
    message += owner_field_a0_after_copied ? L"copied" : L"unavailable";
    if (owner_field_a0_after_copied) {
        message += L" owner_field_a0_after=";
        message += Hex32(owner_field_a0_after);
    }
    message += L" fallback_pointer_144_before=";
    message += HexPtr(fallback_pointer_144_before);
    message += L" fallback_pointer_144_before_status=";
    message += fallback_pointer_144_before_copied ? L"copied" : L"unavailable";
    message += L" fallback_pointer_144_after=";
    message += HexPtr(fallback_pointer_144_after);
    message += L" fallback_pointer_144_after_status=";
    message += fallback_pointer_144_after_copied ? L"copied" : L"unavailable";
    message += L" resolved_object_before=";
    message += HexPtr(resolved_object_before);
    message += L" resolved_object_after=";
    message += HexPtr(resolved_object_after);
    message += L" resolved_pointer_source_after=";
    if (resolved_object_after == primary_pointer_9c_after && primary_pointer_9c_after != 0) {
        message += L"primary_9c";
    } else if (
        resolved_object_after == fallback_pointer_144_after && fallback_pointer_144_after != 0) {
        message += L"fallback_144";
    } else if (resolved_object_after == 0) {
        message += L"null";
    } else {
        message += L"other";
    }
    message += L" resolved_flag_11a_after_status=";
    message += resolved_flag_11a_after_copied ? L"copied" : L"unavailable";
    if (resolved_flag_11a_after_copied) {
        message += L" resolved_flag_11a_after=";
        message += std::to_wstring(resolved_flag_11a_after);
    }
    message += L" resolved_field_521_after_status=";
    message += resolved_field_521_after_copied ? L"copied" : L"unavailable";
    if (resolved_field_521_after_copied) {
        message += L" resolved_field_521_after=";
        message += std::to_wstring(resolved_field_521_after);
    }
    message += L" context_bytes_before_status=";
    message += context_bytes_before_copied ? L"copied" : L"unavailable";
    if (context_bytes_before_copied) {
        message += L" context_bytes_before=";
        message += HexBytes(context_bytes_before.data(), context_bytes_before.size());
    }
    message += L" context_bytes_after_status=";
    message += context_bytes_after_copied ? L"copied" : L"unavailable";
    if (context_bytes_after_copied) {
        message += L" context_bytes_after=";
        message += HexBytes(context_bytes_after.data(), context_bytes_after.size());
    }

    monomyth::logger::Log(message);
}

void LogEverQuestLMouseUpTrace(
    const wchar_t* phase,
    void* this_context,
    std::uintptr_t caller_return_address,
    void* point_like,
    const ClientPointWire& point,
    bool point_copied,
    std::uintptr_t drag_context_pointer,
    bool drag_context_pointer_copied,
    const std::array<std::uint8_t, 24>& drag_context_bytes,
    bool drag_context_bytes_copied,
    std::uintptr_t drag_flags_pointer,
    bool drag_flags_pointer_copied,
    std::uint8_t drag_flag_10,
    bool drag_flag_10_copied,
    std::uint8_t drag_flag_11,
    bool drag_flag_11_copied,
    std::uintptr_t active_window_pointer,
    bool active_window_pointer_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=EverQuestLMouseUp";
    message += L" probe_address=";
    message += HexPtr(module_base + kEverQuestLMouseUpTargetRva);
    message += L" probe_rva=";
    message += Hex32(kEverQuestLMouseUpTargetRva);
    message += L" phase=";
    message += phase == nullptr ? L"unknown" : phase;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" point_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(point_like));
    message += L" point_status=";
    message += point_copied ? L"copied" : L"unavailable";
    if (point_copied) {
        message += L" point_x=";
        message += std::to_wstring(point.x);
        message += L" point_y=";
        message += std::to_wstring(point.y);
    }
    message += L" drag_context_pointer=";
    message += HexPtr(drag_context_pointer);
    message += L" drag_context_pointer_status=";
    message += drag_context_pointer_copied ? L"copied" : L"unavailable";
    message += L" drag_context_bytes_status=";
    message += drag_context_bytes_copied ? L"copied" : L"unavailable";
    if (drag_context_bytes_copied) {
        message += L" drag_context_bytes=";
        message += HexBytes(drag_context_bytes.data(), drag_context_bytes.size());
    }
    message += L" drag_flags_pointer=";
    message += HexPtr(drag_flags_pointer);
    message += L" drag_flags_pointer_status=";
    message += drag_flags_pointer_copied ? L"copied" : L"unavailable";
    message += L" drag_flag_10_status=";
    message += drag_flag_10_copied ? L"copied" : L"unavailable";
    if (drag_flag_10_copied) {
        message += L" drag_flag_10=";
        message += std::to_wstring(drag_flag_10);
    }
    message += L" drag_flag_11_status=";
    message += drag_flag_11_copied ? L"copied" : L"unavailable";
    if (drag_flag_11_copied) {
        message += L" drag_flag_11=";
        message += std::to_wstring(drag_flag_11);
    }
    message += L" active_window_pointer=";
    message += HexPtr(active_window_pointer);
    message += L" active_window_pointer_status=";
    message += active_window_pointer_copied ? L"copied" : L"unavailable";
    monomyth::logger::Log(message);
}

void LogCXWndHandleLButtonUpTrace(
    const wchar_t* phase,
    void* this_context,
    std::uintptr_t caller_return_address,
    void* point_like,
    const ClientPointWire& point,
    bool point_copied,
    std::uint32_t flags_like,
    std::uint32_t original_result,
    bool result_present,
    std::uintptr_t vtable_pointer,
    bool vtable_pointer_copied,
    const std::array<std::uint8_t, 24>& context_bytes,
    bool context_bytes_copied,
    std::uintptr_t drag_context_pointer,
    bool drag_context_pointer_copied,
    const std::array<std::uint8_t, 24>& drag_context_bytes,
    bool drag_context_bytes_copied,
    std::uintptr_t drag_flags_pointer,
    bool drag_flags_pointer_copied,
    std::uint8_t drag_flag_10,
    bool drag_flag_10_copied,
    std::uint8_t drag_flag_11,
    bool drag_flag_11_copied,
    std::uintptr_t active_window_pointer,
    bool active_window_pointer_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=CXWndHandleLButtonUp";
    message += L" probe_address=";
    message += HexPtr(module_base + kCXWndHandleLButtonUpTargetRva);
    message += L" probe_rva=";
    message += Hex32(kCXWndHandleLButtonUpTargetRva);
    message += L" phase=";
    message += phase == nullptr ? L"unknown" : phase;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" point_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(point_like));
    message += L" point_status=";
    message += point_copied ? L"copied" : L"unavailable";
    if (point_copied) {
        message += L" point_x=";
        message += std::to_wstring(point.x);
        message += L" point_y=";
        message += std::to_wstring(point.y);
    }
    message += L" flags_like=";
    message += Hex32(flags_like);
    message += L" result_status=";
    message += result_present ? L"present" : L"omitted";
    if (result_present) {
        message += L" original_result=";
        message += Hex32(original_result);
    }
    message += L" vtable_pointer=";
    message += HexPtr(vtable_pointer);
    message += L" vtable_pointer_status=";
    message += vtable_pointer_copied ? L"copied" : L"unavailable";
    message += L" context_bytes_status=";
    message += context_bytes_copied ? L"copied" : L"unavailable";
    if (context_bytes_copied) {
        message += L" context_bytes=";
        message += HexBytes(context_bytes.data(), context_bytes.size());
    }
    message += L" drag_context_pointer=";
    message += HexPtr(drag_context_pointer);
    message += L" drag_context_pointer_status=";
    message += drag_context_pointer_copied ? L"copied" : L"unavailable";
    message += L" drag_context_bytes_status=";
    message += drag_context_bytes_copied ? L"copied" : L"unavailable";
    if (drag_context_bytes_copied) {
        message += L" drag_context_bytes=";
        message += HexBytes(drag_context_bytes.data(), drag_context_bytes.size());
    }
    message += L" drag_flags_pointer=";
    message += HexPtr(drag_flags_pointer);
    message += L" drag_flags_pointer_status=";
    message += drag_flags_pointer_copied ? L"copied" : L"unavailable";
    message += L" drag_flag_10_status=";
    message += drag_flag_10_copied ? L"copied" : L"unavailable";
    if (drag_flag_10_copied) {
        message += L" drag_flag_10=";
        message += std::to_wstring(drag_flag_10);
    }
    message += L" drag_flag_11_status=";
    message += drag_flag_11_copied ? L"copied" : L"unavailable";
    if (drag_flag_11_copied) {
        message += L" drag_flag_11=";
        message += std::to_wstring(drag_flag_11);
    }
    message += L" active_window_pointer=";
    message += HexPtr(active_window_pointer);
    message += L" active_window_pointer_status=";
    message += active_window_pointer_copied ? L"copied" : L"unavailable";
    monomyth::logger::Log(message);
}

void LogInventoryWindowWndNotificationTrace(
    const wchar_t* phase,
    void* this_context,
    std::uintptr_t caller_return_address,
    void* sender_window,
    std::uint32_t notification_code,
    void* payload_like,
    std::uint32_t original_result,
    bool result_present,
    std::uintptr_t this_vtable,
    bool this_vtable_copied,
    std::uintptr_t sender_vtable,
    bool sender_vtable_copied,
    const std::array<std::uint8_t, 24>& this_bytes,
    bool this_bytes_copied,
    const std::array<std::uint8_t, 24>& sender_bytes,
    bool sender_bytes_copied,
    std::uintptr_t drag_context_pointer,
    bool drag_context_pointer_copied,
    const std::array<std::uint8_t, 24>& drag_context_bytes,
    bool drag_context_bytes_copied,
    std::uintptr_t drag_flags_pointer,
    bool drag_flags_pointer_copied,
    std::uint8_t drag_flag_10,
    bool drag_flag_10_copied,
    std::uint8_t drag_flag_11,
    bool drag_flag_11_copied,
    std::uintptr_t active_window_pointer,
    bool active_window_pointer_copied,
    std::uintptr_t window_state_2d4_pointer,
    bool window_state_2d4_pointer_copied,
    std::uint32_t window_field_3d8,
    bool window_field_3d8_copied,
    std::uint8_t window_field_125,
    bool window_field_125_copied,
    const std::array<std::uint8_t, 32>& window_state_2d4_bytes,
    bool window_state_2d4_bytes_copied,
    std::uint8_t window_state_byte_26,
    bool window_state_byte_26_copied,
    std::uint8_t window_state_byte_27,
    bool window_state_byte_27_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InventoryWindowWndNotification";
    message += L" probe_address=";
    message += HexPtr(module_base + kInventoryWindowWndNotificationTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInventoryWindowWndNotificationTargetRva);
    message += L" phase=";
    message += phase == nullptr ? L"unknown" : phase;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" sender_window=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(sender_window));
    message += L" notification_code=";
    message += Hex32(notification_code);
    message += L" payload_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(payload_like));
    message += L" result_status=";
    message += result_present ? L"present" : L"omitted";
    if (result_present) {
        message += L" original_result=";
        message += Hex32(original_result);
    }
    message += L" this_vtable=";
    message += HexPtr(this_vtable);
    message += L" this_vtable_status=";
    message += this_vtable_copied ? L"copied" : L"unavailable";
    message += L" sender_vtable=";
    message += HexPtr(sender_vtable);
    message += L" sender_vtable_status=";
    message += sender_vtable_copied ? L"copied" : L"unavailable";
    message += L" this_bytes_status=";
    message += this_bytes_copied ? L"copied" : L"unavailable";
    if (this_bytes_copied) {
        message += L" this_bytes=";
        message += HexBytes(this_bytes.data(), this_bytes.size());
    }
    message += L" sender_bytes_status=";
    message += sender_bytes_copied ? L"copied" : L"unavailable";
    if (sender_bytes_copied) {
        message += L" sender_bytes=";
        message += HexBytes(sender_bytes.data(), sender_bytes.size());
    }
    message += L" drag_context_pointer=";
    message += HexPtr(drag_context_pointer);
    message += L" drag_context_pointer_status=";
    message += drag_context_pointer_copied ? L"copied" : L"unavailable";
    message += L" drag_context_bytes_status=";
    message += drag_context_bytes_copied ? L"copied" : L"unavailable";
    if (drag_context_bytes_copied) {
        message += L" drag_context_bytes=";
        message += HexBytes(drag_context_bytes.data(), drag_context_bytes.size());
    }
    message += L" drag_flags_pointer=";
    message += HexPtr(drag_flags_pointer);
    message += L" drag_flags_pointer_status=";
    message += drag_flags_pointer_copied ? L"copied" : L"unavailable";
    message += L" drag_flag_10_status=";
    message += drag_flag_10_copied ? L"copied" : L"unavailable";
    if (drag_flag_10_copied) {
        message += L" drag_flag_10=";
        message += std::to_wstring(drag_flag_10);
    }
    message += L" drag_flag_11_status=";
    message += drag_flag_11_copied ? L"copied" : L"unavailable";
    if (drag_flag_11_copied) {
        message += L" drag_flag_11=";
        message += std::to_wstring(drag_flag_11);
    }
    message += L" active_window_pointer=";
    message += HexPtr(active_window_pointer);
    message += L" active_window_pointer_status=";
    message += active_window_pointer_copied ? L"copied" : L"unavailable";
    message += L" window_state_2d4_pointer=";
    message += HexPtr(window_state_2d4_pointer);
    message += L" window_state_2d4_pointer_status=";
    message += window_state_2d4_pointer_copied ? L"copied" : L"unavailable";
    message += L" window_field_3d8_status=";
    message += window_field_3d8_copied ? L"copied" : L"unavailable";
    if (window_field_3d8_copied) {
        message += L" window_field_3d8=";
        message += Hex32(window_field_3d8);
    }
    message += L" window_field_125_status=";
    message += window_field_125_copied ? L"copied" : L"unavailable";
    if (window_field_125_copied) {
        message += L" window_field_125=";
        message += std::to_wstring(window_field_125);
    }
    message += L" window_state_2d4_bytes_status=";
    message += window_state_2d4_bytes_copied ? L"copied" : L"unavailable";
    if (window_state_2d4_bytes_copied) {
        message += L" window_state_2d4_bytes=";
        message += HexBytes(window_state_2d4_bytes.data(), window_state_2d4_bytes.size());
    }
    message += L" window_state_byte_26_status=";
    message += window_state_byte_26_copied ? L"copied" : L"unavailable";
    if (window_state_byte_26_copied) {
        message += L" window_state_byte_26=";
        message += Hex32(window_state_byte_26);
    }
    message += L" window_state_byte_27_status=";
    message += window_state_byte_27_copied ? L"copied" : L"unavailable";
    if (window_state_byte_27_copied) {
        message += L" window_state_byte_27=";
        message += Hex32(window_state_byte_27);
    }
    monomyth::logger::Log(message);
}

void LogInvSlotWndHandleLButtonTrace(
    const wchar_t* target_label,
    std::uint32_t target_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    void* point_like,
    const ClientPointWire& point_value,
    bool point_value_copied,
    std::uint32_t flags_like,
    std::uint32_t original_result,
    std::uint32_t field_29c_before,
    bool field_29c_before_copied,
    std::uint32_t field_29c_after,
    bool field_29c_after_copied,
    std::uintptr_t field_290_before,
    bool field_290_before_copied,
    std::uintptr_t field_290_after,
    bool field_290_after_copied,
    std::uintptr_t field_270_before,
    bool field_270_before_copied,
    std::uintptr_t field_270_after,
    bool field_270_after_copied,
    std::uint8_t field_28c_before,
    bool field_28c_before_copied,
    std::uint8_t field_28c_after,
    bool field_28c_after_copied,
    const std::array<std::uint8_t, 24>& this_bytes_before,
    bool this_bytes_before_copied,
    const std::array<std::uint8_t, 24>& child_bytes_before,
    bool child_bytes_before_copied,
    std::uintptr_t drag_context_pointer,
    bool drag_context_pointer_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=";
    message += target_label == nullptr ? L"InvSlotWndHandleLButton" : target_label;
    message += L" probe_address=";
    message += HexPtr(module_base + target_rva);
    message += L" probe_rva=";
    message += Hex32(target_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" point_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(point_like));
    message += L" point_status=";
    message += point_value_copied ? L"copied" : L"unavailable";
    if (point_value_copied) {
        message += L" point_x=";
        message += std::to_wstring(point_value.x);
        message += L" point_y=";
        message += std::to_wstring(point_value.y);
    }
    message += L" flags_like=";
    message += Hex32(flags_like);
    message += L" original_result=";
    message += Hex32(original_result);
    message += L" field_29c_before_status=";
    message += field_29c_before_copied ? L"copied" : L"unavailable";
    if (field_29c_before_copied) {
        message += L" field_29c_before=";
        message += Hex32(field_29c_before);
    }
    message += L" field_29c_after_status=";
    message += field_29c_after_copied ? L"copied" : L"unavailable";
    if (field_29c_after_copied) {
        message += L" field_29c_after=";
        message += Hex32(field_29c_after);
    }
    message += L" field_290_before=";
    message += HexPtr(field_290_before);
    message += L" field_290_before_status=";
    message += field_290_before_copied ? L"copied" : L"unavailable";
    message += L" field_290_after=";
    message += HexPtr(field_290_after);
    message += L" field_290_after_status=";
    message += field_290_after_copied ? L"copied" : L"unavailable";
    message += L" field_270_before=";
    message += HexPtr(field_270_before);
    message += L" field_270_before_status=";
    message += field_270_before_copied ? L"copied" : L"unavailable";
    message += L" field_270_after=";
    message += HexPtr(field_270_after);
    message += L" field_270_after_status=";
    message += field_270_after_copied ? L"copied" : L"unavailable";
    message += L" field_28c_before_status=";
    message += field_28c_before_copied ? L"copied" : L"unavailable";
    if (field_28c_before_copied) {
        message += L" field_28c_before=";
        message += Hex32(field_28c_before);
    }
    message += L" field_28c_after_status=";
    message += field_28c_after_copied ? L"copied" : L"unavailable";
    if (field_28c_after_copied) {
        message += L" field_28c_after=";
        message += Hex32(field_28c_after);
    }
    message += L" this_bytes_before_status=";
    message += this_bytes_before_copied ? L"copied" : L"unavailable";
    if (this_bytes_before_copied) {
        message += L" this_bytes_before=";
        message += HexBytes(this_bytes_before.data(), this_bytes_before.size());
    }
    message += L" child_bytes_before_status=";
    message += child_bytes_before_copied ? L"copied" : L"unavailable";
    if (child_bytes_before_copied) {
        message += L" child_bytes_before=";
        message += HexBytes(child_bytes_before.data(), child_bytes_before.size());
    }
    message += L" drag_context_pointer=";
    message += HexPtr(drag_context_pointer);
    message += L" drag_context_pointer_status=";
    message += drag_context_pointer_copied ? L"copied" : L"unavailable";
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreTrace(
    std::uintptr_t caller_return_address,
    void* this_context,
    void* point_like,
    const ClientPointWire& point_value,
    bool point_value_copied,
    std::uint32_t slot_like,
    std::uint32_t held_flag_like,
    std::uint8_t field_10_before,
    bool field_10_before_copied,
    std::uint8_t field_10_after,
    bool field_10_after_copied,
    std::uintptr_t field_04_before,
    bool field_04_before_copied,
    std::uintptr_t field_04_after,
    bool field_04_after_copied,
    std::uint32_t slot_word_264_before,
    bool slot_word_264_before_copied,
    std::uint32_t slot_word_264_after,
    bool slot_word_264_after_copied,
    std::uint32_t slot_word_268_before,
    bool slot_word_268_before_copied,
    std::uint32_t slot_word_268_after,
    bool slot_word_268_after_copied,
    std::uint32_t slot_word_26c_before,
    bool slot_word_26c_before_copied,
    std::uint32_t slot_word_26c_after,
    bool slot_word_26c_after_copied,
    const std::array<std::uint8_t, 24>& this_bytes_before,
    bool this_bytes_before_copied,
    const std::array<std::uint8_t, 24>& field_04_bytes_before,
    bool field_04_bytes_before_copied,
    std::uintptr_t drag_context_pointer,
    bool drag_context_pointer_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCore";
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreTargetRva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" point_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(point_like));
    message += L" point_status=";
    message += point_value_copied ? L"copied" : L"unavailable";
    if (point_value_copied) {
        message += L" point_x=";
        message += std::to_wstring(point_value.x);
        message += L" point_y=";
        message += std::to_wstring(point_value.y);
    }
    message += L" slot_like=";
    message += Hex32(slot_like);
    message += L" held_flag_like=";
    message += Hex32(held_flag_like);
    message += L" field_10_before_status=";
    message += field_10_before_copied ? L"copied" : L"unavailable";
    if (field_10_before_copied) {
        message += L" field_10_before=";
        message += Hex32(field_10_before);
    }
    message += L" field_10_after_status=";
    message += field_10_after_copied ? L"copied" : L"unavailable";
    if (field_10_after_copied) {
        message += L" field_10_after=";
        message += Hex32(field_10_after);
    }
    message += L" field_04_before=";
    message += HexPtr(field_04_before);
    message += L" field_04_before_status=";
    message += field_04_before_copied ? L"copied" : L"unavailable";
    message += L" field_04_after=";
    message += HexPtr(field_04_after);
    message += L" field_04_after_status=";
    message += field_04_after_copied ? L"copied" : L"unavailable";
    message += L" slot_word_264_before_status=";
    message += slot_word_264_before_copied ? L"copied" : L"unavailable";
    if (slot_word_264_before_copied) {
        message += L" slot_word_264_before=";
        message += Hex32(slot_word_264_before);
    }
    message += L" slot_word_264_after_status=";
    message += slot_word_264_after_copied ? L"copied" : L"unavailable";
    if (slot_word_264_after_copied) {
        message += L" slot_word_264_after=";
        message += Hex32(slot_word_264_after);
    }
    message += L" slot_word_268_before_status=";
    message += slot_word_268_before_copied ? L"copied" : L"unavailable";
    if (slot_word_268_before_copied) {
        message += L" slot_word_268_before=";
        message += Hex32(slot_word_268_before);
    }
    message += L" slot_word_268_after_status=";
    message += slot_word_268_after_copied ? L"copied" : L"unavailable";
    if (slot_word_268_after_copied) {
        message += L" slot_word_268_after=";
        message += Hex32(slot_word_268_after);
    }
    message += L" slot_word_26c_before_status=";
    message += slot_word_26c_before_copied ? L"copied" : L"unavailable";
    if (slot_word_26c_before_copied) {
        message += L" slot_word_26c_before=";
        message += Hex32(slot_word_26c_before);
    }
    message += L" slot_word_26c_after_status=";
    message += slot_word_26c_after_copied ? L"copied" : L"unavailable";
    if (slot_word_26c_after_copied) {
        message += L" slot_word_26c_after=";
        message += Hex32(slot_word_26c_after);
    }
    message += L" this_bytes_before_status=";
    message += this_bytes_before_copied ? L"copied" : L"unavailable";
    if (this_bytes_before_copied) {
        message += L" this_bytes_before=";
        message += HexBytes(this_bytes_before.data(), this_bytes_before.size());
    }
    message += L" field_04_bytes_before_status=";
    message += field_04_bytes_before_copied ? L"copied" : L"unavailable";
    if (field_04_bytes_before_copied) {
        message += L" field_04_bytes_before=";
        message += HexBytes(field_04_bytes_before.data(), field_04_bytes_before.size());
    }
    message += L" drag_context_pointer=";
    message += HexPtr(drag_context_pointer);
    message += L" drag_context_pointer_status=";
    message += drag_context_pointer_copied ? L"copied" : L"unavailable";
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCorePrecheckTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const std::array<std::uint8_t, 24>& context_bytes,
    bool context_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCorePrecheck";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCorePrecheckTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCorePrecheckTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" early_return_taken=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" context_bytes_status=";
    message += context_bytes_copied ? L"copied" : L"unavailable";
    if (context_bytes_copied) {
        message += L" context_bytes=";
        message += HexBytes(context_bytes.data(), context_bytes.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreModeBitsTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    std::uint32_t original_result,
    const std::array<std::uint8_t, 24>& context_bytes,
    bool context_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreModeBits";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreModeBitsTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += Hex32(original_result);
    message += L" bit0_set=";
    message += (original_result & 0x1u) != 0 ? L"true" : L"false";
    message += L" bit1_set=";
    message += (original_result & 0x2u) != 0 ? L"true" : L"false";
    message += L" bits_0c_nonzero=";
    message += (original_result & 0xcu) != 0 ? L"true" : L"false";
    message += L" context_bytes_status=";
    message += context_bytes_copied ? L"copied" : L"unavailable";
    if (context_bytes_copied) {
        message += L" context_bytes=";
        message += HexBytes(context_bytes.data(), context_bytes.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLocalGateTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    const void* slot_record_pointer,
    std::uint8_t original_result,
    const ClientInventorySlotWire& slot_record,
    bool slot_record_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire)>& slot_record_bytes,
    bool slot_record_bytes_copied,
    const InvSlotHandleLButtonCoreSlotContextSnapshot& slot_context) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreLocalGate";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreLocalGateTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreLocalGateTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" slot_record_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_record_pointer));
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" early_return_taken=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" slot_record_input_status=";
    message += slot_record_copied ? L"copied" : L"unavailable";
    if (slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"slot_record_input", slot_record);
    }
    message += L" slot_record_input_bytes_status=";
    message += slot_record_bytes_copied ? L"copied" : L"unavailable";
    if (slot_record_bytes_copied) {
        message += L" slot_record_input_bytes=";
        message += HexBytes(slot_record_bytes.data(), slot_record_bytes.size());
    }
    message += L" child_pointer=";
    message += HexPtr(slot_context.child_pointer);
    message += L" child_pointer_status=";
    message += slot_context.child_pointer_copied ? L"copied" : L"unavailable";
    message += L" core_slot_record_status=";
    message += slot_context.slot_record_copied ? L"copied" : L"unavailable";
    if (slot_context.slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"core_slot_record", slot_context.slot_record);
    }
    message += L" child_bytes_status=";
    message += slot_context.child_bytes_copied ? L"copied" : L"unavailable";
    if (slot_context.child_bytes_copied) {
        message += L" child_bytes=";
        message += HexBytes(slot_context.child_bytes.data(), slot_context.child_bytes.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreResolveObjectTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    void* output_pointer,
    void* original_result,
    std::uintptr_t output_object_after,
    bool output_object_after_copied,
    const std::array<std::uint8_t, 24>& output_object_bytes_after,
    bool output_object_bytes_after_copied,
    const InvSlotHandleLButtonCoreSlotContextSnapshot& slot_context) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreResolveObject";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreResolveObjectTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreResolveObjectTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" output_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(output_pointer));
    message += L" original_result=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" output_object_after=";
    message += HexPtr(output_object_after);
    message += L" output_object_after_status=";
    message += output_object_after_copied ? L"copied" : L"unavailable";
    message += L" output_object_nonzero=";
    message += output_object_after_copied && output_object_after != 0 ? L"true" : L"false";
    message += L" core_slot_record_status=";
    message += slot_context.slot_record_copied ? L"copied" : L"unavailable";
    if (slot_context.slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"core_slot_record", slot_context.slot_record);
    }
    message += L" output_object_bytes_after_status=";
    message += output_object_bytes_after_copied ? L"copied" : L"unavailable";
    if (output_object_bytes_after_copied) {
        message += L" output_object_bytes_after=";
        message += HexBytes(output_object_bytes_after.data(), output_object_bytes_after.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreResolvedKindTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    int original_result,
    const std::array<std::uint8_t, 24>& context_bytes,
    bool context_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreResolvedKind";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreResolvedKindTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreResolvedKindTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" equals_two=";
    message += original_result == 2 ? L"true" : L"false";
    message += L" context_bytes_status=";
    message += context_bytes_copied ? L"copied" : L"unavailable";
    if (context_bytes_copied) {
        message += L" context_bytes=";
        message += HexBytes(context_bytes.data(), context_bytes.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreResolvedFlagTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const std::array<std::uint8_t, 24>& context_bytes,
    bool context_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreResolvedFlag";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreResolvedFlagTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreResolvedFlagTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" equals_one=";
    message += original_result == 1 ? L"true" : L"false";
    message += L" equals_two=";
    message += original_result == 2 ? L"true" : L"false";
    message += L" context_bytes_status=";
    message += context_bytes_copied ? L"copied" : L"unavailable";
    if (context_bytes_copied) {
        message += L" context_bytes=";
        message += HexBytes(context_bytes.data(), context_bytes.size());
    }
    monomyth::logger::Log(message);
}

void AppendInvSlotHandleLButtonCoreLateManagerFields(
    std::wstring* message,
    const wchar_t* prefix,
    const InvSlotHandleLButtonCoreLateManagerSnapshot& snapshot) {
    if (message == nullptr || prefix == nullptr) {
        return;
    }

    const struct {
        const wchar_t* field;
        bool copied;
        std::uint32_t value;
        bool render_hex;
    } value_fields[] = {
        {L"23c", snapshot.field_23c_copied, snapshot.field_23c, false},
        {L"240", snapshot.field_240_copied, snapshot.field_240, false},
        {L"244", snapshot.field_244_copied, snapshot.field_244, false},
    };
    for (const auto& field : value_fields) {
        message->append(L" ");
        message->append(prefix);
        message->append(L"_field_");
        message->append(field.field);
        message->append(L"_status=");
        message->append(field.copied ? L"copied" : L"unavailable");
        if (field.copied) {
            message->append(L" ");
            message->append(prefix);
            message->append(L"_field_");
            message->append(field.field);
            message->append(L"=");
            message->append(field.render_hex ? Hex32(field.value) : std::to_wstring(field.value));
        }
    }

    const struct {
        const wchar_t* field;
        bool copied;
        std::uintptr_t value;
    } pointer_fields[] = {
        {L"248", snapshot.field_248_copied, snapshot.field_248},
        {L"254", snapshot.field_254_copied, snapshot.field_254},
        {L"258", snapshot.field_258_copied, snapshot.field_258},
        {L"25c", snapshot.field_25c_copied, snapshot.field_25c},
        {L"260", snapshot.field_260_copied, snapshot.field_260},
        {L"264", snapshot.field_264_copied, snapshot.field_264},
        {L"2a8", snapshot.field_2a8_copied, snapshot.field_2a8},
    };
    for (const auto& field : pointer_fields) {
        message->append(L" ");
        message->append(prefix);
        message->append(L"_field_");
        message->append(field.field);
        message->append(L"_status=");
        message->append(field.copied ? L"copied" : L"unavailable");
        if (field.copied) {
            message->append(L" ");
            message->append(prefix);
            message->append(L"_field_");
            message->append(field.field);
            message->append(L"=");
            message->append(HexPtr(field.value));
        }
    }
}

void LogInvSlotHandleLButtonCoreLateSlot17GateTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    const void* slot_record_pointer,
    std::uint8_t original_result,
    const ClientInventorySlotWire& slot_record,
    bool slot_record_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire)>& slot_record_bytes,
    bool slot_record_bytes_copied,
    const InvSlotHandleLButtonCoreLateManagerSnapshot& manager_before,
    const InvSlotHandleLButtonCoreLateManagerSnapshot& manager_after) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreLateSlot17Gate";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreLateSlot17GateTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreLateSlot17GateTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" slot_record_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_record_pointer));
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" slot_record_input_status=";
    message += slot_record_copied ? L"copied" : L"unavailable";
    if (slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"slot_record_input", slot_record);
    }
    message += L" slot_record_input_bytes_status=";
    message += slot_record_bytes_copied ? L"copied" : L"unavailable";
    if (slot_record_bytes_copied) {
        message += L" slot_record_input_bytes=";
        message += HexBytes(slot_record_bytes.data(), slot_record_bytes.size());
    }
    AppendInvSlotHandleLButtonCoreLateManagerFields(&message, L"manager_before", manager_before);
    AppendInvSlotHandleLButtonCoreLateManagerFields(&message, L"manager_after", manager_after);
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCorePostResolveGateTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* slot_context_like,
    std::uint32_t mode_like,
    std::uintptr_t caller_return_address,
    std::uint32_t original_result,
    const InvSlotHandleLButtonCorePostResolveGateSnapshot& snapshot_before,
    const InvSlotHandleLButtonCorePostResolveGateSnapshot& snapshot_after) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCorePostResolveGate";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCorePostResolveGateTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCorePostResolveGateTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" slot_context_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_context_like));
    message += L" mode_like=";
    message += std::to_wstring(mode_like);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";

    const auto append_snapshot = [&message](const wchar_t* prefix,
                                            const InvSlotHandleLButtonCorePostResolveGateSnapshot& snapshot) {
        message += L" ";
        message += prefix;
        message += L"_slot_context_field_244_status=";
        message += snapshot.slot_context_field_244_copied ? L"copied" : L"unavailable";
        if (snapshot.slot_context_field_244_copied) {
            message += L" ";
            message += prefix;
            message += L"_slot_context_field_244=";
            message += std::to_wstring(snapshot.slot_context_field_244);
        }
        message += L" ";
        message += prefix;
        message += L"_slot_context_bytes_status=";
        message += snapshot.slot_context_bytes_copied ? L"copied" : L"unavailable";
        if (snapshot.slot_context_bytes_copied) {
            message += L" ";
            message += prefix;
            message += L"_slot_context_bytes=";
            message += HexBytes(
                snapshot.slot_context_bytes.data(),
                snapshot.slot_context_bytes.size());
        }
        message += L" ";
        message += prefix;
        message += L"_drag_context_root_status=";
        message += snapshot.drag_context_root_copied ? L"copied" : L"unavailable";
        if (snapshot.drag_context_root_copied) {
            message += L" ";
            message += prefix;
            message += L"_drag_context_root=";
            message += HexPtr(snapshot.drag_context_root);
        }
        message += L" ";
        message += prefix;
        message += L"_drag_context_manager_status=";
        message += snapshot.drag_context_manager_copied ? L"computed" : L"unavailable";
        if (snapshot.drag_context_manager_copied) {
            message += L" ";
            message += prefix;
            message += L"_drag_context_manager=";
            message += HexPtr(snapshot.drag_context_manager);
        }
        message += L" ";
        message += prefix;
        message += L"_drag_context_manager_bytes_status=";
        message += snapshot.drag_context_manager_bytes_copied ? L"copied" : L"unavailable";
        if (snapshot.drag_context_manager_bytes_copied) {
            message += L" ";
            message += prefix;
            message += L"_drag_context_manager_bytes=";
            message += HexBytes(
                snapshot.drag_context_manager_bytes.data(),
                snapshot.drag_context_manager_bytes.size());
        }
        message += L" ";
        message += prefix;
        message += L"_state_root_status=";
        message += snapshot.state_root_copied ? L"copied" : L"unavailable";
        if (snapshot.state_root_copied) {
            message += L" ";
            message += prefix;
            message += L"_state_root=";
            message += HexPtr(snapshot.state_root);
        }
        message += L" ";
        message += prefix;
        message += L"_state_field_448_status=";
        message += snapshot.state_field_448_copied ? L"copied" : L"unavailable";
        if (snapshot.state_field_448_copied) {
            message += L" ";
            message += prefix;
            message += L"_state_field_448=";
            message += std::to_wstring(snapshot.state_field_448);
        }
        message += L" ";
        message += prefix;
        message += L"_state_field_eb8_status=";
        message += snapshot.state_field_eb8_copied ? L"copied" : L"unavailable";
        if (snapshot.state_field_eb8_copied) {
            message += L" ";
            message += prefix;
            message += L"_state_field_eb8=";
            message += std::to_wstring(snapshot.state_field_eb8);
        }
        message += L" ";
        message += prefix;
        message += L"_dispatcher_object_status=";
        message += snapshot.dispatcher_object_copied ? L"copied" : L"unavailable";
        if (snapshot.dispatcher_object_copied) {
            message += L" ";
            message += prefix;
            message += L"_dispatcher_object=";
            message += HexPtr(snapshot.dispatcher_object);
        }
        message += L" ";
        message += prefix;
        message += L"_dispatcher_vtable_status=";
        message += snapshot.dispatcher_vtable_copied ? L"copied" : L"unavailable";
        if (snapshot.dispatcher_vtable_copied) {
            message += L" ";
            message += prefix;
            message += L"_dispatcher_vtable=";
            message += HexPtr(snapshot.dispatcher_vtable);
        }
        message += L" ";
        message += prefix;
        message += L"_dispatcher_virtual_0c_status=";
        message += snapshot.dispatcher_virtual_0c_copied ? L"copied" : L"unavailable";
        if (snapshot.dispatcher_virtual_0c_copied) {
            message += L" ";
            message += prefix;
            message += L"_dispatcher_virtual_0c=";
            message += HexPtr(snapshot.dispatcher_virtual_0c);
        }
    };

    append_snapshot(L"before", snapshot_before);
    append_snapshot(L"after", snapshot_after);
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreAltDispatchTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    const void* slot_record_pointer,
    std::uint32_t mode_flag,
    std::uintptr_t caller_return_address,
    const ClientInventorySlotWire& slot_record,
    bool slot_record_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire)>& slot_record_bytes,
    bool slot_record_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreAltDispatch";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreAltDispatchTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreAltDispatchTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" slot_record_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_record_pointer));
    message += L" mode_flag=";
    message += std::to_wstring(mode_flag);
    message += L" mode_flag_hex=";
    message += Hex32(mode_flag);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" slot_record_input_status=";
    message += slot_record_copied ? L"copied" : L"unavailable";
    if (slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"slot_record_input", slot_record);
    }
    message += L" slot_record_input_bytes_status=";
    message += slot_record_bytes_copied ? L"copied" : L"unavailable";
    if (slot_record_bytes_copied) {
        message += L" slot_record_input_bytes=";
        message += HexBytes(slot_record_bytes.data(), slot_record_bytes.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreSlot17MessageTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    std::uint32_t message_id,
    void* text_builder_like,
    std::uint32_t zero_like,
    std::uintptr_t caller_return_address,
    void* original_result,
    const std::array<std::uint8_t, 24>& text_builder_bytes_before,
    bool text_builder_bytes_before_copied,
    const std::array<std::uint8_t, 24>& text_builder_bytes_after,
    bool text_builder_bytes_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreSlot17Message";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreSlot17MessageTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreSlot17MessageTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" message_id=";
    message += Hex32(message_id);
    message += L" text_builder_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(text_builder_like));
    message += L" zero_like=";
    message += std::to_wstring(zero_like);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" text_builder_bytes_before_status=";
    message += text_builder_bytes_before_copied ? L"copied" : L"unavailable";
    if (text_builder_bytes_before_copied) {
        message += L" text_builder_bytes_before=";
        message += HexBytes(text_builder_bytes_before.data(), text_builder_bytes_before.size());
    }
    message += L" text_builder_bytes_after_status=";
    message += text_builder_bytes_after_copied ? L"copied" : L"unavailable";
    if (text_builder_bytes_after_copied) {
        message += L" text_builder_bytes_after=";
        message += HexBytes(text_builder_bytes_after.data(), text_builder_bytes_after.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreGlobalAction49Trace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uint32_t action_id,
    std::uintptr_t caller_return_address,
    const std::array<std::uint8_t, 24>& context_bytes_before,
    bool context_bytes_before_copied,
    const std::array<std::uint8_t, 24>& context_bytes_after,
    bool context_bytes_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreGlobalAction49";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreGlobalAction49TargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreGlobalAction49TargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" action_id=";
    message += std::to_wstring(action_id);
    message += L" action_id_hex=";
    message += Hex32(action_id);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" context_bytes_before_status=";
    message += context_bytes_before_copied ? L"copied" : L"unavailable";
    if (context_bytes_before_copied) {
        message += L" context_bytes_before=";
        message += HexBytes(context_bytes_before.data(), context_bytes_before.size());
    }
    message += L" context_bytes_after_status=";
    message += context_bytes_after_copied ? L"copied" : L"unavailable";
    if (context_bytes_after_copied) {
        message += L" context_bytes_after=";
        message += HexBytes(context_bytes_after.data(), context_bytes_after.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreItemRangeGateTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    std::uint32_t item_id_like,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const InvSlotHandleLButtonCorePostResolveGateSnapshot& snapshot_before,
    const InvSlotHandleLButtonCorePostResolveGateSnapshot& snapshot_after) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreItemRangeGate";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreItemRangeGateTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreItemRangeGateTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" item_id_like=";
    message += std::to_wstring(item_id_like);
    message += L" item_id_like_hex=";
    message += Hex32(item_id_like);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" allowed=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" before_state_field_448_status=";
    message += snapshot_before.state_field_448_copied ? L"copied" : L"unavailable";
    if (snapshot_before.state_field_448_copied) {
        message += L" before_state_field_448=";
        message += std::to_wstring(snapshot_before.state_field_448);
    }
    message += L" before_state_field_eb8_status=";
    message += snapshot_before.state_field_eb8_copied ? L"copied" : L"unavailable";
    if (snapshot_before.state_field_eb8_copied) {
        message += L" before_state_field_eb8=";
        message += std::to_wstring(snapshot_before.state_field_eb8);
    }
    message += L" before_dispatcher_virtual_0c_status=";
    message += snapshot_before.dispatcher_virtual_0c_copied ? L"copied" : L"unavailable";
    if (snapshot_before.dispatcher_virtual_0c_copied) {
        message += L" before_dispatcher_virtual_0c=";
        message += HexPtr(snapshot_before.dispatcher_virtual_0c);
    }
    message += L" after_state_field_448_status=";
    message += snapshot_after.state_field_448_copied ? L"copied" : L"unavailable";
    if (snapshot_after.state_field_448_copied) {
        message += L" after_state_field_448=";
        message += std::to_wstring(snapshot_after.state_field_448);
    }
    message += L" after_state_field_eb8_status=";
    message += snapshot_after.state_field_eb8_copied ? L"copied" : L"unavailable";
    if (snapshot_after.state_field_eb8_copied) {
        message += L" after_state_field_eb8=";
        message += std::to_wstring(snapshot_after.state_field_eb8);
    }
    message += L" after_dispatcher_virtual_0c_status=";
    message += snapshot_after.dispatcher_virtual_0c_copied ? L"copied" : L"unavailable";
    if (snapshot_after.dispatcher_virtual_0c_copied) {
        message += L" after_dispatcher_virtual_0c=";
        message += HexPtr(snapshot_after.dispatcher_virtual_0c);
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCorePostLookupModeGateTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const std::array<std::uint8_t, 24>& context_bytes) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCorePostLookupModeGate";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCorePostLookupModeGateTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCorePostLookupModeGateTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" context_bytes=";
    message += HexBytes(context_bytes.data(), context_bytes.size());
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCorePostLookupUiPulseTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    std::uint32_t target_id,
    std::int32_t value_a,
    std::int32_t value_b,
    std::int32_t x,
    std::int32_t y,
    std::uint32_t one_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b,
    const std::array<std::uint8_t, 24>& context_bytes_before,
    const std::array<std::uint8_t, 24>& context_bytes_after) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCorePostLookupUiPulse";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCorePostLookupUiPulseTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCorePostLookupUiPulseTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" target_id=";
    message += Hex32(target_id);
    message += L" value_a=";
    message += std::to_wstring(value_a);
    message += L" value_b=";
    message += std::to_wstring(value_b);
    message += L" x=";
    message += std::to_wstring(x);
    message += L" y=";
    message += std::to_wstring(y);
    message += L" one_like=";
    message += std::to_wstring(one_like);
    message += L" zero_a=";
    message += std::to_wstring(zero_a);
    message += L" zero_b=";
    message += std::to_wstring(zero_b);
    message += L" context_bytes_before=";
    message += HexBytes(context_bytes_before.data(), context_bytes_before.size());
    message += L" context_bytes_after=";
    message += HexBytes(context_bytes_after.data(), context_bytes_after.size());
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateBranchGateTrace(
    const wchar_t* target_label,
    std::uint32_t target_rva,
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* slot_record_like,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const ClientInventorySlotWire& slot_record,
    bool slot_record_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire)>& slot_record_bytes,
    bool slot_record_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=";
    message += target_label == nullptr ? L"unknown" : target_label;
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + target_rva);
    message += L" probe_rva=";
    message += Hex32(target_rva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" slot_record_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_record_like));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" slot_record_status=";
    message += slot_record_copied ? L"copied" : L"unavailable";
    if (slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"slot_record", slot_record);
    }
    message += L" slot_record_bytes_status=";
    message += slot_record_bytes_copied ? L"copied" : L"unavailable";
    if (slot_record_bytes_copied) {
        message += L" slot_record_bytes=";
        message += HexBytes(slot_record_bytes.data(), slot_record_bytes.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateBranchPrepTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::intptr_t slot_like,
    void* lookup_result_like,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    std::uint32_t lookup_dword0_before,
    bool lookup_dword0_before_copied,
    std::uint32_t lookup_dword0_after,
    bool lookup_dword0_after_copied,
    const std::array<std::uint8_t, 24>& context_bytes_before,
    bool context_bytes_before_copied,
    const std::array<std::uint8_t, 24>& context_bytes_after,
    bool context_bytes_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreLateBranchPrep";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreLateBranchPrepTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreLateBranchPrepTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" slot_like=";
    message += std::to_wstring(slot_like);
    message += L" lookup_result_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(lookup_result_like));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" lookup_dword0_before_status=";
    message += lookup_dword0_before_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_before_copied) {
        message += L" lookup_dword0_before=";
        message += Hex32(lookup_dword0_before);
    }
    message += L" lookup_dword0_after_status=";
    message += lookup_dword0_after_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_after_copied) {
        message += L" lookup_dword0_after=";
        message += Hex32(lookup_dword0_after);
    }
    message += L" context_bytes_before_status=";
    message += context_bytes_before_copied ? L"copied" : L"unavailable";
    if (context_bytes_before_copied) {
        message += L" context_bytes_before=";
        message += HexBytes(context_bytes_before.data(), context_bytes_before.size());
    }
    message += L" context_bytes_after_status=";
    message += context_bytes_after_copied ? L"copied" : L"unavailable";
    if (context_bytes_after_copied) {
        message += L" context_bytes_after=";
        message += HexBytes(context_bytes_after.data(), context_bytes_after.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateBranchDispatchTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    void* lookup_result_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    std::uint32_t lookup_dword0_before,
    bool lookup_dword0_before_copied,
    std::uint32_t lookup_dword0_after,
    bool lookup_dword0_after_copied,
    const std::array<std::uint8_t, 24>& context_bytes_before,
    bool context_bytes_before_copied,
    const std::array<std::uint8_t, 24>& context_bytes_after,
    bool context_bytes_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreLateBranchDispatch";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreLateBranchDispatchTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreLateBranchDispatchTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" lookup_result_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(lookup_result_like));
    message += L" zero_a=";
    message += std::to_wstring(zero_a);
    message += L" zero_b=";
    message += std::to_wstring(zero_b);
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" lookup_dword0_before_status=";
    message += lookup_dword0_before_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_before_copied) {
        message += L" lookup_dword0_before=";
        message += Hex32(lookup_dword0_before);
    }
    message += L" lookup_dword0_after_status=";
    message += lookup_dword0_after_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_after_copied) {
        message += L" lookup_dword0_after=";
        message += Hex32(lookup_dword0_after);
    }
    message += L" context_bytes_before_status=";
    message += context_bytes_before_copied ? L"copied" : L"unavailable";
    if (context_bytes_before_copied) {
        message += L" context_bytes_before=";
        message += HexBytes(context_bytes_before.data(), context_bytes_before.size());
    }
    message += L" context_bytes_after_status=";
    message += context_bytes_after_copied ? L"copied" : L"unavailable";
    if (context_bytes_after_copied) {
        message += L" context_bytes_after=";
        message += HexBytes(context_bytes_after.data(), context_bytes_after.size());
    }
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateSlot17ApplyTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    const void* slot_record_pointer,
    const ClientInventorySlotWire& slot_record,
    bool slot_record_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire)>& slot_record_bytes,
    bool slot_record_bytes_copied,
    const InvSlotHandleLButtonCoreLateManagerSnapshot& manager_before,
    const InvSlotHandleLButtonCoreLateManagerSnapshot& manager_after) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=InvSlotHandleLButtonCoreLateSlot17Apply";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + kInvSlotHandleLButtonCoreLateSlot17ApplyTargetRva);
    message += L" probe_rva=";
    message += Hex32(kInvSlotHandleLButtonCoreLateSlot17ApplyTargetRva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" slot_record_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_record_pointer));
    message += L" slot_record_input_status=";
    message += slot_record_copied ? L"copied" : L"unavailable";
    if (slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"slot_record_input", slot_record);
    }
    message += L" slot_record_input_bytes_status=";
    message += slot_record_bytes_copied ? L"copied" : L"unavailable";
    if (slot_record_bytes_copied) {
        message += L" slot_record_input_bytes=";
        message += HexBytes(slot_record_bytes.data(), slot_record_bytes.size());
    }
    AppendInvSlotHandleLButtonCoreLateManagerFields(&message, L"manager_before", manager_before);
    AppendInvSlotHandleLButtonCoreLateManagerFields(&message, L"manager_after", manager_after);
    monomyth::logger::Log(message);
}

void LogMoveItemSlot21LookupTrace(
    const wchar_t* caller_label,
    std::uintptr_t caller_return_address,
    void* this_context,
    void* output_like,
    std::uint32_t slot_like,
    void* original_result,
    const std::array<std::uint8_t, 24>& context_bytes_before,
    bool context_bytes_before_copied,
    const std::array<std::uint8_t, 24>& context_bytes_after,
    bool context_bytes_after_copied,
    const std::array<std::uint8_t, 24>& output_bytes_before,
    bool output_bytes_before_copied,
    const std::array<std::uint8_t, 24>& output_bytes_after,
    bool output_bytes_after_copied,
    std::uint32_t output_dword0_before,
    bool output_dword0_before_copied,
    std::uint32_t output_dword0_after,
    bool output_dword0_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemSlot21Lookup";
    message += L" probe_address=";
    message += HexPtr(module_base + kMoveItemSlot21LookupTargetRva);
    message += L" probe_rva=";
    message += Hex32(kMoveItemSlot21LookupTargetRva);
    message += L" caller_label=";
    message += caller_label == nullptr ? L"Other" : caller_label;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" output_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(output_like));
    message += L" slot_like=";
    message += Hex32(slot_like);
    message += L" original_result=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" context_bytes_before_status=";
    message += context_bytes_before_copied ? L"copied" : L"unavailable";
    if (context_bytes_before_copied) {
        message += L" context_bytes_before=";
        message += HexBytes(context_bytes_before.data(), context_bytes_before.size());
    }
    message += L" context_bytes_after_status=";
    message += context_bytes_after_copied ? L"copied" : L"unavailable";
    if (context_bytes_after_copied) {
        message += L" context_bytes_after=";
        message += HexBytes(context_bytes_after.data(), context_bytes_after.size());
    }
    message += L" output_bytes_before_status=";
    message += output_bytes_before_copied ? L"copied" : L"unavailable";
    if (output_bytes_before_copied) {
        message += L" output_bytes_before=";
        message += HexBytes(output_bytes_before.data(), output_bytes_before.size());
    }
    message += L" output_bytes_after_status=";
    message += output_bytes_after_copied ? L"copied" : L"unavailable";
    if (output_bytes_after_copied) {
        message += L" output_bytes_after=";
        message += HexBytes(output_bytes_after.data(), output_bytes_after.size());
    }
    message += L" output_dword0_before_status=";
    message += output_dword0_before_copied ? L"copied" : L"unavailable";
    if (output_dword0_before_copied) {
        message += L" output_dword0_before=";
        message += Hex32(output_dword0_before);
    }
    message += L" output_dword0_after_status=";
    message += output_dword0_after_copied ? L"copied" : L"unavailable";
    if (output_dword0_after_copied) {
        message += L" output_dword0_after=";
        message += Hex32(output_dword0_after);
    }
    monomyth::logger::Log(message);
}

void LogMoveItemValidationGateTrace(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* move_data_a,
    void* move_data_b,
    std::uint32_t arg3,
    std::uint32_t arg4,
    std::uint32_t arg5,
    std::uint32_t arg6,
    bool original_result) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemValidationGate";
    message += L" gate_address=";
    message += HexPtr(module_base + kMoveItemValidationGateTargetRva);
    message += L" gate_rva=";
    message += Hex32(kMoveItemValidationGateTargetRva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" move_data_a=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(move_data_a));
    message += L" move_data_b=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(move_data_b));
    message += L" arg3=";
    message += Hex32(arg3);
    message += L" arg4=";
    message += Hex32(arg4);
    message += L" arg5=";
    message += Hex32(arg5);
    message += L" arg6=";
    message += Hex32(arg6);
    message += L" original_result=";
    message += original_result ? L"true" : L"false";

    ClientInventorySlotWire move_slot_a = {};
    const bool move_slot_a_copied = TryCopyObject(move_data_a, &move_slot_a);
    message += L" move_data_a_status=";
    message += move_slot_a_copied ? L"copied" : L"unavailable";
    if (move_slot_a_copied) {
        AppendClientInventorySlotFields(&message, L"move_a", move_slot_a);
    }

    ClientInventorySlotWire move_slot_b = {};
    const bool move_slot_b_copied = TryCopyObject(move_data_b, &move_slot_b);
    message += L" move_data_b_status=";
    message += move_slot_b_copied ? L"copied" : L"unavailable";
    if (move_slot_b_copied) {
        AppendClientInventorySlotFields(&message, L"move_b", move_slot_b);
    }

    monomyth::logger::Log(message);
}

void LogMoveItemCtorTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    std::uintptr_t caller_return_address,
    void* this_context,
    void* slot_like,
    std::uint32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4,
    void* original_result,
    const ClientInventorySlotWire& primary_before,
    bool primary_before_copied,
    const ClientInventorySlotWire& paired_before,
    bool paired_before_copied,
    const ClientInventorySlotWire& primary_after,
    bool primary_after_copied,
    const ClientInventorySlotWire& paired_after,
    bool paired_after_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire) * 2>& pair_bytes_before,
    bool pair_bytes_before_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire) * 2>& pair_bytes_after,
    bool pair_bytes_after_copied,
    std::uint32_t future_gate_arg5_value,
    bool future_gate_arg5_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    const auto* primary_slot_bytes = reinterpret_cast<const std::uint8_t*>(slot_like);
    const auto* paired_slot_bytes =
        primary_slot_bytes == nullptr ? nullptr : primary_slot_bytes + sizeof(ClientInventorySlotWire);

    std::wstring message = L"MulticlassItemTrace target=MoveItemCtor";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" helper_address=";
    message += HexPtr(module_base + kMoveItemCtorTargetRva);
    message += L" helper_rva=";
    message += Hex32(kMoveItemCtorTargetRva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" slot_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_like));
    message += L" paired_slot_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(paired_slot_bytes));
    message += L" original_result=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" ctor_arg2=";
    message += Hex32(arg2);
    message += L" ctor_arg3=";
    message += std::to_wstring(arg3);
    message += L" ctor_arg4=";
    message += std::to_wstring(arg4);

    message += L" pair_bytes_before_status=";
    message += pair_bytes_before_copied ? L"copied" : L"unavailable";
    if (pair_bytes_before_copied) {
        message += L" pair_bytes_before=";
        message += HexBytes(pair_bytes_before.data(), pair_bytes_before.size());
    }

    message += L" pair_bytes_after_status=";
    message += pair_bytes_after_copied ? L"copied" : L"unavailable";
    if (pair_bytes_after_copied) {
        message += L" pair_bytes_after=";
        message += HexBytes(pair_bytes_after.data(), pair_bytes_after.size());
    }

    message += L" primary_before_status=";
    message += primary_before_copied ? L"copied" : L"unavailable";
    if (primary_before_copied) {
        AppendClientInventorySlotFields(&message, L"primary_before", primary_before);
    }

    message += L" paired_before_status=";
    message += paired_before_copied ? L"copied" : L"unavailable";
    if (paired_before_copied) {
        AppendClientInventorySlotFields(&message, L"paired_before", paired_before);
    }

    message += L" primary_after_status=";
    message += primary_after_copied ? L"copied" : L"unavailable";
    if (primary_after_copied) {
        AppendClientInventorySlotFields(&message, L"primary_after", primary_after);
    }

    message += L" paired_after_status=";
    message += paired_after_copied ? L"copied" : L"unavailable";
    if (paired_after_copied) {
        AppendClientInventorySlotFields(&message, L"paired_after", paired_after);
    }

    if (callsite_rva == kMoveItemCtorSiteACallsiteRva) {
        message += L" future_gate_move_data_a_pointer=";
        message += HexPtr(reinterpret_cast<std::uintptr_t>(paired_slot_bytes));
        message += L" future_gate_move_data_b_pointer=";
        message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_like));
        message += L" future_gate_arg3=0x1 future_gate_arg4=0x1 future_gate_arg5=0x0 future_gate_arg6=0x0";
    } else if (callsite_rva == kMoveItemCtorSiteBCallsiteRva) {
        message += L" future_gate_move_data_a_pointer=";
        message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_like));
        message += L" future_gate_move_data_b_pointer=";
        message += HexPtr(reinterpret_cast<std::uintptr_t>(paired_slot_bytes));
        message += L" future_gate_arg3=0x1 future_gate_arg4=0x1 future_gate_arg5_status=";
        message += future_gate_arg5_copied ? L"copied" : L"unavailable";
        if (future_gate_arg5_copied) {
            message += L" future_gate_arg5=";
            message += Hex32(future_gate_arg5_value);
        }
        message += L" future_gate_arg6=0x0";
    }

    monomyth::logger::Log(message);
}

void LogMoveItemDescriptorBuildTrace(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* output_words,
    std::int32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4,
    void* original_result,
    std::uint8_t prefix_word_count,
    bool prefix_word_count_copied,
    const std::array<std::uint8_t, 6>& prefix_words_before,
    bool prefix_words_before_copied,
    const std::array<std::uint8_t, 6>& output_words_before,
    bool output_words_before_copied,
    const std::array<std::uint8_t, 6>& output_words_after,
    bool output_words_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemDescriptorBuild";
    message += L" helper_address=";
    message += HexPtr(module_base + kMoveItemDescriptorBuildTargetRva);
    message += L" helper_rva=";
    message += Hex32(kMoveItemDescriptorBuildTargetRva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" output_words_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(output_words));
    message += L" original_result=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" arg2=";
    message += std::to_wstring(arg2);
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" arg4=";
    message += std::to_wstring(arg4);
    message += L" prefix_word_count_status=";
    message += prefix_word_count_copied ? L"copied" : L"unavailable";
    if (prefix_word_count_copied) {
        message += L" prefix_word_count=";
        message += std::to_wstring(prefix_word_count);
    }
    message += L" prefix_words_before_status=";
    message += prefix_words_before_copied ? L"copied" : L"unavailable";
    if (prefix_words_before_copied) {
        message += L" prefix_words_before=";
        message += HexBytes(prefix_words_before.data(), prefix_words_before.size());
    }
    message += L" output_words_before_status=";
    message += output_words_before_copied ? L"copied" : L"unavailable";
    if (output_words_before_copied) {
        message += L" output_words_before=";
        message += HexBytes(output_words_before.data(), output_words_before.size());
    }
    message += L" output_words_after_status=";
    message += output_words_after_copied ? L"copied" : L"unavailable";
    if (output_words_after_copied) {
        message += L" output_words_after=";
        message += HexBytes(output_words_after.data(), output_words_after.size());

        std::array<std::int16_t, 3> output_words_decoded = {};
        std::memcpy(
            output_words_decoded.data(),
            output_words_after.data(),
            output_words_after.size());
        message += L" output_word0=";
        message += std::to_wstring(output_words_decoded[0]);
        message += L" output_word1=";
        message += std::to_wstring(output_words_decoded[1]);
        message += L" output_word2=";
        message += std::to_wstring(output_words_decoded[2]);
    }

    monomyth::logger::Log(message);
}

void LogMoveItemSlotPopulateTrace(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* slot_like,
    std::int32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4,
    std::int32_t arg5,
    void* original_result,
    std::uint8_t prefix_word_count,
    bool prefix_word_count_copied,
    const std::array<std::uint8_t, 6>& prefix_words_before,
    bool prefix_words_before_copied,
    const ClientInventorySlotWire& slot_before,
    bool slot_before_copied,
    const ClientInventorySlotWire& slot_after,
    bool slot_after_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire)>& slot_bytes_before,
    bool slot_bytes_before_copied,
    const std::array<std::uint8_t, sizeof(ClientInventorySlotWire)>& slot_bytes_after,
    bool slot_bytes_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemSlotPopulate";
    message += L" helper_address=";
    message += HexPtr(module_base + kMoveItemSlotPopulateTargetRva);
    message += L" helper_rva=";
    message += Hex32(kMoveItemSlotPopulateTargetRva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" slot_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_like));
    message += L" original_result=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" arg2=";
    message += std::to_wstring(arg2);
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" arg4=";
    message += std::to_wstring(arg4);
    message += L" arg5=";
    message += std::to_wstring(arg5);
    message += L" prefix_word_count_status=";
    message += prefix_word_count_copied ? L"copied" : L"unavailable";
    if (prefix_word_count_copied) {
        message += L" prefix_word_count=";
        message += std::to_wstring(prefix_word_count);
    }
    message += L" prefix_words_before_status=";
    message += prefix_words_before_copied ? L"copied" : L"unavailable";
    if (prefix_words_before_copied) {
        message += L" prefix_words_before=";
        message += HexBytes(prefix_words_before.data(), prefix_words_before.size());
    }
    message += L" slot_bytes_before_status=";
    message += slot_bytes_before_copied ? L"copied" : L"unavailable";
    if (slot_bytes_before_copied) {
        message += L" slot_bytes_before=";
        message += HexBytes(slot_bytes_before.data(), slot_bytes_before.size());
    }
    message += L" slot_bytes_after_status=";
    message += slot_bytes_after_copied ? L"copied" : L"unavailable";
    if (slot_bytes_after_copied) {
        message += L" slot_bytes_after=";
        message += HexBytes(slot_bytes_after.data(), slot_bytes_after.size());
    }
    message += L" slot_before_status=";
    message += slot_before_copied ? L"copied" : L"unavailable";
    if (slot_before_copied) {
        AppendClientInventorySlotFields(&message, L"slot_before", slot_before);
    }
    message += L" slot_after_status=";
    message += slot_after_copied ? L"copied" : L"unavailable";
    if (slot_after_copied) {
        AppendClientInventorySlotFields(&message, L"slot_after", slot_after);
    }

    monomyth::logger::Log(message);
}

void LogMoveItemBranchKindTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    std::uintptr_t target_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    int original_result,
    std::uint32_t owner_state_04,
    bool owner_state_04_copied,
    std::uintptr_t owner_pointer_08,
    bool owner_pointer_08_copied,
    std::uintptr_t owner_field_98,
    bool owner_field_98_copied,
    std::uintptr_t primary_pointer_9c,
    bool primary_pointer_9c_copied,
    std::uintptr_t owner_field_104,
    bool owner_field_104_copied,
    std::uintptr_t owner_field_134,
    bool owner_field_134_copied,
    std::uintptr_t fallback_pointer_144,
    bool fallback_pointer_144_copied,
    std::uintptr_t vtable_address,
    std::uintptr_t resolver_address,
    std::uintptr_t resolved_object_address,
    bool resolved_flag_10e_copied,
    std::uint8_t resolved_flag_10e_value,
    bool resolved_flag_110_copied,
    std::uint8_t resolved_flag_110_value,
    bool resolved_dword_114_copied,
    std::uint32_t resolved_dword_114_value,
    bool resolved_flag_copied,
    std::uint8_t resolved_flag_value,
    bool resolved_dword_11c_copied,
    std::uint32_t resolved_dword_11c_value,
    bool resolved_flag_120_copied,
    std::uint8_t resolved_flag_120_value,
    bool resolved_field_521_copied,
    std::uint8_t resolved_field_521_value,
    bool resolved_field_522_copied,
    std::uint8_t resolved_field_522_value,
    const std::array<std::uint8_t, 24>& context_bytes,
    bool context_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemBranchKind";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + target_rva);
    message += L" probe_rva=";
    message += Hex32(target_rva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" cmp_eq_one=";
    message += original_result == 1 ? L"true" : L"false";
    message += L" next_probe_expected=";
    message += original_result == 1 ? L"true" : L"false";
    message += L" direct_slot33_fallthrough=";
    message += original_result == 1 ? L"false" : L"true";
    message += L" owner_state_04_status=";
    message += owner_state_04_copied ? L"copied" : L"unavailable";
    if (owner_state_04_copied) {
        message += L" owner_state_04=";
        message += Hex32(owner_state_04);
    }
    message += L" owner_pointer_08_status=";
    message += owner_pointer_08_copied ? L"copied" : L"unavailable";
    if (owner_pointer_08_copied) {
        message += L" owner_pointer_08=";
        message += HexPtr(owner_pointer_08);
    }
    message += L" owner_field_98_status=";
    message += owner_field_98_copied ? L"copied" : L"unavailable";
    if (owner_field_98_copied) {
        message += L" owner_field_98=";
        message += HexPtr(owner_field_98);
    }
    message += L" primary_pointer_9c=";
    message += HexPtr(primary_pointer_9c);
    message += L" primary_pointer_9c_status=";
    message += primary_pointer_9c_copied ? L"copied" : L"unavailable";
    message += L" owner_field_104_status=";
    message += owner_field_104_copied ? L"copied" : L"unavailable";
    if (owner_field_104_copied) {
        message += L" owner_field_104=";
        message += HexPtr(owner_field_104);
    }
    message += L" owner_field_134_status=";
    message += owner_field_134_copied ? L"copied" : L"unavailable";
    if (owner_field_134_copied) {
        message += L" owner_field_134=";
        message += HexPtr(owner_field_134);
    }
    message += L" fallback_pointer_144=";
    message += HexPtr(fallback_pointer_144);
    message += L" fallback_pointer_144_status=";
    message += fallback_pointer_144_copied ? L"copied" : L"unavailable";
    message += L" resolved_pointer_source=";
    if (resolved_object_address == primary_pointer_9c && primary_pointer_9c != 0) {
        message += L"primary_9c";
    } else if (resolved_object_address == fallback_pointer_144 && fallback_pointer_144 != 0) {
        message += L"fallback_144";
    } else if (resolved_object_address == 0) {
        message += L"null";
    } else {
        message += L"other";
    }
    message += L" vtable=";
    message += HexPtr(vtable_address);
    message += L" resolver=";
    message += HexPtr(resolver_address);
    message += L" resolved_object=";
    message += HexPtr(resolved_object_address);
    message += L" resolved_flag_10e_status=";
    message += resolved_flag_10e_copied ? L"copied" : L"unavailable";
    if (resolved_flag_10e_copied) {
        message += L" resolved_flag_10e=";
        message += std::to_wstring(resolved_flag_10e_value);
    }
    message += L" resolved_flag_110_status=";
    message += resolved_flag_110_copied ? L"copied" : L"unavailable";
    if (resolved_flag_110_copied) {
        message += L" resolved_flag_110=";
        message += std::to_wstring(resolved_flag_110_value);
    }
    message += L" resolved_dword_114_status=";
    message += resolved_dword_114_copied ? L"copied" : L"unavailable";
    if (resolved_dword_114_copied) {
        message += L" resolved_dword_114=";
        message += Hex32(resolved_dword_114_value);
    }
    message += L" resolved_flag_11a_status=";
    message += resolved_flag_copied ? L"copied" : L"unavailable";
    if (resolved_flag_copied) {
        message += L" resolved_flag_11a=";
        message += std::to_wstring(resolved_flag_value);
    }
    message += L" resolved_dword_11c_status=";
    message += resolved_dword_11c_copied ? L"copied" : L"unavailable";
    if (resolved_dword_11c_copied) {
        message += L" resolved_dword_11c=";
        message += Hex32(resolved_dword_11c_value);
    }
    message += L" resolved_flag_120_status=";
    message += resolved_flag_120_copied ? L"copied" : L"unavailable";
    if (resolved_flag_120_copied) {
        message += L" resolved_flag_120=";
        message += std::to_wstring(resolved_flag_120_value);
    }
    message += L" resolved_field_521_status=";
    message += resolved_field_521_copied ? L"copied" : L"unavailable";
    if (resolved_field_521_copied) {
        message += L" resolved_field_521=";
        message += std::to_wstring(resolved_field_521_value);
    }
    message += L" resolved_field_522_status=";
    message += resolved_field_522_copied ? L"copied" : L"unavailable";
    if (resolved_field_522_copied) {
        message += L" resolved_field_522=";
        message += std::to_wstring(resolved_field_522_value);
    }
    message += L" context_bytes_status=";
    message += context_bytes_copied ? L"copied" : L"unavailable";
    if (context_bytes_copied) {
        message += L" context_bytes=";
        message += HexBytes(context_bytes.data(), context_bytes.size());
    }

    monomyth::logger::Log(message);
}

void LogMoveItemBranchBoolTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    std::uintptr_t target_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    std::uint32_t state_at_14,
    bool state_at_14_copied,
    const std::array<std::uint8_t, 24>& context_bytes,
    bool context_bytes_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemBranchBool";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + target_rva);
    message += L" probe_rva=";
    message += Hex32(target_rva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" pre_ctor_setup_65d2f0=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" state_at_14_status=";
    message += state_at_14_copied ? L"copied" : L"unavailable";
    if (state_at_14_copied) {
        message += L" state_at_14=";
        message += Hex32(state_at_14);
        message += L" equals_one=";
        message += state_at_14 == 1 ? L"true" : L"false";
    }
    message += L" context_bytes_status=";
    message += context_bytes_copied ? L"copied" : L"unavailable";
    if (context_bytes_copied) {
        message += L" context_bytes=";
        message += HexBytes(context_bytes.data(), context_bytes.size());
    }

    monomyth::logger::Log(message);
}

void LogMoveItemStackLocalGateTrace(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    std::uintptr_t target_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const std::array<std::uint8_t, 24>& local_bytes_before,
    bool local_bytes_before_copied,
    const std::array<std::uint8_t, 24>& local_bytes_after,
    bool local_bytes_after_copied,
    bool dword0_zero,
    bool slot_word_valid,
    bool subindex_is_ffff,
    bool slot_lte_0x16,
    const ClientInventorySlotWire& local_slot_before,
    bool local_slot_before_copied,
    const ClientInventorySlotWire& local_slot_after,
    bool local_slot_after_copied) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=MoveItemStackLocalGate";
    message += L" callsite_label=";
    message += callsite_label == nullptr ? L"unknown" : callsite_label;
    message += L" probe_address=";
    message += HexPtr(module_base + target_rva);
    message += L" probe_rva=";
    message += Hex32(target_rva);
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nonzero=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" extended_setup_taken=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" predicate_dword0_zero=";
    message += dword0_zero ? L"true" : L"false";
    message += L" predicate_slot_word_valid=";
    message += slot_word_valid ? L"true" : L"false";
    message += L" predicate_subindex_is_ffff=";
    message += subindex_is_ffff ? L"true" : L"false";
    message += L" predicate_slot_lte_0x16=";
    message += slot_lte_0x16 ? L"true" : L"false";
    message += L" local_bytes_before_status=";
    message += local_bytes_before_copied ? L"copied" : L"unavailable";
    if (local_bytes_before_copied) {
        message += L" local_bytes_before=";
        message += HexBytes(local_bytes_before.data(), local_bytes_before.size());
    }
    message += L" local_bytes_after_status=";
    message += local_bytes_after_copied ? L"copied" : L"unavailable";
    if (local_bytes_after_copied) {
        message += L" local_bytes_after=";
        message += HexBytes(local_bytes_after.data(), local_bytes_after.size());
    }
    message += L" local_slot_before_status=";
    message += local_slot_before_copied ? L"copied" : L"unavailable";
    if (local_slot_before_copied) {
        AppendClientInventorySlotFields(&message, L"local_slot_before", local_slot_before);
    }
    message += L" local_slot_after_status=";
    message += local_slot_after_copied ? L"copied" : L"unavailable";
    if (local_slot_after_copied) {
        AppendClientInventorySlotFields(&message, L"local_slot_after", local_slot_after);
    }

    monomyth::logger::Log(message);
}

void LogMemorizeSendTraceStartupMarker(
    const monomyth::runtime::Manifest& manifest,
    bool hook_installed) {
    const bool wrapper_trace_enabled =
        manifest.memorize_send_trace_allowed ||
        manifest.multiclass_item_usability_allowed;
    const bool target_validated =
        manifest.memorize_send_packet_wrapper_state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        manifest.memorize_send_packet_wrapper_address != 0;
    std::wstring message = L"MemorizeSendTraceStartup slice_id=";
    message += kMemorizeSendTraceSliceId;
    message += L" capability_enabled=";
    message += wrapper_trace_enabled ? L"true" : L"false";
    message += L" memorize_focus=";
    message += manifest.memorize_send_trace_allowed ? L"true" : L"false";
    message += L" move_item_focus=";
    message += manifest.multiclass_item_usability_allowed ? L"true" : L"false";
    message += L" target_validated=";
    message += target_validated ? L"true" : L"false";
    message += L" hook_installed=";
    message += hook_installed ? L"true" : L"false";
    message += L" target=MemorizeSendPacketWrapper";
    if (manifest.memorize_send_packet_wrapper_rva != 0) {
        message += L" wrapper_rva=";
        message += Hex32(manifest.memorize_send_packet_wrapper_rva);
    }
    if (manifest.memorize_send_packet_wrapper_address != 0) {
        message += L" wrapper_address=";
        message += HexPtr(manifest.memorize_send_packet_wrapper_address);
    }
    message += L" reason=\"";
    message += manifest.memorize_send_trace_reason.empty()
        ? L"unknown"
        : manifest.memorize_send_trace_reason;
    message += L"\"";
    monomyth::logger::Log(message);
}

void LogScrollScribeTrace(
    const wchar_t* target,
    const std::wstring& suffix) {
    if (target == nullptr || !g_scroll_scribe_active_logging) {
        return;
    }

    std::wstring message = L"ScrollScribeTrace target=";
    message += target;
    message += L" correlation=";
    message += std::to_wstring(g_scroll_scribe_active_correlation_id);
    message += L" ";
    message += suffix;
    monomyth::logger::Log(message);
}

void LogHandleRButtonUpTrace(
    const wchar_t* phase,
    void* this_slot,
    void* point) {
    if (!g_scroll_scribe_active_logging) {
        return;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::wstring suffix = L"phase=";
    suffix += phase == nullptr ? L"unknown" : phase;
    suffix += L" this=";
    suffix += HexPtr(reinterpret_cast<std::uintptr_t>(this_slot));
    suffix += L" point=";
    suffix += HexPtr(reinterpret_cast<std::uintptr_t>(point));
    suffix += L" assigned_mask=";
    suffix += FormatAssignedMask(snapshot);
    suffix += L" has_assigned_mask=";
    suffix += snapshot.has_classes_bitmask ? L"true" : L"false";
    suffix += L" evidence_source=";
    suffix += g_handle_rbutton_up_evidence_source;
    suffix += L" scroll_hint=unknown";
    suffix += L" observed_count=";
    suffix += std::to_wstring(g_scroll_scribe_event_count);
    LogScrollScribeTrace(L"CInvSlot::HandleRButtonUp", suffix);
}

void LogIsClassUsablePredicateTrace(
    void* this_character,
    unsigned int class_id,
    int original_result,
    int returned_result,
    bool behavior_override_applied) {
    if (!g_scroll_scribe_active_logging) {
        return;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::wstring suffix = L"this=";
    suffix += HexPtr(reinterpret_cast<std::uintptr_t>(this_character));
    suffix += L" class_id=";
    suffix += std::to_wstring(class_id);
    suffix += L" original_result=";
    suffix += std::to_wstring(original_result);
    suffix += L" returned_result=";
    suffix += std::to_wstring(returned_result);
    suffix += L" behavior_override_applied=";
    suffix += behavior_override_applied ? L"true" : L"false";
    suffix += L" assigned_mask=";
    suffix += FormatAssignedMask(snapshot);
    suffix += L" has_assigned_mask=";
    suffix += snapshot.has_classes_bitmask ? L"true" : L"false";
    suffix += L" evidence_source=";
    suffix += g_is_class_usable_predicate_evidence_source;
    suffix += L" scroll_hint=unknown";
    LogScrollScribeTrace(L"IsClassUsablePredicate", suffix);
}

const wchar_t* DescribeCanEquipCallerSite(std::uint32_t caller_return_rva) noexcept {
    if (caller_return_rva == 0x0f7846) {
        return L" possible_error_emit_path";
    }
    if (caller_return_rva == 0x2085dd) {
        return L" inventory_scan_candidate";
    }
    if (caller_return_rva == 0x2086f8) {
        return L" inventory_scan_candidate";
    }
    if (caller_return_rva == 0x208e12) {
        return L" inventory_scan_candidate";
    }
    return L"";
}

bool IsInventoryScanCanEquipCaller(std::uint32_t caller_return_rva) noexcept {
    return caller_return_rva == 0x2085dd || caller_return_rva == 0x2086f8 ||
           caller_return_rva == 0x208e12;
}

void LogItemUsabilityOverride(
    void* this_character,
    std::uintptr_t caller_return_address,
    unsigned int class_id,
    int original_result,
    int returned_result,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemUsability target=IsClassUsablePredicate this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_character));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" class_id=";
    message += std::to_wstring(class_id);
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=";
    message += std::to_wstring(returned_result);
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    message += L" evidence_source=";
    message += g_is_class_usable_predicate_evidence_source;
    message += L" override_count=";
    message += std::to_wstring(g_is_class_usable_predicate_override_count);
    monomyth::logger::Log(message);
}

void LogCanEquipOverride(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* inventory_or_container_like,
    void* item_like,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5,
    int original_result,
    std::uint32_t item_class_mask,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemUsability target=CanEquip this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        const std::uint32_t caller_return_rva = static_cast<std::uint32_t>(
            caller_return_address - module_base);
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
        message += DescribeCanEquipCallerSite(caller_return_rva);
    }
    message += L" inventory_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(inventory_or_container_like));
    message += L" item_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(item_like));
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" arg4=";
    message += std::to_wstring(arg4);
    message += L" arg5=";
    message += std::to_wstring(arg5);
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=1";
    message += L" item_class_mask_client=";
    message += Hex32(item_class_mask);
    message += L" item_matches_assigned_class=true";
    message += L" override_mode=authoritative_item_mask_intersection";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    message += L" evidence_source=cleanroom_rva";
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateBranchGateBOverride(
    void* slot_record_like,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const ClientInventorySlotWire& slot_record,
    bool slot_record_copied,
    std::uintptr_t item_like,
    const OffhandWeaponPolicySnapshot& offhand_policy,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"MulticlassItemUsability target=InvSlotHandleLButtonCoreLateBranchGateB";
    message += L" slot_record_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_record_like));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=1";
    message += L" slot_record_status=";
    message += slot_record_copied ? L"copied" : L"unavailable";
    if (slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"slot_record", slot_record);
    }
    message += L" item_like=";
    message += HexPtr(item_like);
    message += L" item_data_like=";
    message += HexPtr(offhand_policy.item_data_like);
    AppendOffhandWeaponPolicyFields(&message, offhand_policy);
    message += L" gate_label=equipment_slot_late_gate";
    message += L" override_mode=secondary_weapon_dual_wield_matrix";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    message += L" override_count=";
    message += std::to_wstring(g_invslot_handle_lbutton_core_late_branch_gate_b_override_count);
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateBranchGateBObservation(
    void* slot_record_like,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    const ClientInventorySlotWire& slot_record,
    bool slot_record_copied,
    std::uintptr_t item_like,
    const OffhandWeaponPolicySnapshot& offhand_policy,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"MulticlassItemTrace target=InvSlotHandleLButtonCoreLateBranchGateBObserved";
    message += L" slot_record_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(slot_record_like));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=";
    message += std::to_wstring(original_result);
    message += L" slot_record_status=";
    message += slot_record_copied ? L"copied" : L"unavailable";
    if (slot_record_copied) {
        AppendClientInventorySlotFields(&message, L"slot_record", slot_record);
    }
    message += L" item_like=";
    message += HexPtr(item_like);
    message += L" item_data_like=";
    message += HexPtr(offhand_policy.item_data_like);
    AppendOffhandWeaponPolicyFields(&message, offhand_policy);
    message += L" override_candidate=";
    message += offhand_policy.eligible ? L"true" : L"false";
    message += L" gate_label=equipment_slot_late_gate";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateBranchPrepOverride(
    void* this_context,
    std::int32_t slot_like,
    void* lookup_result_like,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    std::uint32_t lookup_dword0_before,
    bool lookup_dword0_before_copied,
    std::uint32_t lookup_dword0_after,
    bool lookup_dword0_after_copied,
    std::uintptr_t item_like,
    const OffhandWeaponPolicySnapshot& offhand_policy,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"MulticlassItemUsability target=InvSlotHandleLButtonCoreLateBranchPrep";
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" slot_like=";
    message += std::to_wstring(slot_like);
    message += L" lookup_result_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(lookup_result_like));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=1";
    message += L" lookup_dword0_before_status=";
    message += lookup_dword0_before_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_before_copied) {
        message += L" lookup_dword0_before=";
        message += Hex32(lookup_dword0_before);
    }
    message += L" lookup_dword0_after_status=";
    message += lookup_dword0_after_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_after_copied) {
        message += L" lookup_dword0_after=";
        message += Hex32(lookup_dword0_after);
    }
    message += L" item_like=";
    message += HexPtr(item_like);
    message += L" item_data_like=";
    message += HexPtr(offhand_policy.item_data_like);
    AppendOffhandWeaponPolicyFields(&message, offhand_policy);
    message += L" gate_label=equipment_slot_late_branch_prep";
    message += L" override_mode=secondary_weapon_dual_wield_matrix";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    message += L" override_count=";
    message += std::to_wstring(g_invslot_handle_lbutton_core_late_branch_prep_override_count);
    monomyth::logger::Log(message);
}

void LogInvSlotHandleLButtonCoreLateBranchPrepObservation(
    void* this_context,
    std::int32_t slot_like,
    void* lookup_result_like,
    std::uintptr_t caller_return_address,
    std::uint8_t original_result,
    std::uint32_t lookup_dword0_before,
    bool lookup_dword0_before_copied,
    std::uint32_t lookup_dword0_after,
    bool lookup_dword0_after_copied,
    std::uintptr_t item_like,
    const OffhandWeaponPolicySnapshot& offhand_policy,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message =
        L"MulticlassItemTrace target=InvSlotHandleLButtonCoreLateBranchPrepObserved";
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" slot_like=";
    message += std::to_wstring(slot_like);
    message += L" lookup_result_pointer=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(lookup_result_like));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=";
    message += std::to_wstring(original_result);
    message += L" lookup_dword0_before_status=";
    message += lookup_dword0_before_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_before_copied) {
        message += L" lookup_dword0_before=";
        message += Hex32(lookup_dword0_before);
    }
    message += L" lookup_dword0_after_status=";
    message += lookup_dword0_after_copied ? L"copied" : L"unavailable";
    if (lookup_dword0_after_copied) {
        message += L" lookup_dword0_after=";
        message += Hex32(lookup_dword0_after);
    }
    message += L" item_like=";
    message += HexPtr(item_like);
    message += L" item_data_like=";
    message += HexPtr(offhand_policy.item_data_like);
    AppendOffhandWeaponPolicyFields(&message, offhand_policy);
    message += L" override_candidate=";
    message += offhand_policy.eligible ? L"true" : L"false";
    message += L" gate_label=equipment_slot_late_branch_prep";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogCanEquipObservation(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* inventory_or_container_like,
    void* item_like,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5,
    int original_result,
    std::uint32_t item_class_mask,
    bool item_matches_assigned_class,
    const monomyth::server_auth_stats::Snapshot& snapshot) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=CanEquipObserved this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        const std::uint32_t caller_return_rva = static_cast<std::uint32_t>(
            caller_return_address - module_base);
        message += L" caller_return_rva=";
        message += Hex32(caller_return_rva);
        message += DescribeCanEquipCallerSite(caller_return_rva);
    }
    message += L" inventory_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(inventory_or_container_like));
    message += L" item_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(item_like));
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" arg4=";
    message += std::to_wstring(arg4);
    message += L" arg5=";
    message += std::to_wstring(arg5);
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" item_class_mask_client=";
    message += Hex32(item_class_mask);
    message += L" item_matches_assigned_class=";
    message += item_matches_assigned_class ? L"true" : L"false";
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogEquipRecordLookupTrace(
    const wchar_t* target,
    std::uint32_t callsite_rva,
    void* this_context,
    unsigned long lookup_id,
    void* original_result) {
    std::wstring message = L"MulticlassItemTrace target=";
    message += target == nullptr ? L"EquipRecordLookup" : target;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" lookup_id=";
    message += std::to_wstring(lookup_id);
    message += L" returned=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" returned_null=";
    message += original_result == nullptr ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogEquipClickCanEquipCallsiteTrace(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* inventory_or_container_like,
    void* item_like,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5,
    int original_result,
    int returned_result) {
    std::wstring message = L"MulticlassItemTrace target=EquipClickCanEquip this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    message += L" caller_return_rva=0x0f7846";
    message += L" inventory_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(inventory_or_container_like));
    message += L" item_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(item_like));
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" arg4=";
    message += std::to_wstring(arg4);
    message += L" arg5=";
    message += std::to_wstring(arg5);
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" returned_result=";
    message += std::to_wstring(returned_result);
    monomyth::logger::Log(message);
}

void LogEquipRequirementLookupTrace(
    const wchar_t* target,
    std::uint32_t callsite_rva,
    void* this_context,
    void* record_or_table_like,
    void* descriptor_like,
    unsigned long mode_like,
    void* original_result) {
    std::wstring message = L"MulticlassItemTrace target=";
    message += target == nullptr ? L"EquipRequirementLookup" : target;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" record_or_table_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(record_or_table_like));
    message += L" descriptor_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(descriptor_like));
    message += L" mode_like=";
    message += std::to_wstring(mode_like);
    message += L" returned=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" returned_null=";
    message += original_result == nullptr ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogEquipNestedInventoryGateTrace(
    void* this_context,
    unsigned long arg1,
    unsigned long arg2,
    unsigned long arg3,
    int original_result) {
    std::wstring message = L"MulticlassItemTrace target=EquipNestedInventoryGate this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" callsite_rva=";
    message += Hex32(kEquipNestedInventoryGateCallsiteRva);
    message += L" arg1=";
    message += std::to_wstring(arg1);
    message += L" arg2=";
    message += std::to_wstring(arg2);
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" original_result=";
    message += std::to_wstring(original_result);
    if (g_active_equip_nested_validation_id != 0) {
        message += L" nested_validation_id=";
        message += std::to_wstring(g_active_equip_nested_validation_id);
    }
    monomyth::logger::Log(message);
}

void LogEquipNestedValidatorTrace(
    void* this_context,
    void* arg1,
    void* arg2,
    unsigned long arg3,
    int original_result) {
    std::wstring message = L"MulticlassItemTrace target=EquipNestedValidator this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" callsite_rva=";
    message += Hex32(kEquipNestedValidatorCallsiteRva);
    message += L" arg1=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(arg1));
    message += L" arg2=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(arg2));
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" original_result=";
    message += std::to_wstring(original_result);
    message += L" nested_validation_id=";
    message += std::to_wstring(g_equip_nested_validation_count);
    monomyth::logger::Log(message);
}

void LogDragDropSilentPrecheckTrace(
    void* this_context,
    void* probe_like,
    std::uint8_t original_result) {
    std::wstring message = L"MulticlassItemTrace target=DragDropSilentPrecheck this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" callsite_rva=";
    message += Hex32(kDragDropSilentPrecheckCallsiteRva);
    message += L" probe_like=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(probe_like));
    message += L" returned_result=";
    message += original_result != 0 ? L"true" : L"false";
    message += L" returned_result_raw=";
    message += Hex32(original_result);
    monomyth::logger::Log(message);
}

const wchar_t* DescribeEquipLocalRejectMessageId(unsigned long message_id) noexcept {
    switch (message_id) {
    case 0x33dd:
        return L"class_deity_race_reject";
    case 0x1836:
        return L"requirement_lookup_reject";
    case 0x01a2:
        return L"precheck_reject";
    case 0x145e:
        return L"drag_drop_reject_a";
    case 0x145f:
        return L"drag_drop_reject_b";
    default:
        return L"unknown";
    }
}

void LogEquipLocalRejectMessageTrace(
    const wchar_t* target,
    std::uint32_t callsite_rva,
    void* this_context,
    std::uintptr_t caller_return_address,
    unsigned long message_id,
    unsigned long arg2,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5,
    void* original_result) {
    const std::uintptr_t module_base = GetHostModuleBase();
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::wstring message = L"MulticlassItemTrace target=";
    message += target == nullptr ? L"EquipLocalRejectMessage" : target;
    message += L" this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" callsite_rva=";
    message += Hex32(callsite_rva);
    message += L" message_id=";
    message += Hex32(static_cast<std::uint32_t>(message_id));
    message += L" message_label=";
    message += DescribeEquipLocalRejectMessageId(message_id);
    message += L" arg2=";
    message += std::to_wstring(arg2);
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" arg4=";
    message += std::to_wstring(arg4);
    message += L" arg5=";
    message += std::to_wstring(arg5);
    message += L" resolved_message=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(original_result));
    message += L" assigned_mask=";
    message += FormatAssignedMask(snapshot);
    message += L" has_assigned_mask=";
    message += snapshot.has_classes_bitmask ? L"true" : L"false";
    monomyth::logger::Log(message);
}

void LogInvSlotMgrMoveItemTrace(
    void* this_context,
    std::uintptr_t caller_return_address,
    void* move_data_a,
    void* move_data_b,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5,
    unsigned long arg6,
    bool original_result) {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::wstring message = L"MulticlassItemTrace target=CInvSlotMgr::MoveItem this=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(this_context));
    if (g_inv_slot_mgr_move_item_address != 0) {
        message += L" target_address=";
        message += HexPtr(g_inv_slot_mgr_move_item_address);
        if (module_base != 0 && g_inv_slot_mgr_move_item_address >= module_base) {
            message += L" target_rva=";
            message += Hex32(static_cast<std::uint32_t>(
                g_inv_slot_mgr_move_item_address - module_base));
        }
    }
    message += L" caller_return=";
    message += HexPtr(caller_return_address);
    if (module_base != 0 && caller_return_address >= module_base) {
        message += L" caller_return_rva=";
        message += Hex32(static_cast<std::uint32_t>(caller_return_address - module_base));
    }
    message += L" move_data_a=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(move_data_a));
    message += L" move_data_b=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(move_data_b));
    message += L" arg3=";
    message += std::to_wstring(arg3);
    message += L" arg4=";
    message += std::to_wstring(arg4);
    message += L" arg5=";
    message += std::to_wstring(arg5);
    message += L" arg6=";
    message += std::to_wstring(arg6);
    message += L" original_result=";
    message += original_result ? L"true" : L"false";
    monomyth::logger::Log(message);
}

std::uint8_t QueryOriginalSpellLevel(
    void* context,
    const void* this_spell,
    unsigned int class_id) noexcept {
    const auto original = reinterpret_cast<GetSpellLevelNeededFn>(context);
    if (original == nullptr) {
        return 255;
    }

    return original(this_spell, class_id);
}

std::uint8_t MONOMYTH_FASTCALL GetSpellLevelNeededHook(
    const void* this_spell,
    void*,
    unsigned int class_id) noexcept {
    const std::uint8_t original_level = g_original_get_spell_level_needed(this_spell, class_id);
    ++g_get_spell_level_needed_trace_count;
    if (!g_multiclass_spell_usability_enabled) {
        if (ShouldLogSpellTrace(g_get_spell_level_needed_trace_count)) {
            std::wstring message = L"SpellUsabilityTrace target=GetSpellLevelNeeded class=";
            message += std::to_wstring(class_id);
            message += L" original_level=";
            message += std::to_wstring(static_cast<unsigned int>(original_level));
            AppendActiveScrollCorrelation(&message);
            message += L" observed_count=";
            message += std::to_wstring(g_get_spell_level_needed_trace_count);
            monomyth::logger::Log(message);
        }
        return original_level;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    const monomyth::spell_level_selection::SelectionResult selection =
        monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
            &QueryOriginalSpellLevel,
            reinterpret_cast<void*>(g_original_get_spell_level_needed),
            this_spell,
            class_id,
            snapshot.has_classes_bitmask,
            snapshot.classes_bitmask,
            original_level);

    if (ShouldLogSpellTrace(g_get_spell_level_needed_trace_count)) {
        std::wstring message = L"SpellUsabilityTrace target=GetSpellLevelNeeded class=";
        message += std::to_wstring(class_id);
        message += L" requested_class=";
        message += std::to_wstring(class_id);
        message += L" assigned_mask=";
        message += Hex32(snapshot.has_classes_bitmask ? snapshot.classes_bitmask : 0);
        message += L" original_level=";
        message += std::to_wstring(static_cast<unsigned int>(original_level));
        message += L" selected_class=";
        message += selection.used_assigned_class
            ? std::to_wstring(selection.selected_class)
            : L"none";
        message += L" selected_level=";
        message += std::to_wstring(static_cast<unsigned int>(selection.level));
        message += L" fallback_reason=\"";
        message += selection.used_assigned_class ? L"" : selection.fallback_reason;
        message += L"\"";
        message += L" behavior_enabled=true";
        AppendActiveScrollCorrelation(&message);
        message += L" observed_count=";
        message += std::to_wstring(g_get_spell_level_needed_trace_count);
        monomyth::logger::Log(message);
    }
    return selection.level;
}

void MONOMYTH_FASTCALL SpellbookDispatcherHook(
    void* this_window,
    void*,
    int slot_like) noexcept {
    const std::uint32_t previous_correlation = g_spellbook_ui_active_correlation_id;
    const bool assign_new_correlation = previous_correlation == 0;
    if (assign_new_correlation) {
        g_spellbook_ui_active_correlation_id = ++g_spellbook_ui_correlation_count;
    }
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    LogSpellbookDispatcherCall(
        correlation_id,
        this_window,
        slot_like,
        caller_return_address);
    g_original_spellbook_dispatcher(this_window, slot_like);
    if (assign_new_correlation) {
        g_spellbook_ui_active_correlation_id = 0;
    } else {
        g_spellbook_ui_active_correlation_id = previous_correlation;
    }
}

int MONOMYTH_FASTCALL StartSpellScribePathHook(
    void* this_window,
    void*,
    int spellbook_entry_like) noexcept {
    const std::uint32_t previous_correlation = g_spellbook_ui_active_correlation_id;
    const bool assign_new_correlation = previous_correlation == 0;
    if (assign_new_correlation) {
        g_spellbook_ui_active_correlation_id = ++g_spellbook_ui_correlation_count;
    }
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const SpellbookMemStateSnapshot before_state = CaptureSpellbookMemState(this_window);
    const int original_result =
        g_original_start_spell_scribe_path(this_window, spellbook_entry_like);
    const SpellbookMemStateSnapshot after_state = CaptureSpellbookMemState(this_window);
    LogStartSpellScribePathCall(
        correlation_id,
        this_window,
        spellbook_entry_like,
        caller_return_address,
        before_state,
        after_state,
        original_result);
    if (assign_new_correlation) {
        g_spellbook_ui_active_correlation_id = 0;
    } else {
        g_spellbook_ui_active_correlation_id = previous_correlation;
    }
    return original_result;
}

int MONOMYTH_FASTCALL StartSpellScribePrecheckModeGetterHook(
    void* this_context,
    void*) noexcept {
    const int original_result = g_original_start_spell_scribe_precheck_mode_getter(this_context);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (correlation_id != 0) {
        LogStartSpellScribePrecheckModeGetterCall(
            correlation_id,
            this_context,
            GetCallerReturnAddress(),
            original_result);
    }
    return original_result;
}

bool MONOMYTH_FASTCALL StartSpellScribePrecheckGateHook(
    void* this_context,
    void*,
    void* descriptor_like,
    int require_known_like,
    int allow_recheck_like) noexcept {
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_return_rva =
        module_base != 0 && caller_return_address >= module_base
            ? static_cast<std::uint32_t>(caller_return_address - module_base)
            : 0;
    ResetStartSpellScribePrecheckClassMaskSnapshot(correlation_id);
    const bool original_result = g_original_start_spell_scribe_precheck_gate(
        this_context,
        descriptor_like,
        require_known_like,
        allow_recheck_like);
    CaptureStartSpellScribePrecheckClassResolverFromGateContext(this_context);
    bool final_result = original_result;
    auto& snapshot = g_start_spell_scribe_precheck_class_mask_snapshot;
    const monomyth::server_auth_stats::Snapshot authoritative_snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    snapshot.authoritative_mask_present = authoritative_snapshot.has_classes_bitmask;
    snapshot.authoritative_mask = authoritative_snapshot.classes_bitmask;
    if (snapshot.authoritative_mask_present && snapshot.assigned_mask_getter_called) {
        snapshot.authoritative_mask_intersection =
            snapshot.authoritative_mask & snapshot.assigned_mask;
        snapshot.authoritative_mask_intersects_spell_mask =
            snapshot.authoritative_mask_intersection != 0;
    }

    const bool no_late_rule_called =
        !snapshot.rule_4462c0_called && !snapshot.rule_446190_called &&
        !snapshot.rule_446200_called && !snapshot.rule_446380_called;
    const bool class_id_is_playable =
        snapshot.class_id_copied &&
        monomyth::multiclass_identity::IsPlayableClassId(snapshot.class_id);
    const std::uint32_t current_class_bit = class_id_is_playable
        ? monomyth::multiclass_identity::ClassBit(snapshot.class_id)
        : 0;
    const bool current_class_rejected_by_spell_mask =
        class_id_is_playable &&
        snapshot.assigned_mask_getter_called &&
        (snapshot.assigned_mask & current_class_bit) == 0;

    // This override only clears the proven early native-class reject. Later rule helpers,
    // native-mask hits, and missing authoritative intersections still fall through unchanged.
    if (g_multiclass_spell_usability_enabled &&
        !original_result &&
        require_known_like != 0 &&
        no_late_rule_called &&
        current_class_rejected_by_spell_mask &&
        snapshot.authoritative_mask_intersects_spell_mask) {
        final_result = true;
        snapshot.behavior_override_applied = true;
    }

    const bool is_hot_autoequip_caller =
        caller_return_rva == kAutoEquipClassGateCallerReturnARva ||
        caller_return_rva == kAutoEquipClassGateCallerReturnBRva;
    if (is_hot_autoequip_caller) {
        std::uintptr_t item_like = 0;
        const bool item_like_copied = TryCopyObject(descriptor_like, &item_like);
        const OffhandWeaponPolicySnapshot offhand_policy =
            item_like_copied
            ? CaptureOffhandWeaponPolicySnapshot(item_like, authoritative_snapshot)
            : OffhandWeaponPolicySnapshot {};
        const bool candidate_slot_proven = false;
        const bool candidate_slot_is_secondary = false;
        if ((offhand_policy.is_weapon && offhand_policy.can_wear_secondary) ||
            !original_result ||
            final_result != original_result) {
            LogAutoEquipClassGateDecision(
                this_context,
                caller_return_address,
                descriptor_like,
                static_cast<unsigned long>(require_known_like),
                static_cast<unsigned long>(allow_recheck_like),
                original_result ? 1 : 0,
                final_result ? 1 : 0,
                item_like,
                offhand_policy,
                candidate_slot_proven,
                candidate_slot_is_secondary,
                authoritative_snapshot);
        }
    }

    if (correlation_id != 0) {
        LogStartSpellScribePrecheckGateCall(
            correlation_id,
            this_context,
            descriptor_like,
            require_known_like,
            allow_recheck_like,
            caller_return_address,
            original_result,
            final_result);
    }
    ClearStartSpellScribePrecheckClassMaskSnapshot();
    return final_result;
}

int MONOMYTH_FASTCALL StartSpellScribePrecheckLookupHook(
    void* this_context,
    void*,
    void* spell_or_scroll_like) noexcept {
    const int original_result = g_original_start_spell_scribe_precheck_lookup(
        this_context,
        spell_or_scroll_like);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (correlation_id != 0) {
        LogStartSpellScribePrecheckLookupCall(
            correlation_id,
            this_context,
            spell_or_scroll_like,
            GetCallerReturnAddress(),
            original_result);
    }
    return original_result;
}

int MONOMYTH_FASTCALL StartSpellScribePrecheckFastAcceptHook(
    void* this_context,
    void*) noexcept {
    const int original_result = g_original_start_spell_scribe_precheck_fast_accept(this_context);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (correlation_id != 0) {
        LogStartSpellScribeNestedPrecheckIntCall(
            L"StartSpellScribePrecheckFastAccept",
            g_start_spell_scribe_precheck_fast_accept_address,
            correlation_id,
            this_context,
            GetCallerReturnAddress(),
            L"StartSpellScribePrecheckGateFastAccept",
            original_result);
    }
    return original_result;
}

void* MONOMYTH_FASTCALL StartSpellScribePrecheckClassResolverHook(
    void* this_context,
    void*) noexcept {
    void* const original_result =
        g_original_start_spell_scribe_precheck_class_resolver(this_context);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    std::uint8_t class_id = 0;
    const bool class_id_copied =
        original_result != nullptr &&
        TryCopyBytes(
            reinterpret_cast<const std::uint8_t*>(original_result) +
                kScribeGateResolvedClassIdOffset,
            sizeof(class_id),
            &class_id);
    if (g_start_spell_scribe_precheck_class_mask_snapshot.active &&
        g_start_spell_scribe_precheck_class_mask_snapshot.correlation_id == correlation_id) {
        auto& snapshot = g_start_spell_scribe_precheck_class_mask_snapshot;
        snapshot.class_resolver_called = true;
        snapshot.class_resolver_this = this_context;
        snapshot.class_record = original_result;
        snapshot.class_id_copied = class_id_copied;
        snapshot.class_id = class_id;
    }
    if (correlation_id != 0) {
        LogStartSpellScribePrecheckClassResolverCall(
            correlation_id,
            this_context,
            GetCallerReturnAddress(),
            original_result,
            class_id_copied,
            class_id);
    }
    return original_result;
}

std::uint32_t MONOMYTH_FASTCALL StartSpellScribePrecheckAssignedMaskGetterHook(
    void* this_context,
    void*,
    int flag_like,
    int extra_like) noexcept {
    const std::uint32_t original_result =
        g_original_start_spell_scribe_precheck_assigned_mask_getter(
            this_context,
            flag_like,
            extra_like);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (g_start_spell_scribe_precheck_class_mask_snapshot.active &&
        g_start_spell_scribe_precheck_class_mask_snapshot.correlation_id == correlation_id) {
        auto& snapshot = g_start_spell_scribe_precheck_class_mask_snapshot;
        snapshot.assigned_mask_getter_called = true;
        snapshot.assigned_mask_getter_this = this_context;
        snapshot.assigned_mask_flag_like = flag_like;
        snapshot.assigned_mask_extra_like = extra_like;
        snapshot.assigned_mask = original_result;
    }
    if (correlation_id != 0) {
        LogStartSpellScribePrecheckAssignedMaskGetterCall(
            correlation_id,
            this_context,
            flag_like,
            extra_like,
            GetCallerReturnAddress(),
            original_result);
    }
    return original_result;
}

bool MONOMYTH_FASTCALL StartSpellScribePrecheckRule4462c0Hook(
    void* this_context,
    void*,
    void* descriptor_like,
    int flag_like) noexcept {
    const bool original_result = g_original_start_spell_scribe_precheck_rule_4462c0(
        this_context,
        descriptor_like,
        flag_like);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (g_start_spell_scribe_precheck_class_mask_snapshot.active &&
        g_start_spell_scribe_precheck_class_mask_snapshot.correlation_id == correlation_id) {
        g_start_spell_scribe_precheck_class_mask_snapshot.rule_4462c0_called = true;
    }
    if (correlation_id != 0) {
        LogStartSpellScribeNestedPrecheckBoolCall(
            L"StartSpellScribePrecheckRule4462c0",
            g_start_spell_scribe_precheck_rule_4462c0_address,
            correlation_id,
            this_context,
            descriptor_like,
            flag_like,
            0,
            false,
            GetCallerReturnAddress(),
            L"StartSpellScribePrecheckGateRule4462c0",
            original_result);
    }
    return original_result;
}

bool MONOMYTH_FASTCALL StartSpellScribePrecheckRule446190Hook(
    void* this_context,
    void*,
    void* descriptor_like,
    int flag_like) noexcept {
    const bool original_result = g_original_start_spell_scribe_precheck_rule_446190(
        this_context,
        descriptor_like,
        flag_like);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (g_start_spell_scribe_precheck_class_mask_snapshot.active &&
        g_start_spell_scribe_precheck_class_mask_snapshot.correlation_id == correlation_id) {
        g_start_spell_scribe_precheck_class_mask_snapshot.rule_446190_called = true;
    }
    if (correlation_id != 0) {
        LogStartSpellScribeNestedPrecheckBoolCall(
            L"StartSpellScribePrecheckRule446190",
            g_start_spell_scribe_precheck_rule_446190_address,
            correlation_id,
            this_context,
            descriptor_like,
            flag_like,
            0,
            false,
            GetCallerReturnAddress(),
            L"StartSpellScribePrecheckGateRule446190",
            original_result);
    }
    return original_result;
}

bool MONOMYTH_FASTCALL StartSpellScribePrecheckRule446200Hook(
    void* this_context,
    void*,
    void* descriptor_like,
    int flag_like) noexcept {
    const bool original_result = g_original_start_spell_scribe_precheck_rule_446200(
        this_context,
        descriptor_like,
        flag_like);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (g_start_spell_scribe_precheck_class_mask_snapshot.active &&
        g_start_spell_scribe_precheck_class_mask_snapshot.correlation_id == correlation_id) {
        g_start_spell_scribe_precheck_class_mask_snapshot.rule_446200_called = true;
    }
    if (correlation_id != 0) {
        LogStartSpellScribeNestedPrecheckBoolCall(
            L"StartSpellScribePrecheckRule446200",
            g_start_spell_scribe_precheck_rule_446200_address,
            correlation_id,
            this_context,
            descriptor_like,
            flag_like,
            0,
            false,
            GetCallerReturnAddress(),
            L"StartSpellScribePrecheckGateRule446200",
            original_result);
    }
    return original_result;
}

bool MONOMYTH_FASTCALL StartSpellScribePrecheckRule446380Hook(
    void* this_context,
    void*,
    void* descriptor_like,
    int flag_like,
    int zero_like) noexcept {
    const bool original_result = g_original_start_spell_scribe_precheck_rule_446380(
        this_context,
        descriptor_like,
        flag_like,
        zero_like);
    const std::uint32_t correlation_id = g_spellbook_ui_active_correlation_id;
    if (g_start_spell_scribe_precheck_class_mask_snapshot.active &&
        g_start_spell_scribe_precheck_class_mask_snapshot.correlation_id == correlation_id) {
        g_start_spell_scribe_precheck_class_mask_snapshot.rule_446380_called = true;
    }
    if (correlation_id != 0) {
        LogStartSpellScribeNestedPrecheckBoolCall(
            L"StartSpellScribePrecheckRule446380",
            g_start_spell_scribe_precheck_rule_446380_address,
            correlation_id,
            this_context,
            descriptor_like,
            flag_like,
            zero_like,
            true,
            GetCallerReturnAddress(),
            L"StartSpellScribePrecheckGateRule446380",
            original_result);
    }
    return original_result;
}

bool MONOMYTH_FASTCALL CanStartMemmingHook(
    void* this_window,
    void*,
    int spell_or_book_index) noexcept {
    const bool original_result =
        g_original_can_start_memming(this_window, spell_or_book_index);
    ++g_can_start_memming_trace_count;
    std::uint32_t memorize_send_correlation = 0;
    if (original_result) {
        if (g_memorize_send_pending_correlation_id != 0) {
            LogPendingMemorizeSendGap(L"next_can_start_memming");
        }
        memorize_send_correlation = ++g_memorize_send_correlation_count;
        g_memorize_send_pending_correlation_id = memorize_send_correlation;
        g_memorize_send_pending_wrapper_sends = 0;
        g_memorize_send_pending_window = this_window;
    }
    if (ShouldLogSpellTrace(g_can_start_memming_trace_count)) {
        std::wstring message = L"SpellUsabilityTrace target=CanStartMemming spell_or_book_index=";
        message += std::to_wstring(spell_or_book_index);
        message += L" original_result=";
        message += original_result ? L"true" : L"false";
        if (memorize_send_correlation != 0) {
            message += L" memorize_send_correlation=";
            message += std::to_wstring(memorize_send_correlation);
            message += L" send_observation=pending";
        }
        AppendActiveScrollCorrelation(&message);
        message += L" observed_count=";
        message += std::to_wstring(g_can_start_memming_trace_count);
        monomyth::logger::Log(message);
    }
    return original_result;
}

int MONOMYTH_FASTCALL StartSpellMemorizationPathHook(
    void* this_window,
    void*,
    std::uint32_t pending_slot_state,
    std::uint32_t zero_like_a,
    std::uint32_t mode_like,
    std::int32_t spell_or_book_index,
    std::uint32_t zero_like_b,
    std::int32_t slot_like) noexcept {
    (void)zero_like_a;
    (void)zero_like_b;
    const std::uint32_t correlation_id = g_memorize_send_pending_correlation_id;
    void* const pending_window = g_memorize_send_pending_window;
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const SpellbookMemStateSnapshot controller_before_state = CaptureSpellbookMemState(this_window);
    const SpellbookMemStateSnapshot pending_window_before_state =
        CaptureSpellbookMemState(pending_window);
    const int original_result = g_original_start_spell_memorization_path(
        this_window,
        pending_slot_state,
        zero_like_a,
        mode_like,
        spell_or_book_index,
        zero_like_b,
        slot_like);
    const SpellbookMemStateSnapshot controller_after_state = CaptureSpellbookMemState(this_window);
    const SpellbookMemStateSnapshot pending_window_after_state =
        CaptureSpellbookMemState(pending_window);
    LogStartSpellMemorizationPathCall(
        correlation_id,
        this_window,
        pending_window,
        pending_slot_state,
        mode_like,
        spell_or_book_index,
        slot_like,
        caller_return_address,
        controller_before_state,
        controller_after_state,
        pending_window_before_state,
        pending_window_after_state,
        original_result);
    return original_result;
}

int MONOMYTH_FASTCALL SpellbookMemorizeSendPathHook(
    void* this_window,
    void*) noexcept {
    const std::uint32_t correlation_id = g_memorize_send_pending_correlation_id;
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const SpellbookMemStateSnapshot before_state = CaptureSpellbookMemState(this_window);
    const int original_result = g_original_spellbook_memorize_send_path(this_window);
    const SpellbookMemStateSnapshot after_state = CaptureSpellbookMemState(this_window);
    LogSpellbookMemorizeSendPathCall(
        correlation_id,
        this_window,
        caller_return_address,
        before_state,
        after_state,
        original_result);
    return original_result;
}

bool MONOMYTH_FASTCALL MemorizeSendPacketWrapperHook(
    void* this_context,
    void*,
    std::uint32_t mode_like,
    const void* packet,
    std::uint32_t total_length) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    ++g_memorize_send_trace_count;

    bool opcode_decoded = false;
    std::uint16_t opcode = 0;
    std::uint32_t payload_length = 0;
    const wchar_t* decode_status = L"not_decoded";
    const wchar_t* not_decoded_reason = L"unknown";
    if (packet == nullptr) {
        not_decoded_reason = L"null_packet";
    } else if (total_length < sizeof(std::uint16_t)) {
        not_decoded_reason = L"short_packet";
    } else if (TryReadPacketOpcode(packet, &opcode)) {
        opcode_decoded = true;
        decode_status = L"decoded";
        not_decoded_reason = L"";
        payload_length = total_length > sizeof(opcode)
            ? total_length - static_cast<std::uint32_t>(sizeof(opcode))
            : 0;
    } else {
        not_decoded_reason = L"packet_read_fault";
    }

    const bool original_result = g_original_memorize_send_packet_wrapper(
        this_context,
        mode_like,
        packet,
        total_length);

    const std::uint32_t correlation_id = g_memorize_send_pending_correlation_id;
    std::uint32_t wrapper_send_index = 0;
    if (correlation_id != 0) {
        wrapper_send_index = ++g_memorize_send_pending_wrapper_sends;
    }
    const std::uint32_t spellbook_scribe_correlation_id =
        g_spellbook_scribe_pending_correlation_id;
    std::uint32_t spellbook_scribe_wrapper_send_index = 0;
    if (spellbook_scribe_correlation_id != 0) {
        spellbook_scribe_wrapper_send_index = ++g_spellbook_scribe_pending_wrapper_sends;
    }
    monomyth::packet_observer::ObserveSendMetadata(
        g_memorize_send_packet_wrapper_address,
        reinterpret_cast<std::uintptr_t>(this_context),
        reinterpret_cast<std::uintptr_t>(packet),
        total_length,
        opcode_decoded,
        static_cast<std::uint32_t>(opcode),
        payload_length,
        decode_status,
        not_decoded_reason,
        original_result,
        true,
        correlation_id);

    if (opcode_decoded && original_result) {
        const std::uint32_t opcode32 = static_cast<std::uint32_t>(opcode);
        if (opcode32 == kGmTrainingOpcode &&
            monomyth::multiclass_identity::IsPlayableClassId(
                g_pending_guild_trainer_session_class_id)) {
            ApplyGuildTrainerSessionClassOverride(
                g_pending_guild_trainer_session_class_id,
                L"OP_GMTraining_send");
        } else if (opcode32 == kGmEndTrainingOpcode) {
            RestoreGuildTrainerSessionClassOverride(L"OP_GMEndTraining_send");
            g_pending_guild_trainer_session_class_id = 0;
        }
    }

    if (correlation_id != 0) {
        if (!opcode_decoded) {
            LogMemorizeSendDecodeFailure(correlation_id, not_decoded_reason);
            ClearPendingMemorizeSendObservation();
        } else if (opcode == kMemorizeSpellOpcode) {
            // Successful ordinary memorize sends are proven here from the real wrapper caller.
            // This seam outranks earlier helper-path assumptions when commit-state traces disagree.
            LogMemorizeSendObserved(
                correlation_id,
                wrapper_send_index,
                mode_like,
                total_length,
                packet,
                this_context,
                caller_return_address,
                original_result);
            ClearPendingMemorizeSendObservation();
        } else {
            LogMemorizeSendIntermediateSend(
                correlation_id,
                opcode,
                mode_like,
                total_length,
                packet,
                this_context,
                caller_return_address,
                original_result,
                wrapper_send_index,
                kMemorizeSendCorrelationMaxWrapperSends);
            if (wrapper_send_index >= kMemorizeSendCorrelationMaxWrapperSends) {
                LogMemorizeSendBudgetExhausted(
                    correlation_id,
                    kMemorizeSendCorrelationMaxWrapperSends);
                ClearPendingMemorizeSendObservation();
            }
        }
    }

    if (opcode_decoded) {
        const std::uint32_t opcode32 = static_cast<std::uint32_t>(opcode);
        if (opcode32 == kMoveItemOpcode) {
            LogMoveItemSendObserved(
                mode_like,
                total_length,
                payload_length,
                packet,
                this_context,
                caller_return_address,
                original_result);
        }
        if (opcode32 == kDeleteSpellOpcode) {
            if (g_spellbook_scribe_pending_correlation_id != 0) {
                LogPendingSpellbookScribeGap(L"next_delete_spell_send");
            }
            g_spellbook_scribe_pending_correlation_id = ++g_spellbook_scribe_correlation_count;
            g_spellbook_scribe_pending_wrapper_sends = 1;
            LogSpellbookScribeSendEvent(
                L"delete_send_start",
                g_spellbook_scribe_pending_correlation_id,
                opcode32,
                mode_like,
                total_length,
                packet,
                this_context,
                caller_return_address,
                original_result,
                g_spellbook_scribe_pending_wrapper_sends,
                kSpellbookScribeCorrelationMaxWrapperSends);
        } else if (spellbook_scribe_correlation_id != 0) {
            const bool completes_scribe = opcode32 == kMemorizeSpellOpcode;
            LogSpellbookScribeSendEvent(
                completes_scribe ? L"memorize_send_followup" : L"intermediate_send",
                spellbook_scribe_correlation_id,
                opcode32,
                mode_like,
                total_length,
                packet,
                this_context,
                caller_return_address,
                original_result,
                spellbook_scribe_wrapper_send_index,
                kSpellbookScribeCorrelationMaxWrapperSends);
            if (completes_scribe) {
                g_spellbook_scribe_pending_correlation_id = 0;
                g_spellbook_scribe_pending_wrapper_sends = 0;
            } else if (spellbook_scribe_wrapper_send_index >=
                kSpellbookScribeCorrelationMaxWrapperSends) {
                LogSpellbookScribeBudgetExhausted(
                    spellbook_scribe_correlation_id,
                    kSpellbookScribeCorrelationMaxWrapperSends);
                g_spellbook_scribe_pending_correlation_id = 0;
                g_spellbook_scribe_pending_wrapper_sends = 0;
            }
        }
    }

    if (opcode_decoded && static_cast<std::uint32_t>(opcode) == kMemorizeSpellOpcode &&
        correlation_id == 0) {
        LogMemorizeSendObserved(
            0,
            0,
            mode_like,
            total_length,
            packet,
            this_context,
            caller_return_address,
            original_result);
    }

    return original_result;
}

void MONOMYTH_FASTCALL HandleRButtonUpHook(
    void* this_slot,
    void*,
    void* point) noexcept {
    const std::uint32_t previous_correlation = g_scroll_scribe_active_correlation_id;
    const bool previous_logging = g_scroll_scribe_active_logging;
    ++g_scroll_scribe_event_count;
    g_scroll_scribe_active_correlation_id =
        static_cast<std::uint32_t>(g_scroll_scribe_event_count);
    g_scroll_scribe_active_logging =
        ShouldLogScrollScribeTrace(g_scroll_scribe_event_count);

    LogHandleRButtonUpTrace(L"enter", this_slot, point);
    g_original_handle_rbutton_up(this_slot, point);
    LogHandleRButtonUpTrace(L"exit", this_slot, point);

    g_scroll_scribe_active_correlation_id = previous_correlation;
    g_scroll_scribe_active_logging = previous_logging;
}

int MONOMYTH_FASTCALL IsClassUsablePredicateHook(
    void* this_character,
    void*,
    unsigned int class_id) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const int original_result =
        g_original_is_class_usable_predicate(this_character, class_id);
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    int returned_result = original_result;
    const bool behavior_override_applied =
        g_multiclass_item_usability_enabled &&
        original_result == 0 &&
        monomyth::multiclass_identity::HasAuthoritativeClass(
            snapshot.has_classes_bitmask,
            snapshot.classes_bitmask,
            class_id);
    if (behavior_override_applied) {
        returned_result = 1;
        ++g_is_class_usable_predicate_override_count;
        LogItemUsabilityOverride(
            this_character,
            caller_return_address,
            class_id,
            original_result,
            returned_result,
            snapshot);
    }
    LogIsClassUsablePredicateTrace(
        this_character,
        class_id,
        original_result,
        returned_result,
        behavior_override_applied);
    return returned_result;
}

int MONOMYTH_FASTCALL CanEquipHook(
    void* this_context,
    void*,
    void* inventory_or_container_like,
    void* item_like,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const int original_result =
        g_original_can_equip(
            this_context,
            inventory_or_container_like,
            item_like,
            arg3,
            arg4,
            arg5);
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uint32_t caller_return_rva =
        module_base != 0 && caller_return_address >= module_base
            ? static_cast<std::uint32_t>(caller_return_address - module_base)
            : 0;
    const bool should_log_observation =
        caller_return_rva == 0 || !IsInventoryScanCanEquipCaller(caller_return_rva);
    if (!should_log_observation && (!g_multiclass_item_usability_enabled || original_result != 0)) {
        return original_result;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    const std::uint32_t item_class_mask = ReadClientItemClassMask(item_like);
    const bool item_matches_assigned_class =
        monomyth::multiclass_identity::HasAnyAuthoritativeClientItemClass(
            snapshot.has_classes_bitmask,
            snapshot.classes_bitmask,
            item_class_mask);
    if (should_log_observation) {
        LogCanEquipObservation(
            this_context,
            caller_return_address,
            inventory_or_container_like,
            item_like,
            arg3,
            arg4,
            arg5,
            original_result,
            item_class_mask,
            item_matches_assigned_class,
            snapshot);
    }
    if (!g_multiclass_item_usability_enabled || original_result != 0) {
        return original_result;
    }

    if (!item_matches_assigned_class) {
        return original_result;
    }

    // The live client's remaining item gate sits after the raw item-class predicate and
    // is still keyed to the base profile. When the client-local item class mask intersects
    // the authoritative assigned classes, allow the equip attempt and keep unrelated items
    // fail-closed.
    LogCanEquipOverride(
        this_context,
        caller_return_address,
        inventory_or_container_like,
        item_like,
        arg3,
        arg4,
        arg5,
        original_result,
        item_class_mask,
        snapshot);
    return 1;
}

bool MONOMYTH_FASTCALL InvSlotMgrMoveItemHook(
    void* this_context,
    void*,
    void* move_data_a,
    void* move_data_b,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5,
    unsigned long arg6) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const bool original_result =
        g_original_inv_slot_mgr_move_item(
            this_context,
            move_data_a,
            move_data_b,
            arg3,
            arg4,
            arg5,
            arg6);
    LogInvSlotMgrMoveItemTrace(
        this_context,
        caller_return_address,
        move_data_a,
        move_data_b,
        arg3,
        arg4,
        arg5,
        arg6,
        original_result);
    return original_result;
}

int MONOMYTH_FASTCALL EquipClickCanEquipCallsiteHook(
    void* this_context,
    void*,
    void* inventory_or_container_like,
    void* item_like,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const int original_result =
        g_original_can_equip(
            this_context,
            inventory_or_container_like,
            item_like,
            arg3,
            arg4,
            arg5);
    if (!g_multiclass_item_usability_enabled || original_result != 0) {
        LogEquipClickCanEquipCallsiteTrace(
            this_context,
            caller_return_address,
            inventory_or_container_like,
            item_like,
            arg3,
            arg4,
            arg5,
            original_result,
            original_result);
        return original_result;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    const std::uint32_t item_class_mask = ReadClientItemClassMask(item_like);
    if (!monomyth::multiclass_identity::HasAnyAuthoritativeClientItemClass(
            snapshot.has_classes_bitmask,
            snapshot.classes_bitmask,
            item_class_mask)) {
        LogEquipClickCanEquipCallsiteTrace(
            this_context,
            caller_return_address,
            inventory_or_container_like,
            item_like,
            arg3,
            arg4,
            arg5,
            original_result,
            original_result);
        return original_result;
    }

    LogCanEquipOverride(
        this_context,
        caller_return_address,
        inventory_or_container_like,
        item_like,
        arg3,
        arg4,
        arg5,
        original_result,
        item_class_mask,
        snapshot);
    LogEquipClickCanEquipCallsiteTrace(
        this_context,
        caller_return_address,
        inventory_or_container_like,
        item_like,
        arg3,
        arg4,
        arg5,
        original_result,
        1);
    return 1;
}

void* MONOMYTH_FASTCALL EquipClickRecordLookupCallsiteHook(
    void* this_context,
    void*,
    unsigned long lookup_id) noexcept {
    void* const original_result = g_original_equip_record_lookup(this_context, lookup_id);
    LogEquipRecordLookupTrace(
        L"EquipClickRecordLookup",
        kEquipClickRecordLookupCallsiteRva,
        this_context,
        lookup_id,
        original_result);
    return original_result;
}

void* MONOMYTH_FASTCALL EquipClickRequirementLookupCallsiteHook(
    void* this_context,
    void*,
    void* record_or_table_like,
    void* descriptor_like,
    unsigned long mode_like) noexcept {
    void* const original_result = g_original_equip_requirement_lookup(
        this_context,
        record_or_table_like,
        descriptor_like,
        mode_like);
    LogEquipRequirementLookupTrace(
        L"EquipClickRequirementLookup",
        kEquipClickRequirementLookupCallsiteRva,
        this_context,
        record_or_table_like,
        descriptor_like,
        mode_like,
        original_result);
    return original_result;
}

void* MONOMYTH_FASTCALL EquipLocalRecordLookupCallsiteHook(
    void* this_context,
    void*,
    unsigned long lookup_id) noexcept {
    void* const original_result = g_original_equip_record_lookup(this_context, lookup_id);
    LogEquipRecordLookupTrace(
        L"EquipLocalRecordLookup",
        kEquipLocalRecordLookupCallsiteRva,
        this_context,
        lookup_id,
        original_result);
    return original_result;
}

void* MONOMYTH_FASTCALL EquipLocalRequirementLookupACallsiteHook(
    void* this_context,
    void*,
    void* record_or_table_like,
    void* descriptor_like,
    unsigned long mode_like) noexcept {
    void* const original_result = g_original_equip_requirement_lookup(
        this_context,
        record_or_table_like,
        descriptor_like,
        mode_like);
    LogEquipRequirementLookupTrace(
        L"EquipLocalRequirementLookupA",
        kEquipLocalRequirementLookupACallsiteRva,
        this_context,
        record_or_table_like,
        descriptor_like,
        mode_like,
        original_result);
    return original_result;
}

void* MONOMYTH_FASTCALL EquipLocalRequirementLookupBCallsiteHook(
    void* this_context,
    void*,
    void* record_or_table_like,
    void* descriptor_like,
    unsigned long mode_like) noexcept {
    void* const original_result = g_original_equip_requirement_lookup(
        this_context,
        record_or_table_like,
        descriptor_like,
        mode_like);
    LogEquipRequirementLookupTrace(
        L"EquipLocalRequirementLookupB",
        kEquipLocalRequirementLookupBCallsiteRva,
        this_context,
        record_or_table_like,
        descriptor_like,
        mode_like,
        original_result);
    return original_result;
}

int MONOMYTH_FASTCALL EquipNestedInventoryGateCallsiteHook(
    void* this_context,
    void*,
    unsigned long arg1,
    unsigned long arg2,
    unsigned long arg3) noexcept {
    const int original_result =
        g_original_equip_nested_inventory_gate(this_context, arg1, arg2, arg3);
    LogEquipNestedInventoryGateTrace(
        this_context,
        arg1,
        arg2,
        arg3,
        original_result);
    return original_result;
}

int MONOMYTH_FASTCALL EquipNestedValidatorCallsiteHook(
    void* this_context,
    void*,
    void* arg1,
    void* arg2,
    unsigned long arg3) noexcept {
    const std::uint32_t previous_id = g_active_equip_nested_validation_id;
    ++g_equip_nested_validation_count;
    g_active_equip_nested_validation_id = g_equip_nested_validation_count;
    const int original_result =
        g_original_equip_nested_validator(this_context, arg1, arg2, arg3);
    LogEquipNestedValidatorTrace(
        this_context,
        arg1,
        arg2,
        arg3,
        original_result);
    g_active_equip_nested_validation_id = previous_id;
    return original_result;
}

void* MONOMYTH_FASTCALL EquipLocalRejectMessageCallsiteHook(
    void* this_context,
    void*,
    unsigned long message_id,
    unsigned long arg2,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    void* const original_result = g_original_equip_message_resolver(
        this_context,
        message_id,
        arg2,
        arg3,
        arg4,
        arg5);
    LogEquipLocalRejectMessageTrace(
        L"EquipLocalRejectMessage",
        kEquipLocalRejectMessageCallsiteRva,
        this_context,
        caller_return_address,
        message_id,
        arg2,
        arg3,
        arg4,
        arg5,
        original_result);
    return original_result;
}

void* MONOMYTH_FASTCALL DragDropLocalRejectMessageCallsiteHook(
    void* this_context,
    void*,
    unsigned long message_id,
    unsigned long arg2,
    unsigned long arg3,
    unsigned long arg4,
    unsigned long arg5) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    void* const original_result = g_original_equip_message_resolver(
        this_context,
        message_id,
        arg2,
        arg3,
        arg4,
        arg5);
    LogEquipLocalRejectMessageTrace(
        L"DragDropLocalRejectMessage",
        kDragDropLocalRejectMessageCallsiteRva,
        this_context,
        caller_return_address,
        message_id,
        arg2,
        arg3,
        arg4,
        arg5,
        original_result);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL DragDropSilentPrecheckCallsiteHook(
    void* this_context,
    void*,
    void* probe_like) noexcept {
    const std::uint8_t original_result =
        g_original_drag_drop_silent_precheck(this_context, probe_like);
    LogDragDropSilentPrecheckTrace(this_context, probe_like, original_result);
    return original_result;
}

void MONOMYTH_FASTCALL MoveItemFromSlotResolveCallsiteHook(
    void* this_context,
    void*,
    void* slot_like,
    void* resolved_slot_like) noexcept {
    g_original_move_item_slot_resolve(this_context, slot_like, resolved_slot_like);
    LogMoveItemLocalSlotResolveTrace(
        L"MoveItemFromSlotResolve",
        L"from_slot",
        kMoveItemFromSlotResolveCallsiteRva,
        this_context,
        slot_like,
        resolved_slot_like,
        GetCallerReturnAddress());
}

void MONOMYTH_FASTCALL MoveItemToSlotResolveCallsiteHook(
    void* this_context,
    void*,
    void* slot_like,
    void* resolved_slot_like) noexcept {
    g_original_move_item_slot_resolve(this_context, slot_like, resolved_slot_like);
    LogMoveItemLocalSlotResolveTrace(
        L"MoveItemToSlotResolve",
        L"to_slot",
        kMoveItemToSlotResolveCallsiteRva,
        this_context,
        slot_like,
        resolved_slot_like,
        GetCallerReturnAddress());
}

int MONOMYTH_FASTCALL MoveItemBranchKindSiteACallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    std::uint32_t owner_state_04 = 0;
    const bool owner_state_04_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x4, &owner_state_04);
    std::uintptr_t owner_pointer_08 = 0;
    const bool owner_pointer_08_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x8,
                      &owner_pointer_08);
    std::uintptr_t owner_field_98 = 0;
    const bool owner_field_98_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x98,
                      &owner_field_98);
    std::uintptr_t primary_pointer_9c = 0;
    const bool primary_pointer_9c_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x9c,
                      &primary_pointer_9c);
    std::uintptr_t owner_field_104 = 0;
    const bool owner_field_104_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x104,
                      &owner_field_104);
    std::uintptr_t owner_field_134 = 0;
    const bool owner_field_134_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x134,
                      &owner_field_134);
    std::uintptr_t fallback_pointer_144 = 0;
    const bool fallback_pointer_144_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x144,
                      &fallback_pointer_144);

    std::uintptr_t vtable_address = 0;
    const bool vtable_copied = TryCopyObject(this_context, &vtable_address);
    std::uintptr_t resolver_address = 0;
    const bool resolver_copied =
        vtable_copied && vtable_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(vtable_address + 0x8), &resolver_address);
    std::uintptr_t resolved_object_address = 0;
    if (resolver_copied && resolver_address != 0) {
        resolved_object_address = reinterpret_cast<std::uintptr_t>(
            reinterpret_cast<MoveItemBranchResolvedObjectFn>(resolver_address)(this_context));
    }

    std::uint8_t resolved_flag_value = 0;
    const bool resolved_flag_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x11a),
                      &resolved_flag_value);
    std::uint8_t resolved_flag_10e_value = 0;
    const bool resolved_flag_10e_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x10e),
                      &resolved_flag_10e_value);
    std::uint8_t resolved_flag_110_value = 0;
    const bool resolved_flag_110_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x110),
                      &resolved_flag_110_value);
    std::uint32_t resolved_dword_114_value = 0;
    const bool resolved_dword_114_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x114),
                      &resolved_dword_114_value);
    std::uint32_t resolved_dword_11c_value = 0;
    const bool resolved_dword_11c_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x11c),
                      &resolved_dword_11c_value);
    std::uint8_t resolved_flag_120_value = 0;
    const bool resolved_flag_120_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x120),
                      &resolved_flag_120_value);
    std::uint8_t resolved_field_521_value = 0;
    const bool resolved_field_521_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x521),
                      &resolved_field_521_value);
    std::uint8_t resolved_field_522_value = 0;
    const bool resolved_field_522_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x522),
                      &resolved_field_522_value);
    const int original_result = resolved_flag_copied ? resolved_flag_value : 0;
    LogMoveItemBranchKindTrace(
        L"MoveItemBranchKindSiteA",
        kMoveItemBranchKindSiteACallsiteRva,
        kMoveItemBranchKindTargetRva,
        this_context,
        caller_return_address,
        original_result,
        owner_state_04,
        owner_state_04_copied,
        owner_pointer_08_copied ? owner_pointer_08 : 0,
        owner_pointer_08_copied,
        owner_field_98_copied ? owner_field_98 : 0,
        owner_field_98_copied,
        primary_pointer_9c_copied ? primary_pointer_9c : 0,
        primary_pointer_9c_copied,
        owner_field_104_copied ? owner_field_104 : 0,
        owner_field_104_copied,
        owner_field_134_copied ? owner_field_134 : 0,
        owner_field_134_copied,
        fallback_pointer_144_copied ? fallback_pointer_144 : 0,
        fallback_pointer_144_copied,
        vtable_address,
        resolver_copied ? resolver_address : 0,
        resolved_object_address,
        resolved_flag_10e_copied,
        resolved_flag_10e_value,
        resolved_flag_110_copied,
        resolved_flag_110_value,
        resolved_dword_114_copied,
        resolved_dword_114_value,
        resolved_flag_copied,
        resolved_flag_value,
        resolved_dword_11c_copied,
        resolved_dword_11c_value,
        resolved_flag_120_copied,
        resolved_flag_120_value,
        resolved_field_521_copied,
        resolved_field_521_value,
        resolved_field_522_copied,
        resolved_field_522_value,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

int MONOMYTH_FASTCALL MoveItemBranchKindSiteBCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    std::uint32_t owner_state_04 = 0;
    const bool owner_state_04_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x4, &owner_state_04);
    std::uintptr_t owner_pointer_08 = 0;
    const bool owner_pointer_08_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x8,
                      &owner_pointer_08);
    std::uintptr_t owner_field_98 = 0;
    const bool owner_field_98_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x98,
                      &owner_field_98);
    std::uintptr_t primary_pointer_9c = 0;
    const bool primary_pointer_9c_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x9c,
                      &primary_pointer_9c);
    std::uintptr_t owner_field_104 = 0;
    const bool owner_field_104_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x104,
                      &owner_field_104);
    std::uintptr_t owner_field_134 = 0;
    const bool owner_field_134_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x134,
                      &owner_field_134);
    std::uintptr_t fallback_pointer_144 = 0;
    const bool fallback_pointer_144_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x144,
                      &fallback_pointer_144);

    std::uintptr_t vtable_address = 0;
    const bool vtable_copied = TryCopyObject(this_context, &vtable_address);
    std::uintptr_t resolver_address = 0;
    const bool resolver_copied =
        vtable_copied && vtable_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(vtable_address + 0x8), &resolver_address);
    std::uintptr_t resolved_object_address = 0;
    if (resolver_copied && resolver_address != 0) {
        resolved_object_address = reinterpret_cast<std::uintptr_t>(
            reinterpret_cast<MoveItemBranchResolvedObjectFn>(resolver_address)(this_context));
    }

    std::uint8_t resolved_flag_value = 0;
    const bool resolved_flag_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x11a),
                      &resolved_flag_value);
    std::uint8_t resolved_flag_10e_value = 0;
    const bool resolved_flag_10e_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x10e),
                      &resolved_flag_10e_value);
    std::uint8_t resolved_flag_110_value = 0;
    const bool resolved_flag_110_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x110),
                      &resolved_flag_110_value);
    std::uint32_t resolved_dword_114_value = 0;
    const bool resolved_dword_114_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x114),
                      &resolved_dword_114_value);
    std::uint32_t resolved_dword_11c_value = 0;
    const bool resolved_dword_11c_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x11c),
                      &resolved_dword_11c_value);
    std::uint8_t resolved_flag_120_value = 0;
    const bool resolved_flag_120_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x120),
                      &resolved_flag_120_value);
    std::uint8_t resolved_field_521_value = 0;
    const bool resolved_field_521_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x521),
                      &resolved_field_521_value);
    std::uint8_t resolved_field_522_value = 0;
    const bool resolved_field_522_copied =
        resolved_object_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_address + 0x522),
                      &resolved_field_522_value);
    const int original_result = resolved_flag_copied ? resolved_flag_value : 0;
    LogMoveItemBranchKindTrace(
        L"MoveItemBranchKindSiteB",
        kMoveItemBranchKindSiteBCallsiteRva,
        kMoveItemBranchKindTargetRva,
        this_context,
        caller_return_address,
        original_result,
        owner_state_04,
        owner_state_04_copied,
        owner_pointer_08_copied ? owner_pointer_08 : 0,
        owner_pointer_08_copied,
        owner_field_98_copied ? owner_field_98 : 0,
        owner_field_98_copied,
        primary_pointer_9c_copied ? primary_pointer_9c : 0,
        primary_pointer_9c_copied,
        owner_field_104_copied ? owner_field_104 : 0,
        owner_field_104_copied,
        owner_field_134_copied ? owner_field_134 : 0,
        owner_field_134_copied,
        fallback_pointer_144_copied ? fallback_pointer_144 : 0,
        fallback_pointer_144_copied,
        vtable_address,
        resolver_copied ? resolver_address : 0,
        resolved_object_address,
        resolved_flag_10e_copied,
        resolved_flag_10e_value,
        resolved_flag_110_copied,
        resolved_flag_110_value,
        resolved_dword_114_copied,
        resolved_dword_114_value,
        resolved_flag_copied,
        resolved_flag_value,
        resolved_dword_11c_copied,
        resolved_dword_11c_value,
        resolved_flag_120_copied,
        resolved_flag_120_value,
        resolved_field_521_copied,
        resolved_field_521_value,
        resolved_field_522_copied,
        resolved_field_522_value,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL MoveItemBranchBoolSiteACallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    std::uint32_t state_at_14 = 0;
    const bool state_at_14_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x14, &state_at_14);
    const std::uint8_t original_result =
        state_at_14_copied && state_at_14 == 1 ? 1 : 0;
    LogMoveItemBranchBoolTrace(
        L"MoveItemBranchBoolSiteA",
        kMoveItemBranchBoolSiteACallsiteRva,
        kMoveItemBranchBoolTargetRva,
        this_context,
        caller_return_address,
        original_result,
        state_at_14,
        state_at_14_copied,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL MoveItemBranchBoolSiteBCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    std::uint32_t state_at_14 = 0;
    const bool state_at_14_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x14, &state_at_14);
    const std::uint8_t original_result =
        state_at_14_copied && state_at_14 == 1 ? 1 : 0;
    LogMoveItemBranchBoolTrace(
        L"MoveItemBranchBoolSiteB",
        kMoveItemBranchBoolSiteBCallsiteRva,
        kMoveItemBranchBoolTargetRva,
        this_context,
        caller_return_address,
        original_result,
        state_at_14,
        state_at_14_copied,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL MoveItemStackLocalGateSiteBCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> local_bytes_before = {};
    const bool local_bytes_before_copied =
        TryCopyBytes(this_context, local_bytes_before.size(), local_bytes_before.data());
    ClientInventorySlotWire local_slot_before = {};
    const bool local_slot_before_copied = TryCopyObject(this_context, &local_slot_before);
    std::uint32_t local_dword0 = 0;
    const bool local_dword0_copied = TryCopyObject(this_context, &local_dword0);
    const bool dword0_zero = local_dword0_copied && local_dword0 == 0;
    const bool slot_word_valid =
        local_slot_before_copied &&
        static_cast<std::uint16_t>(local_slot_before.slot) != 0xffffu;
    const bool subindex_is_ffff =
        local_slot_before_copied &&
        static_cast<std::uint16_t>(local_slot_before.subindex) == 0xffffu;
    const bool slot_lte_0x16 =
        local_slot_before_copied &&
        static_cast<std::uint16_t>(local_slot_before.slot) <= 0x16u;
    const std::uint8_t original_result =
        dword0_zero && slot_word_valid && subindex_is_ffff && slot_lte_0x16 ? 1 : 0;

    std::array<std::uint8_t, 24> local_bytes_after = {};
    const bool local_bytes_after_copied =
        TryCopyBytes(this_context, local_bytes_after.size(), local_bytes_after.data());
    ClientInventorySlotWire local_slot_after = {};
    const bool local_slot_after_copied = TryCopyObject(this_context, &local_slot_after);

    LogMoveItemStackLocalGateTrace(
        L"MoveItemStackLocalGateSiteB",
        kMoveItemStackLocalGateSiteBCallsiteRva,
        kMoveItemStackLocalGateTargetRva,
        this_context,
        caller_return_address,
        original_result,
        local_bytes_before,
        local_bytes_before_copied,
        local_bytes_after,
        local_bytes_after_copied,
        dword0_zero,
        slot_word_valid,
        subindex_is_ffff,
        slot_lte_0x16,
        local_slot_before,
        local_slot_before_copied,
        local_slot_after,
        local_slot_after_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCorePrecheckCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_precheck != nullptr
        ? g_original_invslot_handle_lbutton_core_precheck(this_context)
        : 0;
    LogInvSlotHandleLButtonCorePrecheckTrace(
        L"InvSlotHandleLButtonCorePrecheck",
        kInvSlotHandleLButtonCorePrecheckCallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t InvokeInvSlotHandleLButtonCoreManagerPrecheckCallsiteHook(
    const wchar_t* callsite_label,
    std::uint32_t callsite_rva,
    void* this_context) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_precheck != nullptr
        ? g_original_invslot_handle_lbutton_core_precheck(this_context)
        : 0;
    LogInvSlotHandleLButtonCorePrecheckTrace(
        callsite_label,
        callsite_rva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreManagerPrecheckACallsiteHook(
    void* this_context,
    void*) noexcept {
    return InvokeInvSlotHandleLButtonCoreManagerPrecheckCallsiteHook(
        L"InvSlotHandleLButtonCoreManagerPrecheckA",
        kInvSlotHandleLButtonCoreManagerPrecheckACallsiteRva,
        this_context);
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreManagerPrecheckBCallsiteHook(
    void* this_context,
    void*) noexcept {
    return InvokeInvSlotHandleLButtonCoreManagerPrecheckCallsiteHook(
        L"InvSlotHandleLButtonCoreManagerPrecheckB",
        kInvSlotHandleLButtonCoreManagerPrecheckBCallsiteRva,
        this_context);
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreManagerPrecheckCCallsiteHook(
    void* this_context,
    void*) noexcept {
    return InvokeInvSlotHandleLButtonCoreManagerPrecheckCallsiteHook(
        L"InvSlotHandleLButtonCoreManagerPrecheckC",
        kInvSlotHandleLButtonCoreManagerPrecheckCCallsiteRva,
        this_context);
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreManagerPrecheckDCallsiteHook(
    void* this_context,
    void*) noexcept {
    return InvokeInvSlotHandleLButtonCoreManagerPrecheckCallsiteHook(
        L"InvSlotHandleLButtonCoreManagerPrecheckD",
        kInvSlotHandleLButtonCoreManagerPrecheckDCallsiteRva,
        this_context);
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreManagerPrecheckECallsiteHook(
    void* this_context,
    void*) noexcept {
    return InvokeInvSlotHandleLButtonCoreManagerPrecheckCallsiteHook(
        L"InvSlotHandleLButtonCoreManagerPrecheckE",
        kInvSlotHandleLButtonCoreManagerPrecheckECallsiteRva,
        this_context);
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreManagerPrecheckFCallsiteHook(
    void* this_context,
    void*) noexcept {
    return InvokeInvSlotHandleLButtonCoreManagerPrecheckCallsiteHook(
        L"InvSlotHandleLButtonCoreManagerPrecheckF",
        kInvSlotHandleLButtonCoreManagerPrecheckFCallsiteRva,
        this_context);
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCorePostLookupModeGateCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_post_lookup_mode_gate != nullptr
        ? g_original_invslot_handle_lbutton_core_post_lookup_mode_gate(this_context)
        : 0;
    LogInvSlotHandleLButtonCorePostLookupModeGateTrace(
        L"InvSlotHandleLButtonCorePostLookupModeGate",
        kInvSlotHandleLButtonCorePostLookupModeGateCallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes);
    return original_result;
}

void MONOMYTH_FASTCALL InvSlotHandleLButtonCorePostLookupUiPulseCallsiteHook(
    void* this_context,
    void*,
    std::uint32_t target_id,
    std::int32_t value_a,
    std::int32_t value_b,
    std::int32_t x,
    std::int32_t y,
    std::uint32_t one_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes_before = {};
    TryCopyBytes(this_context, context_bytes_before.size(), context_bytes_before.data());
    if (g_original_invslot_handle_lbutton_core_post_lookup_ui_pulse != nullptr) {
        g_original_invslot_handle_lbutton_core_post_lookup_ui_pulse(
            this_context,
            target_id,
            value_a,
            value_b,
            x,
            y,
            one_like,
            zero_a,
            zero_b);
    }
    std::array<std::uint8_t, 24> context_bytes_after = {};
    TryCopyBytes(this_context, context_bytes_after.size(), context_bytes_after.data());
    LogInvSlotHandleLButtonCorePostLookupUiPulseTrace(
        L"InvSlotHandleLButtonCorePostLookupUiPulse",
        kInvSlotHandleLButtonCorePostLookupUiPulseCallsiteRva,
        this_context,
        caller_return_address,
        target_id,
        value_a,
        value_b,
        x,
        y,
        one_like,
        zero_a,
        zero_b,
        context_bytes_before,
        context_bytes_after);
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateBranchGateACallsiteHook(
    void* slot_record_like,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    ClientInventorySlotWire slot_record = {};
    const bool slot_record_copied = TryCopyObject(slot_record_like, &slot_record);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_record_bytes = {};
    const bool slot_record_bytes_copied =
        TryCopyBytes(slot_record_like, slot_record_bytes.size(), slot_record_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_late_branch_gate_a != nullptr
        ? g_original_invslot_handle_lbutton_core_late_branch_gate_a(slot_record_like)
        : 0;
    LogInvSlotHandleLButtonCoreLateBranchGateTrace(
        L"InvSlotHandleLButtonCoreLateBranchGateA",
        kInvSlotHandleLButtonCoreLateBranchGateATargetRva,
        L"InvSlotHandleLButtonCoreLateBranchGateA",
        kInvSlotHandleLButtonCoreLateBranchGateACallsiteRva,
        slot_record_like,
        caller_return_address,
        original_result,
        slot_record,
        slot_record_copied,
        slot_record_bytes,
        slot_record_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateBranchPrepCallsiteHook(
    void* this_context,
    void*,
    std::int32_t slot_like,
    void* lookup_result_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes_before = {};
    const bool context_bytes_before_copied =
        TryCopyBytes(this_context, context_bytes_before.size(), context_bytes_before.data());
    std::uint32_t lookup_dword0_before = 0;
    const bool lookup_dword0_before_copied = TryCopyObject(lookup_result_like, &lookup_dword0_before);
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_late_branch_prep != nullptr
        ? g_original_invslot_handle_lbutton_core_late_branch_prep(
              this_context,
              slot_like,
              lookup_result_like)
        : 0;
    std::array<std::uint8_t, 24> context_bytes_after = {};
    const bool context_bytes_after_copied =
        TryCopyBytes(this_context, context_bytes_after.size(), context_bytes_after.data());
    std::uint32_t lookup_dword0_after = 0;
    const bool lookup_dword0_after_copied = TryCopyObject(lookup_result_like, &lookup_dword0_after);
    if (g_multiclass_item_usability_enabled &&
        original_result == 0 &&
        slot_like == kSecondaryEquipmentSlot &&
        lookup_dword0_before_copied &&
        lookup_dword0_before != 0) {
        const monomyth::server_auth_stats::Snapshot snapshot =
            monomyth::server_auth_stats::GetSnapshot();
        const std::uintptr_t item_like =
            g_invslot_handle_lbutton_core_last_late_lookup_item_pointer;
        const OffhandWeaponPolicySnapshot offhand_policy =
            CaptureOffhandWeaponPolicySnapshot(item_like, snapshot);
        const bool is_offhand_weapon_attempt =
            offhand_policy.is_weapon && offhand_policy.can_wear_secondary;
        if (is_offhand_weapon_attempt && offhand_policy.eligible) {
            ++g_invslot_handle_lbutton_core_late_branch_prep_override_count;
            LogInvSlotHandleLButtonCoreLateBranchPrepOverride(
                this_context,
                slot_like,
                lookup_result_like,
                caller_return_address,
                original_result,
                lookup_dword0_before,
                lookup_dword0_before_copied,
                lookup_dword0_after,
                lookup_dword0_after_copied,
                item_like,
                offhand_policy,
                snapshot);
            return 1;
        }
        if (is_offhand_weapon_attempt) {
            LogInvSlotHandleLButtonCoreLateBranchPrepObservation(
                this_context,
                slot_like,
                lookup_result_like,
                caller_return_address,
                original_result,
                lookup_dword0_before,
                lookup_dword0_before_copied,
                lookup_dword0_after,
                lookup_dword0_after_copied,
                item_like,
                offhand_policy,
                snapshot);
        }
    }
    LogInvSlotHandleLButtonCoreLateBranchPrepTrace(
        L"InvSlotHandleLButtonCoreLateBranchPrep",
        kInvSlotHandleLButtonCoreLateBranchPrepCallsiteRva,
        this_context,
        slot_like,
        lookup_result_like,
        caller_return_address,
        original_result,
        lookup_dword0_before,
        lookup_dword0_before_copied,
        lookup_dword0_after,
        lookup_dword0_after_copied,
        context_bytes_before,
        context_bytes_before_copied,
        context_bytes_after,
        context_bytes_after_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateBranchGateBCallsiteHook(
    void* slot_record_like,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    ClientInventorySlotWire slot_record = {};
    const bool slot_record_copied = TryCopyObject(slot_record_like, &slot_record);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_record_bytes = {};
    const bool slot_record_bytes_copied =
        TryCopyBytes(slot_record_like, slot_record_bytes.size(), slot_record_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_late_branch_gate_b != nullptr
        ? g_original_invslot_handle_lbutton_core_late_branch_gate_b(slot_record_like)
        : 0;
    if (g_multiclass_item_usability_enabled &&
        original_result == 0 &&
        slot_record_copied &&
        slot_record.type == 0 &&
        slot_record.unknown02 == 0 &&
        slot_record.slot == kSecondaryEquipmentSlot &&
        slot_record.subindex == -1) {
        const monomyth::server_auth_stats::Snapshot snapshot =
            monomyth::server_auth_stats::GetSnapshot();
        const std::uintptr_t item_like =
            g_invslot_handle_lbutton_core_last_late_lookup_item_pointer;
        const OffhandWeaponPolicySnapshot offhand_policy =
            CaptureOffhandWeaponPolicySnapshot(item_like, snapshot);
        const bool is_offhand_weapon_attempt =
            offhand_policy.is_weapon && offhand_policy.can_wear_secondary;
        if (is_offhand_weapon_attempt && offhand_policy.eligible) {
            ++g_invslot_handle_lbutton_core_late_branch_gate_b_override_count;
            LogInvSlotHandleLButtonCoreLateBranchGateBOverride(
                slot_record_like,
                caller_return_address,
                original_result,
                slot_record,
                slot_record_copied,
                item_like,
                offhand_policy,
                snapshot);
            return 1;
        }
        if (is_offhand_weapon_attempt) {
            LogInvSlotHandleLButtonCoreLateBranchGateBObservation(
                slot_record_like,
                caller_return_address,
                original_result,
                slot_record,
                slot_record_copied,
                item_like,
                offhand_policy,
                snapshot);
        }
    }
    if (!g_multiclass_item_usability_enabled) {
        LogInvSlotHandleLButtonCoreLateBranchGateTrace(
            L"InvSlotHandleLButtonCoreLateBranchGateB",
            kInvSlotHandleLButtonCoreLateBranchGateBTargetRva,
            L"InvSlotHandleLButtonCoreLateBranchGateB",
            kInvSlotHandleLButtonCoreLateBranchGateBCallsiteRva,
            slot_record_like,
            caller_return_address,
            original_result,
            slot_record,
            slot_record_copied,
            slot_record_bytes,
            slot_record_bytes_copied);
    }
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateBranchDispatchCallsiteHook(
    void* this_context,
    void*,
    void* lookup_result_like,
    std::uint32_t zero_a,
    std::uint32_t zero_b) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes_before = {};
    const bool context_bytes_before_copied =
        TryCopyBytes(this_context, context_bytes_before.size(), context_bytes_before.data());
    std::uint32_t lookup_dword0_before = 0;
    const bool lookup_dword0_before_copied = TryCopyObject(lookup_result_like, &lookup_dword0_before);
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_late_branch_dispatch != nullptr
        ? g_original_invslot_handle_lbutton_core_late_branch_dispatch(
              this_context,
              lookup_result_like,
              zero_a,
              zero_b)
        : 0;
    std::array<std::uint8_t, 24> context_bytes_after = {};
    const bool context_bytes_after_copied =
        TryCopyBytes(this_context, context_bytes_after.size(), context_bytes_after.data());
    std::uint32_t lookup_dword0_after = 0;
    const bool lookup_dword0_after_copied = TryCopyObject(lookup_result_like, &lookup_dword0_after);
    LogInvSlotHandleLButtonCoreLateBranchDispatchTrace(
        L"InvSlotHandleLButtonCoreLateBranchDispatch",
        kInvSlotHandleLButtonCoreLateBranchDispatchCallsiteRva,
        this_context,
        lookup_result_like,
        zero_a,
        zero_b,
        caller_return_address,
        original_result,
        lookup_dword0_before,
        lookup_dword0_before_copied,
        lookup_dword0_after,
        lookup_dword0_after_copied,
        context_bytes_before,
        context_bytes_before_copied,
        context_bytes_after,
        context_bytes_after_copied);
    return original_result;
}

std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsACallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint32_t original_result =
        g_original_invslot_handle_lbutton_core_mode_bits != nullptr
        ? g_original_invslot_handle_lbutton_core_mode_bits(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreModeBitsTrace(
        L"InvSlotHandleLButtonCoreModeBitsA",
        kInvSlotHandleLButtonCoreModeBitsACallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsBCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint32_t original_result =
        g_original_invslot_handle_lbutton_core_mode_bits != nullptr
        ? g_original_invslot_handle_lbutton_core_mode_bits(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreModeBitsTrace(
        L"InvSlotHandleLButtonCoreModeBitsB",
        kInvSlotHandleLButtonCoreModeBitsBCallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsCCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint32_t original_result =
        g_original_invslot_handle_lbutton_core_mode_bits != nullptr
        ? g_original_invslot_handle_lbutton_core_mode_bits(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreModeBitsTrace(
        L"InvSlotHandleLButtonCoreModeBitsC",
        kInvSlotHandleLButtonCoreModeBitsCCallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsDCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint32_t original_result =
        g_original_invslot_handle_lbutton_core_mode_bits != nullptr
        ? g_original_invslot_handle_lbutton_core_mode_bits(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreModeBitsTrace(
        L"InvSlotHandleLButtonCoreModeBitsD",
        kInvSlotHandleLButtonCoreModeBitsDCallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLocalGateCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const auto slot_context = CaptureInvSlotHandleLButtonCoreSlotContext(this_context);
    ClientInventorySlotWire slot_record = {};
    const bool slot_record_copied = TryCopyObject(slot_record_pointer, &slot_record);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_record_bytes = {};
    const bool slot_record_bytes_copied =
        TryCopyBytes(slot_record_pointer, slot_record_bytes.size(), slot_record_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_local_gate != nullptr
        ? g_original_invslot_handle_lbutton_core_local_gate(this_context, slot_record_pointer)
        : 0;
    LogInvSlotHandleLButtonCoreLocalGateTrace(
        L"InvSlotHandleLButtonCoreLocalGate",
        kInvSlotHandleLButtonCoreLocalGateCallsiteRva,
        this_context,
        caller_return_address,
        slot_record_pointer,
        original_result,
        slot_record,
        slot_record_copied,
        slot_record_bytes,
        slot_record_bytes_copied,
        slot_context);
    return original_result;
}

void* MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolveObjectCallsiteHook(
    void* this_context,
    void*,
    void* output_pointer) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const auto slot_context = CaptureInvSlotHandleLButtonCoreSlotContext(this_context);
    void* original_result =
        g_original_invslot_handle_lbutton_core_resolve_object != nullptr
        ? g_original_invslot_handle_lbutton_core_resolve_object(this_context, output_pointer)
        : output_pointer;
    std::uintptr_t output_object_after = 0;
    const bool output_object_after_copied = TryCopyObject(output_pointer, &output_object_after);
    std::array<std::uint8_t, 24> output_object_bytes_after = {};
    const bool output_object_bytes_after_copied =
        output_object_after_copied && output_object_after != 0 &&
        TryCopyBytes(reinterpret_cast<const void*>(output_object_after),
                     output_object_bytes_after.size(),
                     output_object_bytes_after.data());
    LogInvSlotHandleLButtonCoreResolveObjectTrace(
        L"InvSlotHandleLButtonCoreResolveObject",
        kInvSlotHandleLButtonCoreResolveObjectCallsiteRva,
        this_context,
        caller_return_address,
        output_pointer,
        original_result,
        output_object_after_copied ? output_object_after : 0,
        output_object_after_copied,
        output_object_bytes_after,
        output_object_bytes_after_copied,
        slot_context);
    return original_result;
}

int MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedKindACallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const int original_result =
        g_original_invslot_handle_lbutton_core_resolved_kind != nullptr
        ? g_original_invslot_handle_lbutton_core_resolved_kind(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreResolvedKindTrace(
        L"InvSlotHandleLButtonCoreResolvedKindA",
        kInvSlotHandleLButtonCoreResolvedKindACallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

int MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedKindBCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const int original_result =
        g_original_invslot_handle_lbutton_core_resolved_kind != nullptr
        ? g_original_invslot_handle_lbutton_core_resolved_kind(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreResolvedKindTrace(
        L"InvSlotHandleLButtonCoreResolvedKindB",
        kInvSlotHandleLButtonCoreResolvedKindBCallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedFlagACallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_resolved_flag != nullptr
        ? g_original_invslot_handle_lbutton_core_resolved_flag(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreResolvedFlagTrace(
        L"InvSlotHandleLButtonCoreResolvedFlagA",
        kInvSlotHandleLButtonCoreResolvedFlagACallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreResolvedFlagBCallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_resolved_flag != nullptr
        ? g_original_invslot_handle_lbutton_core_resolved_flag(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreResolvedFlagTrace(
        L"InvSlotHandleLButtonCoreResolvedFlagB",
        kInvSlotHandleLButtonCoreResolvedFlagBCallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint32_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreModeBitsECallsiteHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes = {};
    const bool context_bytes_copied =
        TryCopyBytes(this_context, context_bytes.size(), context_bytes.data());
    const std::uint32_t original_result =
        g_original_invslot_handle_lbutton_core_mode_bits != nullptr
        ? g_original_invslot_handle_lbutton_core_mode_bits(this_context)
        : 0;
    LogInvSlotHandleLButtonCoreModeBitsTrace(
        L"InvSlotHandleLButtonCoreModeBitsE",
        kInvSlotHandleLButtonCoreModeBitsECallsiteRva,
        this_context,
        caller_return_address,
        original_result,
        context_bytes,
        context_bytes_copied);
    return original_result;
}

std::uint32_t WINAPI InvSlotHandleLButtonCorePostResolveGateCallsiteHook(
    void* slot_context_like,
    std::uint32_t mode_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const auto snapshot_before =
        CaptureInvSlotHandleLButtonCorePostResolveGateContext(slot_context_like);
    const std::uint32_t original_result =
        g_original_invslot_handle_lbutton_core_post_resolve_gate != nullptr
        ? g_original_invslot_handle_lbutton_core_post_resolve_gate(slot_context_like, mode_like)
        : 0;
    const auto snapshot_after =
        CaptureInvSlotHandleLButtonCorePostResolveGateContext(slot_context_like);
    LogInvSlotHandleLButtonCorePostResolveGateTrace(
        L"InvSlotHandleLButtonCorePostResolveGate",
        kInvSlotHandleLButtonCorePostResolveGateCallsiteRva,
        slot_context_like,
        mode_like,
        caller_return_address,
        original_result,
        snapshot_before,
        snapshot_after);
    return original_result;
}

void MONOMYTH_FASTCALL InvSlotHandleLButtonCoreAltDispatchCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer,
    std::uint32_t mode_flag) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    ClientInventorySlotWire slot_record = {};
    const bool slot_record_copied = TryCopyObject(slot_record_pointer, &slot_record);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_record_bytes = {};
    const bool slot_record_bytes_copied =
        TryCopyBytes(slot_record_pointer, slot_record_bytes.size(), slot_record_bytes.data());
    if (g_original_invslot_handle_lbutton_core_alt_dispatch != nullptr) {
        g_original_invslot_handle_lbutton_core_alt_dispatch(
            this_context,
            slot_record_pointer,
            mode_flag);
    }
    LogInvSlotHandleLButtonCoreAltDispatchTrace(
        L"InvSlotHandleLButtonCoreAltDispatch",
        kInvSlotHandleLButtonCoreAltDispatchCallsiteRva,
        this_context,
        slot_record_pointer,
        mode_flag,
        caller_return_address,
        slot_record,
        slot_record_copied,
        slot_record_bytes,
        slot_record_bytes_copied);
}

void* CDECL InvSlotHandleLButtonCoreSlot17MessageCallsiteHook(
    std::uint32_t message_id,
    void* text_builder_like,
    std::uint32_t zero_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> text_builder_bytes_before = {};
    const bool text_builder_bytes_before_copied =
        TryCopyBytes(text_builder_like, text_builder_bytes_before.size(), text_builder_bytes_before.data());
    void* original_result = nullptr;
    if (g_original_invslot_handle_lbutton_core_slot17_message != nullptr) {
        original_result = g_original_invslot_handle_lbutton_core_slot17_message(
            message_id,
            text_builder_like,
            zero_like);
    }
    std::array<std::uint8_t, 24> text_builder_bytes_after = {};
    const bool text_builder_bytes_after_copied =
        TryCopyBytes(text_builder_like, text_builder_bytes_after.size(), text_builder_bytes_after.data());
    LogInvSlotHandleLButtonCoreSlot17MessageTrace(
        L"InvSlotHandleLButtonCoreSlot17Message",
        kInvSlotHandleLButtonCoreSlot17MessageCallsiteRva,
        message_id,
        text_builder_like,
        zero_like,
        caller_return_address,
        original_result,
        text_builder_bytes_before,
        text_builder_bytes_before_copied,
        text_builder_bytes_after,
        text_builder_bytes_after_copied);
    return original_result;
}

void MONOMYTH_FASTCALL InvSlotHandleLButtonCoreGlobalAction49CallsiteHook(
    void* this_context,
    void*,
    std::uint32_t action_id) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes_before = {};
    const bool context_bytes_before_copied =
        TryCopyBytes(this_context, context_bytes_before.size(), context_bytes_before.data());
    if (g_original_invslot_handle_lbutton_core_global_action49 != nullptr) {
        g_original_invslot_handle_lbutton_core_global_action49(this_context, action_id);
    }
    std::array<std::uint8_t, 24> context_bytes_after = {};
    const bool context_bytes_after_copied =
        TryCopyBytes(this_context, context_bytes_after.size(), context_bytes_after.data());
    LogInvSlotHandleLButtonCoreGlobalAction49Trace(
        L"InvSlotHandleLButtonCoreGlobalAction49",
        kInvSlotHandleLButtonCoreGlobalAction49CallsiteRva,
        this_context,
        action_id,
        caller_return_address,
        context_bytes_before,
        context_bytes_before_copied,
        context_bytes_after,
        context_bytes_after_copied);
}

std::uint8_t CDECL InvSlotHandleLButtonCoreItemRangeGateCallsiteHook(
    std::uint32_t item_id_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const auto snapshot_before = CaptureInvSlotHandleLButtonCorePostResolveGateContext(nullptr);
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_item_range_gate != nullptr
        ? g_original_invslot_handle_lbutton_core_item_range_gate(item_id_like)
        : 0;
    const auto snapshot_after = CaptureInvSlotHandleLButtonCorePostResolveGateContext(nullptr);
    LogInvSlotHandleLButtonCoreItemRangeGateTrace(
        L"InvSlotHandleLButtonCoreItemRangeGate",
        kInvSlotHandleLButtonCoreItemRangeGateCallsiteRva,
        item_id_like,
        caller_return_address,
        original_result,
        snapshot_before,
        snapshot_after);
    return original_result;
}

std::uint8_t MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    ClientInventorySlotWire slot_record = {};
    const bool slot_record_copied = TryCopyObject(slot_record_pointer, &slot_record);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_record_bytes = {};
    const bool slot_record_bytes_copied =
        TryCopyBytes(slot_record_pointer, slot_record_bytes.size(), slot_record_bytes.data());
    const auto manager_before = CaptureInvSlotHandleLButtonCoreLateManagerContext(this_context);
    const std::uint8_t original_result =
        g_original_invslot_handle_lbutton_core_late_slot17_gate != nullptr
        ? g_original_invslot_handle_lbutton_core_late_slot17_gate(
              this_context,
              slot_record_pointer)
        : 0;
    const auto manager_after = CaptureInvSlotHandleLButtonCoreLateManagerContext(this_context);
    LogInvSlotHandleLButtonCoreLateSlot17GateTrace(
        L"InvSlotHandleLButtonCoreLateSlot17Gate",
        kInvSlotHandleLButtonCoreLateSlot17GateCallsiteRva,
        this_context,
        caller_return_address,
        slot_record_pointer,
        original_result,
        slot_record,
        slot_record_copied,
        slot_record_bytes,
        slot_record_bytes_copied,
        manager_before,
        manager_after);
    return original_result;
}

void MONOMYTH_FASTCALL InvSlotHandleLButtonCoreLateSlot17ApplyCallsiteHook(
    void* this_context,
    void*,
    const void* slot_record_pointer) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    ClientInventorySlotWire slot_record = {};
    const bool slot_record_copied = TryCopyObject(slot_record_pointer, &slot_record);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_record_bytes = {};
    const bool slot_record_bytes_copied =
        TryCopyBytes(slot_record_pointer, slot_record_bytes.size(), slot_record_bytes.data());
    const auto manager_before = CaptureInvSlotHandleLButtonCoreLateManagerContext(this_context);
    if (g_original_invslot_handle_lbutton_core_late_slot17_apply != nullptr) {
        g_original_invslot_handle_lbutton_core_late_slot17_apply(this_context, slot_record_pointer);
    }
    const auto manager_after = CaptureInvSlotHandleLButtonCoreLateManagerContext(this_context);
    LogInvSlotHandleLButtonCoreLateSlot17ApplyTrace(
        L"InvSlotHandleLButtonCoreLateSlot17Apply",
        kInvSlotHandleLButtonCoreLateSlot17ApplyCallsiteRva,
        this_context,
        caller_return_address,
        slot_record_pointer,
        slot_record,
        slot_record_copied,
        slot_record_bytes,
        slot_record_bytes_copied,
        manager_before,
        manager_after);
}

void MONOMYTH_FASTCALL EverQuestLMouseUpHook(
    void* this_context,
    void*,
    void* point_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();

    ClientPointWire point_before = {};
    const bool point_before_copied = TryCopyObject(point_like, &point_before);
    std::uintptr_t drag_context_before = 0;
    const bool drag_context_before_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_before);
    std::array<std::uint8_t, 24> drag_context_bytes_before = {};
    const bool drag_context_bytes_before_copied =
        drag_context_before_copied && drag_context_before != 0 &&
        TryCopyBytes(reinterpret_cast<const void*>(drag_context_before),
                     drag_context_bytes_before.size(),
                     drag_context_bytes_before.data());
    std::uintptr_t drag_flags_before = 0;
    const bool drag_flags_before_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragFlagsGlobalRva),
                      &drag_flags_before);
    std::uint8_t drag_flag_10_before = 0;
    const bool drag_flag_10_before_copied =
        drag_flags_before_copied && drag_flags_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_before) + 0x10,
                      &drag_flag_10_before);
    std::uint8_t drag_flag_11_before = 0;
    const bool drag_flag_11_before_copied =
        drag_flags_before_copied && drag_flags_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_before) + 0x11,
                      &drag_flag_11_before);
    std::uintptr_t active_window_before = 0;
    const bool active_window_before_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kActiveWindowGlobalRva),
                      &active_window_before);
    LogEverQuestLMouseUpTrace(
        L"enter",
        this_context,
        caller_return_address,
        point_like,
        point_before,
        point_before_copied,
        drag_context_before,
        drag_context_before_copied,
        drag_context_bytes_before,
        drag_context_bytes_before_copied,
        drag_flags_before,
        drag_flags_before_copied,
        drag_flag_10_before,
        drag_flag_10_before_copied,
        drag_flag_11_before,
        drag_flag_11_before_copied,
        active_window_before,
        active_window_before_copied);

    g_original_everquest_lmouse_up(this_context, point_like);

    ClientPointWire point_after = {};
    const bool point_after_copied = TryCopyObject(point_like, &point_after);
    std::uintptr_t drag_context_after = 0;
    const bool drag_context_after_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_after);
    std::array<std::uint8_t, 24> drag_context_bytes_after = {};
    const bool drag_context_bytes_after_copied =
        drag_context_after_copied && drag_context_after != 0 &&
        TryCopyBytes(reinterpret_cast<const void*>(drag_context_after),
                     drag_context_bytes_after.size(),
                     drag_context_bytes_after.data());
    std::uintptr_t drag_flags_after = 0;
    const bool drag_flags_after_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragFlagsGlobalRva),
                      &drag_flags_after);
    std::uint8_t drag_flag_10_after = 0;
    const bool drag_flag_10_after_copied =
        drag_flags_after_copied && drag_flags_after != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_after) + 0x10,
                      &drag_flag_10_after);
    std::uint8_t drag_flag_11_after = 0;
    const bool drag_flag_11_after_copied =
        drag_flags_after_copied && drag_flags_after != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_after) + 0x11,
                      &drag_flag_11_after);
    std::uintptr_t active_window_after = 0;
    const bool active_window_after_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kActiveWindowGlobalRva),
                      &active_window_after);
    LogEverQuestLMouseUpTrace(
        L"exit",
        this_context,
        caller_return_address,
        point_like,
        point_after,
        point_after_copied,
        drag_context_after,
        drag_context_after_copied,
        drag_context_bytes_after,
        drag_context_bytes_after_copied,
        drag_flags_after,
        drag_flags_after_copied,
        drag_flag_10_after,
        drag_flag_10_after_copied,
        drag_flag_11_after,
        drag_flag_11_after_copied,
        active_window_after,
        active_window_after_copied);
}

int MONOMYTH_FASTCALL CXWndHandleLButtonUpHook(
    void* this_context,
    void*,
    void* point_like,
    std::uint32_t flags_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();

    ClientPointWire point_before = {};
    const bool point_before_copied = TryCopyObject(point_like, &point_before);
    std::uintptr_t vtable_before = 0;
    const bool vtable_before_copied = TryCopyObject(this_context, &vtable_before);
    std::array<std::uint8_t, 24> context_bytes_before = {};
    const bool context_bytes_before_copied =
        TryCopyBytes(this_context, context_bytes_before.size(), context_bytes_before.data());
    std::uintptr_t drag_context_before = 0;
    const bool drag_context_before_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_before);
    std::array<std::uint8_t, 24> drag_context_bytes_before = {};
    const bool drag_context_bytes_before_copied =
        drag_context_before_copied && drag_context_before != 0 &&
        TryCopyBytes(reinterpret_cast<const void*>(drag_context_before),
                     drag_context_bytes_before.size(),
                     drag_context_bytes_before.data());
    std::uintptr_t drag_flags_before = 0;
    const bool drag_flags_before_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragFlagsGlobalRva),
                      &drag_flags_before);
    std::uint8_t drag_flag_10_before = 0;
    const bool drag_flag_10_before_copied =
        drag_flags_before_copied && drag_flags_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_before) + 0x10,
                      &drag_flag_10_before);
    std::uint8_t drag_flag_11_before = 0;
    const bool drag_flag_11_before_copied =
        drag_flags_before_copied && drag_flags_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_before) + 0x11,
                      &drag_flag_11_before);
    std::uintptr_t active_window_before = 0;
    const bool active_window_before_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kActiveWindowGlobalRva),
                      &active_window_before);
    LogCXWndHandleLButtonUpTrace(
        L"enter",
        this_context,
        caller_return_address,
        point_like,
        point_before,
        point_before_copied,
        flags_like,
        0,
        false,
        vtable_before,
        vtable_before_copied,
        context_bytes_before,
        context_bytes_before_copied,
        drag_context_before,
        drag_context_before_copied,
        drag_context_bytes_before,
        drag_context_bytes_before_copied,
        drag_flags_before,
        drag_flags_before_copied,
        drag_flag_10_before,
        drag_flag_10_before_copied,
        drag_flag_11_before,
        drag_flag_11_before_copied,
        active_window_before,
        active_window_before_copied);

    const int original_result =
        g_original_cxwnd_handle_lbutton_up(this_context, point_like, flags_like);

    ClientPointWire point_after = {};
    const bool point_after_copied = TryCopyObject(point_like, &point_after);
    std::uintptr_t vtable_after = 0;
    const bool vtable_after_copied = TryCopyObject(this_context, &vtable_after);
    std::array<std::uint8_t, 24> context_bytes_after = {};
    const bool context_bytes_after_copied =
        TryCopyBytes(this_context, context_bytes_after.size(), context_bytes_after.data());
    std::uintptr_t drag_context_after = 0;
    const bool drag_context_after_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_after);
    std::array<std::uint8_t, 24> drag_context_bytes_after = {};
    const bool drag_context_bytes_after_copied =
        drag_context_after_copied && drag_context_after != 0 &&
        TryCopyBytes(reinterpret_cast<const void*>(drag_context_after),
                     drag_context_bytes_after.size(),
                     drag_context_bytes_after.data());
    std::uintptr_t drag_flags_after = 0;
    const bool drag_flags_after_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragFlagsGlobalRva),
                      &drag_flags_after);
    std::uint8_t drag_flag_10_after = 0;
    const bool drag_flag_10_after_copied =
        drag_flags_after_copied && drag_flags_after != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_after) + 0x10,
                      &drag_flag_10_after);
    std::uint8_t drag_flag_11_after = 0;
    const bool drag_flag_11_after_copied =
        drag_flags_after_copied && drag_flags_after != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_after) + 0x11,
                      &drag_flag_11_after);
    std::uintptr_t active_window_after = 0;
    const bool active_window_after_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kActiveWindowGlobalRva),
                      &active_window_after);
    LogCXWndHandleLButtonUpTrace(
        L"exit",
        this_context,
        caller_return_address,
        point_like,
        point_after,
        point_after_copied,
        flags_like,
        static_cast<std::uint32_t>(original_result),
        true,
        vtable_after,
        vtable_after_copied,
        context_bytes_after,
        context_bytes_after_copied,
        drag_context_after,
        drag_context_after_copied,
        drag_context_bytes_after,
        drag_context_bytes_after_copied,
        drag_flags_after,
        drag_flags_after_copied,
        drag_flag_10_after,
        drag_flag_10_after_copied,
        drag_flag_11_after,
        drag_flag_11_after_copied,
        active_window_after,
        active_window_after_copied);

    return original_result;
}

int MONOMYTH_FASTCALL InventoryWindowWndNotificationHook(
    void* this_context,
    void*,
    void* sender_window,
    std::uint32_t notification_code,
    void* payload_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();
    if (g_multiclass_ui_display_enabled) {
        ArmInventoryClassDisplayCorrelation(
            caller_return_address,
            sender_window,
            notification_code,
            payload_like);
    }

    std::uintptr_t drag_context_before = 0;
    const bool drag_context_before_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_before);
    const bool should_log_before = drag_context_before_copied && drag_context_before != 0;

    std::array<std::uint8_t, 24> drag_context_bytes_before = {};
    const bool drag_context_bytes_before_copied =
        should_log_before &&
        TryCopyBytes(reinterpret_cast<const void*>(drag_context_before),
                     drag_context_bytes_before.size(),
                     drag_context_bytes_before.data());
    std::uintptr_t drag_flags_before = 0;
    const bool drag_flags_before_copied =
        should_log_before &&
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragFlagsGlobalRva),
                      &drag_flags_before);
    std::uint8_t drag_flag_10_before = 0;
    const bool drag_flag_10_before_copied =
        drag_flags_before_copied && drag_flags_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_before) + 0x10,
                      &drag_flag_10_before);
    std::uint8_t drag_flag_11_before = 0;
    const bool drag_flag_11_before_copied =
        drag_flags_before_copied && drag_flags_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_before) + 0x11,
                      &drag_flag_11_before);
    std::uintptr_t active_window_before = 0;
    const bool active_window_before_copied =
        should_log_before &&
        TryCopyObject(reinterpret_cast<const void*>(module_base + kActiveWindowGlobalRva),
                      &active_window_before);
    std::uintptr_t window_state_2d4_before = 0;
    const bool window_state_2d4_before_copied =
        should_log_before &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x2d4,
                      &window_state_2d4_before);
    std::uint32_t window_field_3d8_before = 0;
    const bool window_field_3d8_before_copied =
        should_log_before &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x3d8,
                      &window_field_3d8_before);
    std::uint8_t window_field_125_before = 0;
    const bool window_field_125_before_copied =
        should_log_before &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x125,
                      &window_field_125_before);
    std::array<std::uint8_t, 32> window_state_2d4_bytes_before = {};
    const bool window_state_2d4_bytes_before_copied =
        window_state_2d4_before_copied && window_state_2d4_before != 0 &&
        TryCopyBytes(reinterpret_cast<const void*>(window_state_2d4_before),
                     window_state_2d4_bytes_before.size(),
                     window_state_2d4_bytes_before.data());
    std::uint8_t window_state_byte_26_before = 0;
    const bool window_state_byte_26_before_copied =
        window_state_2d4_before_copied && window_state_2d4_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(window_state_2d4_before) + 0x26,
                      &window_state_byte_26_before);
    std::uint8_t window_state_byte_27_before = 0;
    const bool window_state_byte_27_before_copied =
        window_state_2d4_before_copied && window_state_2d4_before != 0 &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(window_state_2d4_before) + 0x27,
                      &window_state_byte_27_before);

    if (should_log_before) {
        std::uintptr_t this_vtable_before = 0;
        const bool this_vtable_before_copied = TryCopyObject(this_context, &this_vtable_before);
        std::uintptr_t sender_vtable_before = 0;
        const bool sender_vtable_before_copied =
            sender_window != nullptr && TryCopyObject(sender_window, &sender_vtable_before);
        std::array<std::uint8_t, 24> this_bytes_before = {};
        const bool this_bytes_before_copied =
            TryCopyBytes(this_context, this_bytes_before.size(), this_bytes_before.data());
        std::array<std::uint8_t, 24> sender_bytes_before = {};
        const bool sender_bytes_before_copied =
            sender_window != nullptr &&
            TryCopyBytes(sender_window, sender_bytes_before.size(), sender_bytes_before.data());
        LogInventoryWindowWndNotificationTrace(
            L"enter",
            this_context,
            caller_return_address,
            sender_window,
            notification_code,
            payload_like,
            0,
            false,
            this_vtable_before,
            this_vtable_before_copied,
            sender_vtable_before,
            sender_vtable_before_copied,
            this_bytes_before,
            this_bytes_before_copied,
            sender_bytes_before,
            sender_bytes_before_copied,
            drag_context_before,
            drag_context_before_copied,
            drag_context_bytes_before,
            drag_context_bytes_before_copied,
            drag_flags_before,
            drag_flags_before_copied,
            drag_flag_10_before,
            drag_flag_10_before_copied,
            drag_flag_11_before,
            drag_flag_11_before_copied,
            active_window_before,
            active_window_before_copied,
            window_state_2d4_before,
            window_state_2d4_before_copied,
            window_field_3d8_before,
            window_field_3d8_before_copied,
            window_field_125_before,
            window_field_125_before_copied,
            window_state_2d4_bytes_before,
            window_state_2d4_bytes_before_copied,
            window_state_byte_26_before,
            window_state_byte_26_before_copied,
            window_state_byte_27_before,
            window_state_byte_27_before_copied);
    }

    const int original_result = g_original_inventory_window_wnd_notification(
        this_context,
        sender_window,
        notification_code,
        payload_like);

    std::uintptr_t drag_context_after = 0;
    const bool drag_context_after_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_after);
    const bool should_log_after =
        should_log_before || (drag_context_after_copied && drag_context_after != 0);

    if (should_log_after) {
        std::array<std::uint8_t, 24> drag_context_bytes_after = {};
        const bool drag_context_bytes_after_copied =
            drag_context_after_copied && drag_context_after != 0 &&
            TryCopyBytes(reinterpret_cast<const void*>(drag_context_after),
                         drag_context_bytes_after.size(),
                         drag_context_bytes_after.data());
        std::uintptr_t drag_flags_after = 0;
        const bool drag_flags_after_copied =
            TryCopyObject(reinterpret_cast<const void*>(module_base + kDragFlagsGlobalRva),
                          &drag_flags_after);
        std::uint8_t drag_flag_10_after = 0;
        const bool drag_flag_10_after_copied =
            drag_flags_after_copied && drag_flags_after != 0 &&
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_after) + 0x10,
                          &drag_flag_10_after);
        std::uint8_t drag_flag_11_after = 0;
        const bool drag_flag_11_after_copied =
            drag_flags_after_copied && drag_flags_after != 0 &&
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(drag_flags_after) + 0x11,
                          &drag_flag_11_after);
        std::uintptr_t active_window_after = 0;
        const bool active_window_after_copied =
            TryCopyObject(reinterpret_cast<const void*>(module_base + kActiveWindowGlobalRva),
                          &active_window_after);
        std::uintptr_t window_state_2d4_after = 0;
        const bool window_state_2d4_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x2d4,
                          &window_state_2d4_after);
        std::uint32_t window_field_3d8_after = 0;
        const bool window_field_3d8_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x3d8,
                          &window_field_3d8_after);
        std::uint8_t window_field_125_after = 0;
        const bool window_field_125_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x125,
                          &window_field_125_after);
        std::array<std::uint8_t, 32> window_state_2d4_bytes_after = {};
        const bool window_state_2d4_bytes_after_copied =
            window_state_2d4_after_copied && window_state_2d4_after != 0 &&
            TryCopyBytes(reinterpret_cast<const void*>(window_state_2d4_after),
                         window_state_2d4_bytes_after.size(),
                         window_state_2d4_bytes_after.data());
        std::uint8_t window_state_byte_26_after = 0;
        const bool window_state_byte_26_after_copied =
            window_state_2d4_after_copied && window_state_2d4_after != 0 &&
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(window_state_2d4_after) + 0x26,
                          &window_state_byte_26_after);
        std::uint8_t window_state_byte_27_after = 0;
        const bool window_state_byte_27_after_copied =
            window_state_2d4_after_copied && window_state_2d4_after != 0 &&
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(window_state_2d4_after) + 0x27,
                          &window_state_byte_27_after);
        std::uintptr_t this_vtable_after = 0;
        const bool this_vtable_after_copied = TryCopyObject(this_context, &this_vtable_after);
        std::uintptr_t sender_vtable_after = 0;
        const bool sender_vtable_after_copied =
            sender_window != nullptr && TryCopyObject(sender_window, &sender_vtable_after);
        std::array<std::uint8_t, 24> this_bytes_after = {};
        const bool this_bytes_after_copied =
            TryCopyBytes(this_context, this_bytes_after.size(), this_bytes_after.data());
        std::array<std::uint8_t, 24> sender_bytes_after = {};
        const bool sender_bytes_after_copied =
            sender_window != nullptr &&
            TryCopyBytes(sender_window, sender_bytes_after.size(), sender_bytes_after.data());
        LogInventoryWindowWndNotificationTrace(
            L"exit",
            this_context,
            caller_return_address,
            sender_window,
            notification_code,
            payload_like,
            static_cast<std::uint32_t>(original_result),
            true,
            this_vtable_after,
            this_vtable_after_copied,
            sender_vtable_after,
            sender_vtable_after_copied,
            this_bytes_after,
            this_bytes_after_copied,
            sender_bytes_after,
            sender_bytes_after_copied,
            drag_context_after,
            drag_context_after_copied,
            drag_context_bytes_after,
            drag_context_bytes_after_copied,
            drag_flags_after,
            drag_flags_after_copied,
            drag_flag_10_after,
            drag_flag_10_after_copied,
            drag_flag_11_after,
            drag_flag_11_after_copied,
            active_window_after,
            active_window_after_copied,
            window_state_2d4_after,
            window_state_2d4_after_copied,
            window_field_3d8_after,
            window_field_3d8_after_copied,
            window_field_125_after,
            window_field_125_after_copied,
            window_state_2d4_bytes_after,
            window_state_2d4_bytes_after_copied,
            window_state_byte_26_after,
            window_state_byte_26_after_copied,
            window_state_byte_27_after,
            window_state_byte_27_after_copied);
    }

    return original_result;
}

void MONOMYTH_FASTCALL InventorySummaryRefreshCandidateAHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    if (g_multiclass_ui_display_enabled) {
        ArmInventoryClassDisplayCorrelationFromCandidate(
            L"InventorySummaryRefreshCandidateA",
            kInventorySummaryRefreshCandidateARva,
            caller_return_address,
            this_context,
            nullptr);
    }
    if (g_original_inventory_summary_refresh_candidate_a != nullptr) {
        g_original_inventory_summary_refresh_candidate_a(this_context);
    }
}

void MONOMYTH_FASTCALL InventorySummaryRefreshCandidateBHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    if (g_multiclass_ui_display_enabled) {
        ArmInventoryClassDisplayCorrelationFromCandidate(
            L"InventorySummaryRefreshCandidateB",
            kInventorySummaryRefreshCandidateBRva,
            caller_return_address,
            this_context,
            nullptr);
    }
    if (g_original_inventory_summary_refresh_candidate_b != nullptr) {
        g_original_inventory_summary_refresh_candidate_b(this_context);
    }
}

void MONOMYTH_FASTCALL InventorySummaryRefreshCandidateCHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    if (g_multiclass_ui_display_enabled) {
        ArmInventoryClassDisplayCorrelationFromCandidate(
            L"InventorySummaryRefreshCandidateC",
            kInventorySummaryRefreshCandidateCRva,
            caller_return_address,
            this_context,
            nullptr);
    }
    if (g_original_inventory_summary_refresh_candidate_c != nullptr) {
        g_original_inventory_summary_refresh_candidate_c(this_context);
    }
}

int MONOMYTH_FASTCALL InventorySummaryRefreshCandidateDHook(
    void* this_context,
    void*) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::uint32_t state_2f4 = 0;
    const bool state_2f4_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x2f4, &state_2f4);
    std::uint32_t last_refresh_tick_294 = 0;
    const bool last_refresh_tick_294_copied =
        this_context != nullptr &&
        TryCopyObject(
            reinterpret_cast<const std::uint8_t*>(this_context) + 0x294,
            &last_refresh_tick_294);
    std::uint32_t ui_tick_154 = 0;
    const std::uintptr_t module_base = GetHostModuleBase();
    const bool ui_tick_154_copied =
        module_base != 0 &&
        TryCopyObject(
            reinterpret_cast<const void*>(module_base + kDragFlagsGlobalRva + 0x154),
            &ui_tick_154);
    const bool refresh_ready =
        last_refresh_tick_294_copied &&
        ui_tick_154_copied &&
        ui_tick_154 > (last_refresh_tick_294 + 0xc8u);
    std::uintptr_t field_248 = 0;
    const bool field_248_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x248, &field_248);
    std::uintptr_t field_24c = 0;
    const bool field_24c_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x24c, &field_24c);
    std::uintptr_t field_250 = 0;
    const bool field_250_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x250, &field_250);
    std::uintptr_t field_254 = 0;
    const bool field_254_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x254, &field_254);
    std::uintptr_t field_25c = 0;
    const bool field_25c_copied =
        this_context != nullptr &&
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x25c, &field_25c);
    const bool summary_controls_present =
        (field_248_copied && field_248 != 0) ||
        (field_24c_copied && field_24c != 0) ||
        (field_250_copied && field_250 != 0) ||
        (field_254_copied && field_254 != 0) ||
        (field_25c_copied && field_25c != 0);
    const std::uint64_t count = ++g_inventory_on_process_frame_trace_count;
    if (refresh_ready && summary_controls_present && ShouldLogInventoryOnProcessFrameTrace(count)) {
        LogInventoryOnProcessFrameTrace(
            count,
            caller_return_address,
            this_context,
            state_2f4,
            state_2f4_copied,
            last_refresh_tick_294,
            last_refresh_tick_294_copied,
            ui_tick_154,
            ui_tick_154_copied,
            refresh_ready,
            field_248,
            field_248_copied,
            field_24c,
            field_24c_copied,
            field_250,
            field_250_copied,
            field_254,
            field_254_copied,
            field_25c,
            field_25c_copied);
    }
    if (refresh_ready && summary_controls_present && g_multiclass_ui_display_enabled) {
        ArmInventoryClassDisplayCorrelationFromCandidate(
            L"InventorySummaryRefreshCandidateD",
            kInventorySummaryRefreshCandidateDRva,
            caller_return_address,
            this_context,
            nullptr);
    }
    return g_original_inventory_summary_refresh_candidate_d == nullptr
        ? 0
        : g_original_inventory_summary_refresh_candidate_d(this_context);
}

void MONOMYTH_FASTCALL ItemDisplayClassRowBuilderHook(
    void* this_context,
    void*,
    void* arg1_like,
    void* arg2_like) noexcept {
    ArmItemDisplayClassDisplayCorrelation(
        GetCallerReturnAddress(),
        this_context,
        arg1_like,
        arg2_like);
    if (g_original_item_display_class_row_builder != nullptr) {
        g_original_item_display_class_row_builder(this_context, arg1_like, arg2_like);
    }
}

void MONOMYTH_FASTCALL ItemDisplayRefreshWorkerHook(
    void* this_context,
    void*,
    void* arg1_like,
    void* arg2_like) noexcept {
    LogItemDisplayRefreshWorkerEntryTrace(
        GetCallerReturnAddress(),
        this_context,
        arg1_like,
        arg2_like);
    if (g_original_item_display_refresh_worker_trace != nullptr) {
        g_original_item_display_refresh_worker_trace(this_context, arg1_like, arg2_like);
    }
}

int MONOMYTH_FASTCALL InvSlotWndHandleLButtonUpHook(
    void* this_context,
    void*,
    void* point_like,
    std::uint32_t flags_like) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::uintptr_t drag_context_pointer = 0;
    const bool drag_context_pointer_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_pointer);
    std::uint32_t field_29c_before = 0;
    const bool field_29c_before_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x29c,
                      &field_29c_before);
    const bool should_log =
        (drag_context_pointer_copied && drag_context_pointer != 0) ||
        (field_29c_before_copied && field_29c_before == 1);

    std::uintptr_t caller_return_address = 0;
    ClientPointWire point_value = {};
    bool point_value_copied = false;
    std::uintptr_t field_290_before = 0;
    bool field_290_before_copied = false;
    std::uintptr_t field_270_before = 0;
    bool field_270_before_copied = false;
    std::uint8_t field_28c_before = 0;
    bool field_28c_before_copied = false;
    std::array<std::uint8_t, 24> this_bytes_before = {};
    bool this_bytes_before_copied = false;
    std::array<std::uint8_t, 24> child_bytes_before = {};
    bool child_bytes_before_copied = false;
    if (should_log) {
        caller_return_address = GetCallerReturnAddress();
        point_value_copied = TryCopyObject(point_like, &point_value);
        field_290_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x290,
                          &field_290_before);
        field_270_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x270,
                          &field_270_before);
        field_28c_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x28c,
                          &field_28c_before);
        this_bytes_before_copied =
            TryCopyBytes(this_context, this_bytes_before.size(), this_bytes_before.data());
        child_bytes_before_copied =
            field_290_before_copied && field_290_before != 0 &&
            TryCopyBytes(reinterpret_cast<const void*>(field_290_before),
                         child_bytes_before.size(),
                         child_bytes_before.data());
    }

    const int original_result =
        g_original_invslot_wnd_handle_lbutton_up(this_context, point_like, flags_like);

    if (should_log) {
        std::uint32_t field_29c_after = 0;
        const bool field_29c_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x29c,
                          &field_29c_after);
        std::uintptr_t field_290_after = 0;
        const bool field_290_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x290,
                          &field_290_after);
        std::uintptr_t field_270_after = 0;
        const bool field_270_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x270,
                          &field_270_after);
        std::uint8_t field_28c_after = 0;
        const bool field_28c_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x28c,
                          &field_28c_after);
        LogInvSlotWndHandleLButtonTrace(
            L"InvSlotWndHandleLButtonUp",
            kInvSlotWndHandleLButtonUpTargetRva,
            this_context,
            caller_return_address,
            point_like,
            point_value,
            point_value_copied,
            flags_like,
            static_cast<std::uint32_t>(original_result),
            field_29c_before,
            field_29c_before_copied,
            field_29c_after,
            field_29c_after_copied,
            field_290_before,
            field_290_before_copied,
            field_290_after,
            field_290_after_copied,
            field_270_before,
            field_270_before_copied,
            field_270_after,
            field_270_after_copied,
            field_28c_before,
            field_28c_before_copied,
            field_28c_after,
            field_28c_after_copied,
            this_bytes_before,
            this_bytes_before_copied,
            child_bytes_before,
            child_bytes_before_copied,
            drag_context_pointer,
            drag_context_pointer_copied);
    }

    return original_result;
}

int MONOMYTH_FASTCALL InvSlotWndHandleLButtonUpAfterHeldHook(
    void* this_context,
    void*,
    void* point_like,
    std::uint32_t flags_like) noexcept {
    const std::uintptr_t module_base = GetHostModuleBase();
    std::uintptr_t drag_context_pointer = 0;
    const bool drag_context_pointer_copied =
        TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                      &drag_context_pointer);
    std::uint32_t field_29c_before = 0;
    const bool field_29c_before_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x29c,
                      &field_29c_before);
    const bool should_log =
        (drag_context_pointer_copied && drag_context_pointer != 0) ||
        (field_29c_before_copied && field_29c_before == 1);

    std::uintptr_t caller_return_address = 0;
    ClientPointWire point_value = {};
    bool point_value_copied = false;
    std::uintptr_t field_290_before = 0;
    bool field_290_before_copied = false;
    std::uintptr_t field_270_before = 0;
    bool field_270_before_copied = false;
    std::uint8_t field_28c_before = 0;
    bool field_28c_before_copied = false;
    std::array<std::uint8_t, 24> this_bytes_before = {};
    bool this_bytes_before_copied = false;
    std::array<std::uint8_t, 24> child_bytes_before = {};
    bool child_bytes_before_copied = false;
    if (should_log) {
        caller_return_address = GetCallerReturnAddress();
        point_value_copied = TryCopyObject(point_like, &point_value);
        field_290_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x290,
                          &field_290_before);
        field_270_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x270,
                          &field_270_before);
        field_28c_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x28c,
                          &field_28c_before);
        this_bytes_before_copied =
            TryCopyBytes(this_context, this_bytes_before.size(), this_bytes_before.data());
        child_bytes_before_copied =
            field_290_before_copied && field_290_before != 0 &&
            TryCopyBytes(reinterpret_cast<const void*>(field_290_before),
                         child_bytes_before.size(),
                         child_bytes_before.data());
    }

    const int original_result =
        g_original_invslot_wnd_handle_lbutton_up_afterheld(this_context, point_like, flags_like);

    if (should_log) {
        std::uint32_t field_29c_after = 0;
        const bool field_29c_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x29c,
                          &field_29c_after);
        std::uintptr_t field_290_after = 0;
        const bool field_290_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x290,
                          &field_290_after);
        std::uintptr_t field_270_after = 0;
        const bool field_270_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x270,
                          &field_270_after);
        std::uint8_t field_28c_after = 0;
        const bool field_28c_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x28c,
                          &field_28c_after);
        LogInvSlotWndHandleLButtonTrace(
            L"InvSlotWndHandleLButtonUpAfterHeld",
            kInvSlotWndHandleLButtonUpAfterHeldTargetRva,
            this_context,
            caller_return_address,
            point_like,
            point_value,
            point_value_copied,
            flags_like,
            static_cast<std::uint32_t>(original_result),
            field_29c_before,
            field_29c_before_copied,
            field_29c_after,
            field_29c_after_copied,
            field_290_before,
            field_290_before_copied,
            field_290_after,
            field_290_after_copied,
            field_270_before,
            field_270_before_copied,
            field_270_after,
            field_270_after_copied,
            field_28c_before,
            field_28c_before_copied,
            field_28c_after,
            field_28c_after_copied,
            this_bytes_before,
            this_bytes_before_copied,
            child_bytes_before,
            child_bytes_before_copied,
            drag_context_pointer,
            drag_context_pointer_copied);
    }

    return original_result;
}

void MONOMYTH_FASTCALL InvSlotHandleLButtonCoreHook(
    void* this_context,
    void*,
    void* point_like,
    std::uint32_t slot_like,
    std::uint32_t held_flag_like) noexcept {
    g_invslot_handle_lbutton_core_last_late_lookup_item_pointer = 0;
    const std::uintptr_t module_base = GetHostModuleBase();
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::uintptr_t drag_context_pointer = 0;
    bool drag_context_pointer_copied = false;
    if (module_base != 0) {
        drag_context_pointer_copied =
            TryCopyObject(reinterpret_cast<const void*>(module_base + kDragContextGlobalRva),
                          &drag_context_pointer);
    }
    const bool core_callsite_matched =
        module_base != 0 &&
        caller_return_address == module_base + kInvSlotHandleLButtonCoreCallerReturnRva;
    const bool active_drag_context =
        drag_context_pointer_copied && drag_context_pointer != 0;
    const bool should_log =
        core_callsite_matched &&
        (!g_multiclass_item_usability_enabled ||
         active_drag_context ||
         held_flag_like != 0);

    ClientPointWire point_value = {};
    bool point_value_copied = false;
    std::uint8_t field_10_before = 0;
    bool field_10_before_copied = false;
    std::uintptr_t field_04_before = 0;
    bool field_04_before_copied = false;
    std::uint32_t slot_word_264_before = 0;
    bool slot_word_264_before_copied = false;
    std::uint32_t slot_word_268_before = 0;
    bool slot_word_268_before_copied = false;
    std::uint32_t slot_word_26c_before = 0;
    bool slot_word_26c_before_copied = false;
    std::array<std::uint8_t, 24> this_bytes_before = {};
    bool this_bytes_before_copied = false;
    std::array<std::uint8_t, 24> field_04_bytes_before = {};
    bool field_04_bytes_before_copied = false;
    if (should_log) {
        point_value_copied = TryCopyObject(point_like, &point_value);
        field_10_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x10,
                          &field_10_before);
        field_04_before_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x4,
                          &field_04_before);
        if (field_04_before_copied && field_04_before != 0) {
            slot_word_264_before_copied =
                TryCopyObject(reinterpret_cast<const std::uint8_t*>(field_04_before) + 0x264,
                              &slot_word_264_before);
            slot_word_268_before_copied =
                TryCopyObject(reinterpret_cast<const std::uint8_t*>(field_04_before) + 0x268,
                              &slot_word_268_before);
            slot_word_26c_before_copied =
                TryCopyObject(reinterpret_cast<const std::uint8_t*>(field_04_before) + 0x26c,
                              &slot_word_26c_before);
            field_04_bytes_before_copied =
                TryCopyBytes(reinterpret_cast<const void*>(field_04_before),
                             field_04_bytes_before.size(),
                             field_04_bytes_before.data());
        }
        this_bytes_before_copied =
            TryCopyBytes(this_context, this_bytes_before.size(), this_bytes_before.data());
    }

    g_original_invslot_handle_lbutton_core(
        this_context, point_like, slot_like, held_flag_like);

    if (should_log) {
        std::uint8_t field_10_after = 0;
        const bool field_10_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x10,
                          &field_10_after);
        std::uintptr_t field_04_after = 0;
        const bool field_04_after_copied =
            TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x4,
                          &field_04_after);
        std::uint32_t slot_word_264_after = 0;
        bool slot_word_264_after_copied = false;
        std::uint32_t slot_word_268_after = 0;
        bool slot_word_268_after_copied = false;
        std::uint32_t slot_word_26c_after = 0;
        bool slot_word_26c_after_copied = false;
        if (field_04_after_copied && field_04_after != 0) {
            slot_word_264_after_copied =
                TryCopyObject(reinterpret_cast<const std::uint8_t*>(field_04_after) + 0x264,
                              &slot_word_264_after);
            slot_word_268_after_copied =
                TryCopyObject(reinterpret_cast<const std::uint8_t*>(field_04_after) + 0x268,
                              &slot_word_268_after);
            slot_word_26c_after_copied =
                TryCopyObject(reinterpret_cast<const std::uint8_t*>(field_04_after) + 0x26c,
                              &slot_word_26c_after);
        }
        LogInvSlotHandleLButtonCoreTrace(
            caller_return_address,
            this_context,
            point_like,
            point_value,
            point_value_copied,
            slot_like,
            held_flag_like,
            field_10_before,
            field_10_before_copied,
            field_10_after,
            field_10_after_copied,
            field_04_before,
            field_04_before_copied,
            field_04_after,
            field_04_after_copied,
            slot_word_264_before,
            slot_word_264_before_copied,
            slot_word_264_after,
            slot_word_264_after_copied,
            slot_word_268_before,
            slot_word_268_before_copied,
            slot_word_268_after,
            slot_word_268_after_copied,
            slot_word_26c_before,
            slot_word_26c_before_copied,
            slot_word_26c_after,
            slot_word_26c_after_copied,
            this_bytes_before,
            this_bytes_before_copied,
            field_04_bytes_before,
            field_04_bytes_before_copied,
            drag_context_pointer,
            drag_context_pointer_copied);
    }
}

void MONOMYTH_FASTCALL MoveItemOwnerPrimarySetterHook(
    void* this_context,
    void*,
    void* primary_object_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    std::array<std::uint8_t, 24> context_bytes_before = {};
    const bool context_bytes_before_copied =
        TryCopyBytes(this_context, context_bytes_before.size(), context_bytes_before.data());
    std::uintptr_t owner_field_98_before = 0;
    const bool owner_field_98_before_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x98,
                      &owner_field_98_before);
    std::uintptr_t primary_pointer_9c_before = 0;
    const bool primary_pointer_9c_before_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x9c,
                      &primary_pointer_9c_before);
    std::uint32_t owner_field_a0_before = 0;
    const bool owner_field_a0_before_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0xa0,
                      &owner_field_a0_before);
    std::uintptr_t fallback_pointer_144_before = 0;
    const bool fallback_pointer_144_before_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x144,
                      &fallback_pointer_144_before);

    std::uintptr_t vtable_address = 0;
    const bool vtable_copied = TryCopyObject(this_context, &vtable_address);
    std::uintptr_t resolver_address = 0;
    const bool resolver_copied =
        vtable_copied && vtable_address != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(vtable_address + 0x8), &resolver_address);
    std::uintptr_t resolved_object_before = 0;
    if (resolver_copied && resolver_address != 0) {
        resolved_object_before = reinterpret_cast<std::uintptr_t>(
            reinterpret_cast<MoveItemBranchResolvedObjectFn>(resolver_address)(this_context));
    }

    g_original_move_item_owner_primary_setter(this_context, primary_object_like);

    std::array<std::uint8_t, 24> context_bytes_after = {};
    const bool context_bytes_after_copied =
        TryCopyBytes(this_context, context_bytes_after.size(), context_bytes_after.data());
    std::uintptr_t owner_field_98_after = 0;
    const bool owner_field_98_after_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x98,
                      &owner_field_98_after);
    std::uintptr_t primary_pointer_9c_after = 0;
    const bool primary_pointer_9c_after_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x9c,
                      &primary_pointer_9c_after);
    std::uint32_t owner_field_a0_after = 0;
    const bool owner_field_a0_after_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0xa0,
                      &owner_field_a0_after);
    std::uintptr_t fallback_pointer_144_after = 0;
    const bool fallback_pointer_144_after_copied =
        TryCopyObject(reinterpret_cast<const std::uint8_t*>(this_context) + 0x144,
                      &fallback_pointer_144_after);

    std::uintptr_t resolved_object_after = 0;
    if (resolver_copied && resolver_address != 0) {
        resolved_object_after = reinterpret_cast<std::uintptr_t>(
            reinterpret_cast<MoveItemBranchResolvedObjectFn>(resolver_address)(this_context));
    }
    std::uint8_t resolved_flag_11a_after = 0;
    const bool resolved_flag_11a_after_copied =
        resolved_object_after != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_after + 0x11a),
                      &resolved_flag_11a_after);
    std::uint8_t resolved_field_521_after = 0;
    const bool resolved_field_521_after_copied =
        resolved_object_after != 0 &&
        TryCopyObject(reinterpret_cast<const void*>(resolved_object_after + 0x521),
                      &resolved_field_521_after);

    LogMoveItemOwnerPrimarySetterTrace(
        this_context,
        caller_return_address,
        primary_object_like,
        owner_field_98_before_copied ? owner_field_98_before : 0,
        owner_field_98_before_copied,
        owner_field_98_after_copied ? owner_field_98_after : 0,
        owner_field_98_after_copied,
        primary_pointer_9c_before_copied ? primary_pointer_9c_before : 0,
        primary_pointer_9c_before_copied,
        primary_pointer_9c_after_copied ? primary_pointer_9c_after : 0,
        primary_pointer_9c_after_copied,
        owner_field_a0_before,
        owner_field_a0_before_copied,
        owner_field_a0_after,
        owner_field_a0_after_copied,
        fallback_pointer_144_before_copied ? fallback_pointer_144_before : 0,
        fallback_pointer_144_before_copied,
        fallback_pointer_144_after_copied ? fallback_pointer_144_after : 0,
        fallback_pointer_144_after_copied,
        resolved_object_before,
        resolved_object_after,
        resolved_flag_11a_after,
        resolved_flag_11a_after_copied,
        resolved_field_521_after,
        resolved_field_521_after_copied,
        context_bytes_before,
        context_bytes_before_copied,
        context_bytes_after,
        context_bytes_after_copied);
}

void* MONOMYTH_FASTCALL MoveItemCtorSiteACallsiteHook(
    void* this_context,
    void*,
    void* slot_like,
    std::uint32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const auto* primary_slot_bytes = reinterpret_cast<const std::uint8_t*>(slot_like);
    const void* paired_slot_like =
        primary_slot_bytes == nullptr ? nullptr : primary_slot_bytes + sizeof(ClientInventorySlotWire);

    ClientInventorySlotWire primary_before = {};
    const bool primary_before_copied = TryCopyObject(slot_like, &primary_before);
    ClientInventorySlotWire paired_before = {};
    const bool paired_before_copied = TryCopyObject(paired_slot_like, &paired_before);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire) * 2> pair_bytes_before = {};
    const bool pair_bytes_before_copied = TryCopyBytes(
        slot_like,
        pair_bytes_before.size(),
        pair_bytes_before.data());

    void* const original_result =
        g_original_move_item_ctor(this_context, slot_like, arg2, arg3, arg4);

    ClientInventorySlotWire primary_after = {};
    const bool primary_after_copied = TryCopyObject(slot_like, &primary_after);
    ClientInventorySlotWire paired_after = {};
    const bool paired_after_copied = TryCopyObject(paired_slot_like, &paired_after);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire) * 2> pair_bytes_after = {};
    const bool pair_bytes_after_copied = TryCopyBytes(
        slot_like,
        pair_bytes_after.size(),
        pair_bytes_after.data());

    LogMoveItemCtorTrace(
        L"MoveItemCtorSiteA",
        kMoveItemCtorSiteACallsiteRva,
        caller_return_address,
        this_context,
        slot_like,
        arg2,
        arg3,
        arg4,
        original_result,
        primary_before,
        primary_before_copied,
        paired_before,
        paired_before_copied,
        primary_after,
        primary_after_copied,
        paired_after,
        paired_after_copied,
        pair_bytes_before,
        pair_bytes_before_copied,
        pair_bytes_after,
        pair_bytes_after_copied,
        0,
        false);
    return original_result;
}

void* MONOMYTH_FASTCALL MoveItemCtorSiteBCallsiteHook(
    void* this_context,
    void*,
    void* slot_like,
    std::uint32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const auto* primary_slot_bytes = reinterpret_cast<const std::uint8_t*>(slot_like);
    const void* paired_slot_like =
        primary_slot_bytes == nullptr ? nullptr : primary_slot_bytes + sizeof(ClientInventorySlotWire);
    const void* future_gate_arg5_pointer =
        primary_slot_bytes == nullptr ? nullptr : primary_slot_bytes + 0x24;

    ClientInventorySlotWire primary_before = {};
    const bool primary_before_copied = TryCopyObject(slot_like, &primary_before);
    ClientInventorySlotWire paired_before = {};
    const bool paired_before_copied = TryCopyObject(paired_slot_like, &paired_before);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire) * 2> pair_bytes_before = {};
    const bool pair_bytes_before_copied = TryCopyBytes(
        slot_like,
        pair_bytes_before.size(),
        pair_bytes_before.data());
    std::uint32_t future_gate_arg5_value = 0;
    const bool future_gate_arg5_copied =
        TryCopyObject(future_gate_arg5_pointer, &future_gate_arg5_value);

    void* const original_result =
        g_original_move_item_ctor(this_context, slot_like, arg2, arg3, arg4);

    ClientInventorySlotWire primary_after = {};
    const bool primary_after_copied = TryCopyObject(slot_like, &primary_after);
    ClientInventorySlotWire paired_after = {};
    const bool paired_after_copied = TryCopyObject(paired_slot_like, &paired_after);
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire) * 2> pair_bytes_after = {};
    const bool pair_bytes_after_copied = TryCopyBytes(
        slot_like,
        pair_bytes_after.size(),
        pair_bytes_after.data());

    LogMoveItemCtorTrace(
        L"MoveItemCtorSiteB",
        kMoveItemCtorSiteBCallsiteRva,
        caller_return_address,
        this_context,
        slot_like,
        arg2,
        arg3,
        arg4,
        original_result,
        primary_before,
        primary_before_copied,
        paired_before,
        paired_before_copied,
        primary_after,
        primary_after_copied,
        paired_after,
        paired_after_copied,
        pair_bytes_before,
        pair_bytes_before_copied,
        pair_bytes_after,
        pair_bytes_after_copied,
        future_gate_arg5_value,
        future_gate_arg5_copied);
    return original_result;
}

void* MONOMYTH_FASTCALL MoveItemSlot21LookupHook(
    void* this_context,
    void*,
    void* output_like,
    std::uint32_t slot_like) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();
    const bool capture_invslot_late_lookup =
        module_base != 0 &&
        (caller_return_address ==
             module_base + kInvSlotHandleLButtonCoreSlot21LookupCallerReturnARva ||
         caller_return_address ==
             module_base + kInvSlotHandleLButtonCoreSlot21LookupCallerReturnBRva);
    const bool trace_target =
        !g_multiclass_item_usability_enabled &&
        (caller_return_address == module_base + kMoveItemSlot21LookupCallerReturnRva ||
         caller_return_address ==
             module_base + kInventoryWndNotificationSlot21LookupCallerReturnARva ||
         caller_return_address ==
             module_base + kInventoryWndNotificationSlot21LookupCallerReturnBRva ||
         caller_return_address ==
             module_base + kInvSlotHandleLButtonCoreSlot21LookupCallerReturnARva ||
         caller_return_address ==
             module_base + kInvSlotHandleLButtonCoreSlot21LookupCallerReturnBRva);

    std::array<std::uint8_t, 24> context_bytes_before = {};
    bool context_bytes_before_copied = false;
    std::array<std::uint8_t, 24> output_bytes_before = {};
    bool output_bytes_before_copied = false;
    std::uint32_t output_dword0_before = 0;
    bool output_dword0_before_copied = false;

    if (trace_target) {
        context_bytes_before_copied =
            TryCopyBytes(this_context, context_bytes_before.size(), context_bytes_before.data());
        output_bytes_before_copied =
            TryCopyBytes(output_like, output_bytes_before.size(), output_bytes_before.data());
        output_dword0_before_copied = TryCopyObject(output_like, &output_dword0_before);
    }

    void* const original_result =
        g_original_move_item_slot21_lookup(this_context, output_like, slot_like);

    if (capture_invslot_late_lookup) {
        std::uint32_t output_dword0_after = 0;
        const bool output_dword0_after_copied = TryCopyObject(output_like, &output_dword0_after);
        g_invslot_handle_lbutton_core_last_late_lookup_item_pointer =
            output_dword0_after_copied
                ? static_cast<std::uintptr_t>(output_dword0_after)
                : 0;
    }

    if (trace_target) {
        std::array<std::uint8_t, 24> context_bytes_after = {};
        const bool context_bytes_after_copied =
            TryCopyBytes(this_context, context_bytes_after.size(), context_bytes_after.data());
        std::array<std::uint8_t, 24> output_bytes_after = {};
        const bool output_bytes_after_copied =
            TryCopyBytes(output_like, output_bytes_after.size(), output_bytes_after.data());
        std::uint32_t output_dword0_after = 0;
        const bool output_dword0_after_copied = TryCopyObject(output_like, &output_dword0_after);
        const wchar_t* caller_label = L"Other";
        if (caller_return_address == module_base + kMoveItemSlot21LookupCallerReturnRva) {
            caller_label = L"MoveItemSharedBlock";
        } else if (
            caller_return_address ==
            module_base + kInventoryWndNotificationSlot21LookupCallerReturnARva) {
            caller_label = L"InventoryWndNotificationSiteA";
        } else if (
            caller_return_address ==
            module_base + kInventoryWndNotificationSlot21LookupCallerReturnBRva) {
            caller_label = L"InventoryWndNotificationSiteB";
        } else if (
            caller_return_address ==
            module_base + kInvSlotHandleLButtonCoreSlot21LookupCallerReturnARva) {
            caller_label = L"InvSlotHandleLButtonCoreLateLookupA";
        } else if (
            caller_return_address ==
            module_base + kInvSlotHandleLButtonCoreSlot21LookupCallerReturnBRva) {
            caller_label = L"InvSlotHandleLButtonCoreLateLookupB";
        }
        LogMoveItemSlot21LookupTrace(
            caller_label,
            caller_return_address,
            this_context,
            output_like,
            slot_like,
            original_result,
            context_bytes_before,
            context_bytes_before_copied,
            context_bytes_after,
            context_bytes_after_copied,
            output_bytes_before,
            output_bytes_before_copied,
            output_bytes_after,
            output_bytes_after_copied,
            output_dword0_before,
            output_dword0_before_copied,
            output_dword0_after,
            output_dword0_after_copied);
    }

    return original_result;
}

void* MONOMYTH_FASTCALL MoveItemDescriptorBuildHook(
    void* this_context,
    void*,
    void* output_words,
    std::int32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();
    const bool should_log =
        module_base != 0 &&
        caller_return_address == module_base + kMoveItemDescriptorBuildCallerReturnRva;

    std::uint8_t prefix_word_count = 0;
    bool prefix_word_count_copied = false;
    std::array<std::uint8_t, 6> prefix_words_before = {};
    bool prefix_words_before_copied = false;
    std::array<std::uint8_t, 6> output_words_before = {};
    bool output_words_before_copied = false;
    if (should_log) {
        const auto* context_bytes = reinterpret_cast<const std::uint8_t*>(this_context);
        prefix_word_count_copied = TryCopyObject(context_bytes + 0x14, &prefix_word_count);
        prefix_words_before_copied = TryCopyBytes(
            context_bytes + 0x16,
            prefix_words_before.size(),
            prefix_words_before.data());
        output_words_before_copied = TryCopyBytes(
            output_words,
            output_words_before.size(),
            output_words_before.data());
    }

    void* const original_result =
        g_original_move_item_descriptor_build(
            this_context,
            output_words,
            arg2,
            arg3,
            arg4);

    if (should_log) {
        std::array<std::uint8_t, 6> output_words_after = {};
        const bool output_words_after_copied = TryCopyBytes(
            output_words,
            output_words_after.size(),
            output_words_after.data());
        LogMoveItemDescriptorBuildTrace(
            this_context,
            caller_return_address,
            output_words,
            arg2,
            arg3,
            arg4,
            original_result,
            prefix_word_count,
            prefix_word_count_copied,
            prefix_words_before,
            prefix_words_before_copied,
            output_words_before,
            output_words_before_copied,
            output_words_after,
            output_words_after_copied);
    }

    return original_result;
}

void* MONOMYTH_FASTCALL MoveItemSlotPopulateHook(
    void* this_context,
    void*,
    void* slot_like,
    std::int32_t arg2,
    std::int32_t arg3,
    std::int32_t arg4,
    std::int32_t arg5) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const std::uintptr_t module_base = GetHostModuleBase();
    const bool should_log =
        module_base != 0 &&
        caller_return_address == module_base + kMoveItemSlotPopulateCallerReturnRva;

    std::uint8_t prefix_word_count = 0;
    bool prefix_word_count_copied = false;
    std::array<std::uint8_t, 6> prefix_words_before = {};
    bool prefix_words_before_copied = false;
    ClientInventorySlotWire slot_before = {};
    bool slot_before_copied = false;
    std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_bytes_before = {};
    bool slot_bytes_before_copied = false;
    if (should_log) {
        const auto* context_bytes = reinterpret_cast<const std::uint8_t*>(this_context);
        prefix_word_count_copied = TryCopyObject(context_bytes + 0x14, &prefix_word_count);
        prefix_words_before_copied = TryCopyBytes(
            context_bytes + 0x16,
            prefix_words_before.size(),
            prefix_words_before.data());
        slot_before_copied = TryCopyObject(slot_like, &slot_before);
        slot_bytes_before_copied = TryCopyBytes(
            slot_like,
            slot_bytes_before.size(),
            slot_bytes_before.data());
    }

    void* const original_result =
        g_original_move_item_slot_populate(
            this_context,
            slot_like,
            arg2,
            arg3,
            arg4,
            arg5);

    if (should_log) {
        ClientInventorySlotWire slot_after = {};
        const bool slot_after_copied = TryCopyObject(slot_like, &slot_after);
        std::array<std::uint8_t, sizeof(ClientInventorySlotWire)> slot_bytes_after = {};
        const bool slot_bytes_after_copied = TryCopyBytes(
            slot_like,
            slot_bytes_after.size(),
            slot_bytes_after.data());
        LogMoveItemSlotPopulateTrace(
            this_context,
            caller_return_address,
            slot_like,
            arg2,
            arg3,
            arg4,
            arg5,
            original_result,
            prefix_word_count,
            prefix_word_count_copied,
            prefix_words_before,
            prefix_words_before_copied,
            slot_before,
            slot_before_copied,
            slot_after,
            slot_after_copied,
            slot_bytes_before,
            slot_bytes_before_copied,
            slot_bytes_after,
            slot_bytes_after_copied);
    }

    return original_result;
}

bool MONOMYTH_FASTCALL MoveItemValidationGateHook(
    void* this_context,
    void*,
    void* move_data_a,
    void* move_data_b,
    std::uint32_t arg3,
    std::uint32_t arg4,
    std::uint32_t arg5,
    std::uint32_t arg6) noexcept {
    const std::uintptr_t caller_return_address = GetCallerReturnAddress();
    const bool original_result =
        g_original_move_item_validation_gate(
            this_context,
            move_data_a,
            move_data_b,
            arg3,
            arg4,
            arg5,
            arg6);
    if (!g_multiclass_item_usability_enabled || !original_result) {
        LogMoveItemValidationGateTrace(
            this_context,
            caller_return_address,
            move_data_a,
            move_data_b,
            arg3,
            arg4,
            arg5,
            arg6,
            original_result);
    }
    return original_result;
}

bool InstallWhoClassNameDisplayHook(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.multiclass_ui_display_allowed) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        monomyth::logger::Log(
            L"hook_manager: multiclass UI display hook denied because module base was unavailable");
        return false;
    }

    bool installed = false;
    if (manifest.who_class_name_state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        manifest.who_class_name_address != 0) {
        if (manifest.who_class_name_address != module_base + kLeftClickedOnPlayerSurrogateRva) {
            std::wstring message =
                L"hook_manager: LeftClickedOnPlayerSurrogate validation address drift expected=";
            message += HexPtr(module_base + kLeftClickedOnPlayerSurrogateRva);
            message += L" actual=";
            message += HexPtr(manifest.who_class_name_address);
            monomyth::logger::Log(message);
        }

        if (!InstallInlineDetour(
                reinterpret_cast<void*>(manifest.who_class_name_address),
                reinterpret_cast<void*>(&WhoClassNameHook),
                &g_who_class_name_detour,
                reinterpret_cast<void**>(&g_original_who_class_name),
                L"LeftClickedOnPlayerSurrogate")) {
            goto cleanup;
        }
        {
            std::wstring message =
                L"hook_manager: LeftClickedOnPlayerSurrogate inline detour installed address=";
            message += HexPtr(manifest.who_class_name_address);
            message += L" hook=";
            message += HexPtr(reinterpret_cast<std::uintptr_t>(&WhoClassNameHook));
            monomyth::logger::Log(message);
        }
        installed = true;

        if (!InstallInlineDetour(
                reinterpret_cast<void*>(module_base + kWhoClassNameClassLookupTargetRva),
                reinterpret_cast<void*>(&WhoClassNameClassLookupHook),
                &g_who_class_name_class_lookup_detour,
                reinterpret_cast<void**>(&g_original_who_class_name_class_lookup),
                L"WhoClassNameClassLookup")) {
            goto cleanup;
        }
        {
            std::wstring message =
                L"hook_manager: WhoClassNameClassLookup inline detour installed address=";
            message += HexPtr(module_base + kWhoClassNameClassLookupTargetRva);
            message += L" hook=";
            message += HexPtr(reinterpret_cast<std::uintptr_t>(&WhoClassNameClassLookupHook));
            monomyth::logger::Log(message);
        }
        installed = true;
    } else {
        std::wstring message = L"hook_manager: multiclass UI display hook denied ";
        message += FormatDiscoveryDetails(
            L"LeftClickedOnPlayerSurrogate",
            manifest.who_class_name_evidence_source,
            manifest.who_class_name_failure_reason);
        monomyth::logger::Log(message);
    }

    if (manifest.get_class_desc_state != monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.get_class_desc_address == 0) {
        std::wstring message = L"hook_manager: multiclass UI display hook denied ";
        message += FormatDiscoveryDetails(
            L"GetClassDesc",
            manifest.get_class_desc_evidence_source,
            manifest.get_class_desc_failure_reason);
        if (!installed) {
            monomyth::logger::Log(message);
            return false;
        }
        monomyth::logger::Log(message);
        goto success;
    }

    if (manifest.get_class_three_letter_code_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.get_class_three_letter_code_address == 0) {
        std::wstring message = L"hook_manager: multiclass UI display hook denied ";
        message += FormatDiscoveryDetails(
            L"GetClassThreeLetterCode",
            manifest.get_class_three_letter_code_evidence_source,
            manifest.get_class_three_letter_code_failure_reason);
        if (!installed) {
            monomyth::logger::Log(message);
            return false;
        }
        monomyth::logger::Log(message);
        goto success;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.get_class_desc_address),
            reinterpret_cast<void*>(&GetClassDescHook),
            &g_get_class_desc_detour,
            reinterpret_cast<void**>(&g_original_get_class_desc),
            L"GetClassDesc")) {
        if (!installed) {
            goto cleanup;
        }
        goto success;
    }
    installed = true;

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.get_class_three_letter_code_address),
            reinterpret_cast<void*>(&GetClassThreeLetterCodeHook),
            &g_get_class_three_letter_code_detour,
            reinterpret_cast<void**>(&g_original_get_class_three_letter_code),
            L"GetClassThreeLetterCode")) {
        if (!installed) {
            goto cleanup;
        }
        goto success;
    }
    installed = true;

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(module_base + kGetDeityDescRva),
            reinterpret_cast<void*>(&GetDeityDescHook),
            &g_get_deity_desc_detour,
            reinterpret_cast<void**>(&g_original_get_deity_desc),
            L"GetDeityDesc")) {
        monomyth::logger::Log(
            L"hook_manager: inventory deity trace denied target=GetDeityDesc install_failed");
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(module_base + kCXWndSetWindowTextATargetRva),
            reinterpret_cast<void*>(&CXWndSetWindowTextAHook),
            &g_cxwnd_set_window_text_a_detour,
            reinterpret_cast<void**>(&g_original_cxwnd_set_window_text_a),
            L"CXWnd::SetWindowTextA")) {
        monomyth::logger::Log(
            L"hook_manager: char select text substitution denied target=CXWnd::SetWindowTextA install_failed");
    }

success:
    g_get_class_desc_seen_caller_rvas = {};
    g_get_class_desc_seen_caller_rva_count = 0;
    g_get_class_three_letter_seen_caller_rvas = {};
    g_get_class_three_letter_seen_caller_rva_count = 0;
    g_ui_class_producer_candidate_seen_caller_rvas = {};
    g_ui_class_producer_candidate_seen_caller_rva_count = 0;
    g_inventory_class_display_correlation_remaining.store(0);
    g_inventory_class_display_correlation_activation.store(0);
    g_inventory_class_display_correlation_notification_code.store(0);
    g_inventory_class_display_correlation_notification_caller_return.store(0);
    g_inventory_class_display_correlation_notification_caller_rva.store(0);
    g_inventory_class_display_correlation_sender_window.store(0);
    g_inventory_class_display_correlation_payload_like.store(0);
    g_char_select_native_class_substitution_remaining.store(0);
    g_char_select_native_class_substitution_activation.store(0);
    g_char_select_native_class_substitution_trace_count = 0;
    monomyth::logger::Log(
        L"hook_manager: multiclass UI display hook installed target=LeftClickedOnPlayerSurrogate/GetClassDesc/GetClassThreeLetterCode local_self_only=true");
    return true;

cleanup:
    g_active_who_class_name_subject = nullptr;
    RemoveInlineDetour(&g_cxwnd_set_window_text_a_detour);
    RemoveInlineDetour(&g_get_deity_desc_detour);
    RemoveInlineDetour(&g_who_class_name_class_lookup_detour);
    RemoveInlineDetour(&g_who_class_name_detour);
    RemoveInlineDetour(&g_get_class_three_letter_code_detour);
    RemoveInlineDetour(&g_get_class_desc_detour);
    g_original_cxwnd_set_window_text_a = nullptr;
    g_original_get_deity_desc = nullptr;
    g_original_get_class_desc = nullptr;
    g_original_get_class_three_letter_code = nullptr;
    g_original_who_class_name = nullptr;
    g_original_who_class_name_class_lookup = nullptr;
    return false;
}

bool InstallCharacterListUpdateListHook() noexcept {
    if (!g_multiclass_ui_display_enabled) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        monomyth::logger::Log(
            L"hook_manager: character list refresh hook denied because module base was unavailable");
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(module_base + kCharacterListWndUpdateListRva),
            reinterpret_cast<void*>(&CharacterListWndUpdateListHook),
            &g_character_list_update_list_detour,
            reinterpret_cast<void**>(&g_original_character_list_update_list),
            L"CCharacterListWnd::UpdateList")) {
        RemoveInlineDetour(&g_character_list_update_list_detour);
        g_original_character_list_update_list = nullptr;
        monomyth::logger::Log(
            L"hook_manager: character list refresh hook install failed target=CCharacterListWnd::UpdateList");
        return false;
    }

    g_character_list_window_pointer.store(0);
    g_character_list_last_forced_refresh_window.store(0);
    g_character_list_last_forced_refresh_mask.store(0);
    g_multiclass_cache_persist_pending.store(false);
    g_multiclass_cache_persist_pending_mask.store(0);
    g_character_list_update_list_trace_count = 0;
    g_character_list_forced_refresh_count = 0;
    std::wstring message =
        L"hook_manager: character list refresh hook installed target=CCharacterListWnd::UpdateList address=";
    message += HexPtr(module_base + kCharacterListWndUpdateListRva);
    message += L" target_rva=";
    message += Hex32(kCharacterListWndUpdateListRva);
    monomyth::logger::Log(message);
    return true;
}

bool InstallProgressionSelectionClassDisplayHook(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.multiclass_ui_display_allowed ||
        manifest.char_select_class_name_func_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.char_select_class_name_func_address == 0) {
        if (manifest.multiclass_ui_display_allowed) {
            std::wstring message =
                L"hook_manager: progression selection class display hook denied ";
            message += FormatDiscoveryDetails(
                L"ProgressionSelectionClassValueWriter",
                manifest.char_select_class_name_func_evidence_source,
                manifest.char_select_class_name_func_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        monomyth::logger::Log(
            L"hook_manager: progression selection class display hook denied because module base was unavailable");
        return false;
    }

    bool installed_any = false;
    constexpr std::array<std::uint8_t, 32> kCharSelectClassNameFuncEntryBytes = {{
        0x6A, 0xFF, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        0x68, 0x15, 0x2D, 0x99, 0x00, 0x50, 0x64, 0x89,
        0x25, 0x00, 0x00, 0x00, 0x00, 0x81, 0xEC, 0x98,
        0x00, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57, 0x8B,
    }};
    std::array<std::uint8_t, kCharSelectClassNameFuncEntryBytes.size()>
        live_char_select_class_name_func_entry = {};
    const bool char_select_class_name_func_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kCharSelectClassNameFuncRva),
        live_char_select_class_name_func_entry.size(),
        live_char_select_class_name_func_entry.data());
    const bool char_select_class_name_func_entry_matches =
        char_select_class_name_func_entry_copied &&
        std::memcmp(
            live_char_select_class_name_func_entry.data(),
            kCharSelectClassNameFuncEntryBytes.data(),
            kCharSelectClassNameFuncEntryBytes.size()) == 0;
    if (!char_select_class_name_func_entry_matches) {
        std::wstring message =
            L"hook_manager: char select class-name function trace denied target=CharSelectClassNameFunc validation=entry_bytes_mismatch expected=\"";
        message += HexBytes(
            kCharSelectClassNameFuncEntryBytes.data(),
            kCharSelectClassNameFuncEntryBytes.size());
        message += L"\" live=\"";
        message += HexBytes(
            live_char_select_class_name_func_entry.data(),
            live_char_select_class_name_func_entry.size());
        message += L"\" address=";
        message += HexPtr(module_base + kCharSelectClassNameFuncRva);
        message += L" target_rva=";
        message += Hex32(kCharSelectClassNameFuncRva);
        monomyth::logger::Log(message);
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(manifest.char_select_class_name_func_address),
                   reinterpret_cast<void*>(&CharSelectClassNameFuncHook),
                   &g_char_select_class_name_func_detour,
                   reinterpret_cast<void**>(&g_original_char_select_class_name_func),
                   L"CharSelectClassNameFunc trace")) {
        RemoveInlineDetour(&g_char_select_class_name_func_detour);
        g_original_char_select_class_name_func = nullptr;
    } else {
        std::wstring message =
            L"hook_manager: char select class-name function trace installed target=CharSelectClassNameFunc address=";
        message += HexPtr(manifest.char_select_class_name_func_address);
        message += L" target_rva=";
        message += Hex32(manifest.char_select_class_name_func_rva);
        monomyth::logger::Log(message);
        installed_any = true;
    }

    g_original_progression_selection_class_lookup =
        reinterpret_cast<ProgressionSelectionClassLookupFn>(
            module_base + kProgressionSelectionClassLookupTargetRva);
    if (!InstallCallsitePatch(
            reinterpret_cast<void*>(module_base + kProgressionSelectionClassLookupCallsiteRva),
            reinterpret_cast<void*>(&ProgressionSelectionClassLookupCallsiteHook),
            module_base + kProgressionSelectionClassLookupTargetRva,
            &g_progression_selection_class_lookup_callsite_patch,
            L"ProgressionSelectionClassLookupCallsite")) {
        g_original_progression_selection_class_lookup = nullptr;
    } else {
        installed_any = true;
    }

    if (InstallCallsitePatch(
            reinterpret_cast<void*>(module_base + kCharSelectLateFullNameCallsiteARva),
            reinterpret_cast<void*>(&CharSelectLateFullNameCallsiteAHook),
            module_base + kGetClassDescRva,
            &g_char_select_late_full_name_callsite_a_patch,
            L"CharSelectLateFullNameCallsiteA")) {
        installed_any = true;
    }
    if (InstallCallsitePatch(
            reinterpret_cast<void*>(module_base + kCharSelectLateFullNameCallsiteBCallsiteRva),
            reinterpret_cast<void*>(&CharSelectLateFullNameCallsiteBHook),
            module_base + kGetClassDescRva,
            &g_char_select_late_full_name_callsite_b_patch,
            L"CharSelectLateFullNameCallsiteB")) {
        installed_any = true;
    }
    if (InstallCallsitePatch(
            reinterpret_cast<void*>(module_base + kCharSelectLateFullNameCallsiteCCallsiteRva),
            reinterpret_cast<void*>(&CharSelectLateFullNameCallsiteCHook),
            module_base + kGetClassDescRva,
            &g_char_select_late_full_name_callsite_c_patch,
            L"CharSelectLateFullNameCallsiteC")) {
        installed_any = true;
    }

    if (!installed_any) {
        return false;
    }

    g_progression_selection_trace_count = 0;
    g_char_select_class_name_func_trace_count = 0;
    g_char_select_late_full_name_trace_count = 0;
    std::wstring message =
        L"hook_manager: progression selection class display hook installed target=ClassValueLabel entry_trace=";
    message += g_char_select_class_name_func_detour.installed ? L"true" : L"false";
    message += L" callsite_patch=";
    message += g_progression_selection_class_lookup_callsite_patch.installed ? L"true" : L"false";
    message += L" late_full_name_a=";
    message += g_char_select_late_full_name_callsite_a_patch.installed ? L"true" : L"false";
    message += L" late_full_name_b=";
    message += g_char_select_late_full_name_callsite_b_patch.installed ? L"true" : L"false";
    message += L" late_full_name_c=";
    message += g_char_select_late_full_name_callsite_c_patch.installed ? L"true" : L"false";
    message += L" local_self_style=three_letter";
    monomyth::logger::Log(message);
    return true;
}

bool InstallInventoryClassTitleDisplayHook(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.multiclass_ui_display_allowed) {
        return false;
    }
    monomyth::logger::Log(
        L"hook_manager: item display class text trace retired while inventory summary surface is the active multiclass blocker");
    return false;
}

bool InstallReceiveDispatchHook(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.receive_dispatch_validated || manifest.receive_dispatch_address == 0) {
        monomyth::logger::Log(
            L"hook_manager: receive dispatcher hook denied because discovery is not validated");
        return false;
    }

    auto* target = reinterpret_cast<void*>(manifest.receive_dispatch_address);
    if (!InstallInlineDetour(
            target,
            reinterpret_cast<void*>(&ReceiveDispatchHook),
            &g_receive_dispatch_detour,
            reinterpret_cast<void**>(&g_original_receive_dispatch),
            L"receive dispatcher")) {
        RemoveInlineDetour(&g_receive_dispatch_detour);
        return false;
    }

    std::wstring message = L"hook_manager: receive dispatcher hook installed address=";
    message += HexPtr(manifest.receive_dispatch_address);
    message += manifest.receive_introspection_allowed
        ? L" mode=metadata_plus_bounded_introspection"
        : L" mode=metadata_only";
    monomyth::logger::Log(message);
    return true;
}

bool InstallGetSpellLevelNeededHook(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) ||
        manifest.get_spell_level_needed_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.get_spell_level_needed_address == 0) {
        if (manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) {
            std::wstring message = L"hook_manager: spell usability hook denied ";
            message += FormatDiscoveryDetails(
                L"GetSpellLevelNeeded",
                manifest.get_spell_level_needed_evidence_source,
                manifest.get_spell_level_needed_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.get_spell_level_needed_address),
            reinterpret_cast<void*>(&GetSpellLevelNeededHook),
            &g_get_spell_level_needed_detour,
            reinterpret_cast<void**>(&g_original_get_spell_level_needed),
            L"GetSpellLevelNeeded")) {
        RemoveInlineDetour(&g_get_spell_level_needed_detour);
        g_original_get_spell_level_needed = nullptr;
        return false;
    }

    g_multiclass_spell_usability_enabled = manifest.multiclass_spell_usability_allowed;
    std::wstring message = L"hook_manager: spell usability hook installed target=GetSpellLevelNeeded address=";
    message += HexPtr(manifest.get_spell_level_needed_address);
    message += L" trace_enabled=";
    message += manifest.spell_usability_trace_allowed ? L"true" : L"false";
    message += L" multiclass_spell_usability_enabled=";
    message += g_multiclass_spell_usability_enabled ? L"true" : L"false";
    monomyth::logger::Log(message);
    g_get_spell_level_needed_trace_count = 0;
    return true;
}

bool InstallScrollScribeTraceHooks(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.scroll_scribe_trace_allowed || manifest.multiclass_item_usability_allowed) ||
        manifest.is_class_usable_predicate_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.is_class_usable_predicate_address == 0) {
        if (manifest.scroll_scribe_trace_dev_opt_in) {
            std::wstring message = L"hook_manager: scroll scribe trace denied ";
            message += FormatDiscoveryDetails(
                L"EQ_Character::IsClassUsablePredicate",
                manifest.is_class_usable_predicate_evidence_source,
                manifest.is_class_usable_predicate_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (manifest.scroll_scribe_trace_allowed) {
        if (manifest.handle_rbutton_up_state !=
                monomyth::spell_usability_discovery::TargetState::kValidated ||
            manifest.handle_rbutton_up_address == 0) {
            if (manifest.scroll_scribe_trace_dev_opt_in) {
                std::wstring message = L"hook_manager: scroll scribe trace denied ";
                message += FormatDiscoveryDetails(
                    L"CInvSlot::HandleRButtonUp",
                    manifest.handle_rbutton_up_evidence_source,
                    manifest.handle_rbutton_up_failure_reason);
                message += L"; ";
                message += FormatDiscoveryDetails(
                    L"EQ_Character::IsClassUsablePredicate",
                    manifest.is_class_usable_predicate_evidence_source,
                    manifest.is_class_usable_predicate_failure_reason);
                monomyth::logger::Log(message);
            }
            return false;
        }

        if (!InstallInlineDetour(
                reinterpret_cast<void*>(manifest.handle_rbutton_up_address),
                reinterpret_cast<void*>(&HandleRButtonUpHook),
                &g_handle_rbutton_up_detour,
                reinterpret_cast<void**>(&g_original_handle_rbutton_up),
                L"CInvSlot::HandleRButtonUp trace")) {
            RemoveInlineDetour(&g_handle_rbutton_up_detour);
            g_original_handle_rbutton_up = nullptr;
            return false;
        }
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.is_class_usable_predicate_address),
            reinterpret_cast<void*>(&IsClassUsablePredicateHook),
            &g_is_class_usable_predicate_detour,
            reinterpret_cast<void**>(&g_original_is_class_usable_predicate),
            L"IsClassUsablePredicate")) {
        RemoveInlineDetour(&g_is_class_usable_predicate_detour);
        if (g_handle_rbutton_up_detour.installed) {
            RemoveInlineDetour(&g_handle_rbutton_up_detour);
        }
        g_original_is_class_usable_predicate = nullptr;
        g_original_handle_rbutton_up = nullptr;
        return false;
    }

    g_is_class_usable_predicate_override_count = 0;
    std::wstring message = L"hook_manager: class usability hook installed target=EQ_Character::IsClassUsablePredicate address=";
    message += HexPtr(manifest.is_class_usable_predicate_address);
    message += L" scroll_scribe_trace_enabled=";
    message += manifest.scroll_scribe_trace_allowed ? L"true" : L"false";
    message += L" multiclass_item_usability_requested=";
    message += manifest.multiclass_item_usability_allowed ? L"true" : L"false";
    if (manifest.scroll_scribe_trace_allowed) {
        message += L" handle_rbutton_up_address=";
        message += HexPtr(manifest.handle_rbutton_up_address);
    }
    monomyth::logger::Log(message);
    g_scroll_scribe_event_count = 0;
    g_scroll_scribe_active_correlation_id = 0;
    g_scroll_scribe_active_logging = false;
    g_handle_rbutton_up_evidence_source = manifest.handle_rbutton_up_evidence_source;
    g_is_class_usable_predicate_evidence_source =
        manifest.is_class_usable_predicate_evidence_source;
    return true;
}

bool InstallGuildTrainerHook(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.hooks_allowed || !manifest.packet_hooks_allowed) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        monomyth::logger::Log(
            L"hook_manager: guild trainer hook denied reason=\"missing host module base\"");
        return false;
    }

    auto* target = reinterpret_cast<void*>(module_base + kGuildTrainerClassLookupCallsiteRva);
    if (std::memcmp(
            target,
            kGuildTrainerClassLookupCallsiteBytes.data(),
            kGuildTrainerClassLookupCallsiteBytes.size()) != 0) {
        std::wstring message =
            L"hook_manager: guild trainer hook denied reason=\"class lookup callsite bytes mismatch\"";
        message += L" callsite_rva=";
        message += Hex32(kGuildTrainerClassLookupCallsiteRva);
        message += L" callsite_address=";
        message += HexPtr(module_base + kGuildTrainerClassLookupCallsiteRva);
        monomyth::logger::Log(message);
        return false;
    }

    g_original_guild_trainer_class_lookup =
        reinterpret_cast<GuildTrainerClassLookupFn>(
            module_base + kGuildTrainerClassLookupTargetRva);
    if (!InstallCallsitePatch(
            target,
            reinterpret_cast<void*>(&GuildTrainerClassLookupCallsiteHookBridge),
            module_base + kGuildTrainerClassLookupTargetRva,
            &g_guild_trainer_class_lookup_callsite_patch,
            L"GuildTrainerClassLookupCallsite")) {
        g_original_guild_trainer_class_lookup = nullptr;
        return false;
    }

    g_guild_trainer_class_lookup_override_count = 0;
    g_guild_trainer_class_lookup_override_record.fill(0);

    std::wstring message =
        L"hook_manager: guild trainer hook installed target=GuildTrainerClassLookupCallsite";
    message += L" callsite_rva=";
    message += Hex32(kGuildTrainerClassLookupCallsiteRva);
    message += L" callsite_address=";
    message += HexPtr(module_base + kGuildTrainerClassLookupCallsiteRva);
    message += L" original_target_rva=";
    message += Hex32(kGuildTrainerClassLookupTargetRva);
    message += L" original_target_address=";
    message += HexPtr(module_base + kGuildTrainerClassLookupTargetRva);
    message += L" packet_hooks_allowed=";
    message += manifest.packet_hooks_allowed ? L"true" : L"false";
    message += L" evidence_source=active_guildmaster_class_match_callsite";
    monomyth::logger::Log(message);
    return true;
}

bool InstallCanEquipHook(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.multiclass_item_usability_allowed ||
        manifest.can_equip_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.can_equip_address == 0) {
        if (manifest.multiclass_item_usability_allowed) {
            std::wstring message = L"hook_manager: item usability hook denied ";
            message += FormatDiscoveryDetails(
                L"EQ_Character::CanEquip",
                manifest.can_equip_evidence_source,
                manifest.can_equip_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.can_equip_address),
            reinterpret_cast<void*>(&CanEquipHook),
            &g_can_equip_detour,
            reinterpret_cast<void**>(&g_original_can_equip),
            L"CanEquip")) {
        RemoveInlineDetour(&g_can_equip_detour);
        g_original_can_equip = nullptr;
        return false;
    }

    g_multiclass_item_usability_enabled = true;
    g_invslot_handle_lbutton_core_late_branch_prep_override_count = 0;
    g_invslot_handle_lbutton_core_late_branch_gate_b_override_count = 0;
    std::wstring message =
        L"hook_manager: item usability hook installed target=EQ_Character::CanEquip address=";
    message += HexPtr(manifest.can_equip_address);
    monomyth::logger::Log(message);

    const std::uintptr_t module_base = GetHostModuleBase();
    constexpr std::array<std::uint8_t, 16> kCharacterZoneClientHasSkillEntryBytes = {
        0x8b, 0x44, 0x24, 0x04, 0x50, 0xe8, 0x26, 0xfe,
        0xff, 0xff, 0x33, 0xc9, 0x85, 0xc0, 0x0f, 0x9f};
    std::array<std::uint8_t, kCharacterZoneClientHasSkillEntryBytes.size()>
        live_character_zone_client_has_skill_entry = {};
    const bool has_skill_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kCharacterZoneClientHasSkillRva),
        live_character_zone_client_has_skill_entry.size(),
        live_character_zone_client_has_skill_entry.data());
    const bool has_skill_entry_matches =
        has_skill_entry_copied &&
        std::memcmp(
            live_character_zone_client_has_skill_entry.data(),
            kCharacterZoneClientHasSkillEntryBytes.data(),
            kCharacterZoneClientHasSkillEntryBytes.size()) == 0;
    if (!has_skill_entry_matches) {
        std::wstring has_skill_message =
            L"hook_manager: offhand dual wield seam denied target=CharacterZoneClient::HasSkill expected=\"";
        has_skill_message += HexBytes(
            kCharacterZoneClientHasSkillEntryBytes.data(),
            kCharacterZoneClientHasSkillEntryBytes.size());
        has_skill_message += L"\" live=\"";
        has_skill_message += HexBytes(
            live_character_zone_client_has_skill_entry.data(),
            live_character_zone_client_has_skill_entry.size());
        has_skill_message += L"\" address=";
        has_skill_message += HexPtr(module_base + kCharacterZoneClientHasSkillRva);
        has_skill_message += L" target_rva=";
        has_skill_message += Hex32(kCharacterZoneClientHasSkillRva);
        monomyth::logger::Log(has_skill_message);
        RemoveInlineDetour(&g_can_equip_detour);
        g_original_can_equip = nullptr;
        g_multiclass_item_usability_enabled = false;
        return false;
    }
    g_character_zone_client_has_skill_address = module_base + kCharacterZoneClientHasSkillRva;
    g_character_zone_client_has_skill =
        reinterpret_cast<CharacterZoneClientHasSkillFn>(g_character_zone_client_has_skill_address);
    {
        std::wstring has_skill_message =
            L"hook_manager: offhand dual wield seam validated target=CharacterZoneClient::HasSkill address=";
        has_skill_message += HexPtr(g_character_zone_client_has_skill_address);
        has_skill_message += L" target_rva=";
        has_skill_message += Hex32(kCharacterZoneClientHasSkillRva);
        monomyth::logger::Log(has_skill_message);
    }

    constexpr std::array<std::uint8_t, 8> kMoveItemSlot21LookupEntryBytes = {
        0x51, 0x56, 0x83, 0xc1, 0x04, 0xc7, 0x44, 0x24};
    std::array<std::uint8_t, kMoveItemSlot21LookupEntryBytes.size()> live_slot21_lookup_entry =
        {};
    const bool slot21_lookup_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kMoveItemSlot21LookupTargetRva),
        live_slot21_lookup_entry.size(),
        live_slot21_lookup_entry.data());
    const bool slot21_lookup_entry_matches =
        slot21_lookup_entry_copied &&
        std::memcmp(
            live_slot21_lookup_entry.data(),
            kMoveItemSlot21LookupEntryBytes.data(),
            kMoveItemSlot21LookupEntryBytes.size()) == 0;
    if (!slot21_lookup_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: move item slot21 lookup trace denied target=0x42dec0 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(module_base + kMoveItemSlot21LookupTargetRva),
                   reinterpret_cast<void*>(&MoveItemSlot21LookupHook),
                   &g_move_item_slot21_lookup_detour,
                   reinterpret_cast<void**>(&g_original_move_item_slot21_lookup),
                   L"MoveItemSlot21Lookup trace")) {
        RemoveInlineDetour(&g_move_item_slot21_lookup_detour);
        g_original_move_item_slot21_lookup = nullptr;
    } else {
        std::wstring slot21_lookup_message =
            L"hook_manager: move item slot21 lookup trace installed address=";
        slot21_lookup_message += HexPtr(module_base + kMoveItemSlot21LookupTargetRva);
        slot21_lookup_message += L" target_rva=";
        slot21_lookup_message += Hex32(kMoveItemSlot21LookupTargetRva);
        slot21_lookup_message += L" filtered_caller_return_rvas=";
        slot21_lookup_message += Hex32(kMoveItemSlot21LookupCallerReturnRva);
        slot21_lookup_message += L",";
        slot21_lookup_message += Hex32(kInventoryWndNotificationSlot21LookupCallerReturnARva);
        slot21_lookup_message += L",";
        slot21_lookup_message += Hex32(kInventoryWndNotificationSlot21LookupCallerReturnBRva);
        slot21_lookup_message += L",";
        slot21_lookup_message += Hex32(kInvSlotHandleLButtonCoreSlot21LookupCallerReturnARva);
        slot21_lookup_message += L",";
        slot21_lookup_message += Hex32(kInvSlotHandleLButtonCoreSlot21LookupCallerReturnBRva);
        monomyth::logger::Log(slot21_lookup_message);
    }

    {
        constexpr std::array<std::uint8_t, 8> kInvSlotHandleLButtonCoreEntryBytes = {
            0x6a, 0xff, 0x68, 0x1d, 0xa8, 0x98, 0x00, 0x64};
        std::array<std::uint8_t, kInvSlotHandleLButtonCoreEntryBytes.size()>
            live_invslot_handle_lbutton_core_entry = {};
        const bool invslot_handle_lbutton_core_entry_copied = TryCopyBytes(
            reinterpret_cast<const void*>(module_base + kInvSlotHandleLButtonCoreTargetRva),
            live_invslot_handle_lbutton_core_entry.size(),
            live_invslot_handle_lbutton_core_entry.data());
        const bool invslot_handle_lbutton_core_entry_matches =
            invslot_handle_lbutton_core_entry_copied &&
            std::memcmp(
                live_invslot_handle_lbutton_core_entry.data(),
                kInvSlotHandleLButtonCoreEntryBytes.data(),
                kInvSlotHandleLButtonCoreEntryBytes.size()) == 0;
        if (!invslot_handle_lbutton_core_entry_matches) {
            monomyth::logger::Log(
                L"hook_manager: item usability helper denied target=CInvSlot::HandleLButtonUp validation=entry_bytes_mismatch");
            RemoveInlineDetour(&g_move_item_slot21_lookup_detour);
            g_original_move_item_slot21_lookup = nullptr;
            RemoveInlineDetour(&g_can_equip_detour);
            g_original_can_equip = nullptr;
            g_multiclass_item_usability_enabled = false;
            return false;
        }
        if (!InstallInlineDetour(
                reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreTargetRva),
                reinterpret_cast<void*>(&InvSlotHandleLButtonCoreHook),
                &g_invslot_handle_lbutton_core_detour,
                reinterpret_cast<void**>(&g_original_invslot_handle_lbutton_core),
                L"CInvSlot::HandleLButtonUp core trace")) {
            RemoveInlineDetour(&g_move_item_slot21_lookup_detour);
            g_original_move_item_slot21_lookup = nullptr;
            RemoveInlineDetour(&g_can_equip_detour);
            g_original_can_equip = nullptr;
            g_multiclass_item_usability_enabled = false;
            return false;
        }
        {
            std::wstring core_message =
                L"hook_manager: item usability helper installed target=CInvSlot::HandleLButtonUp address=";
            core_message += HexPtr(module_base + kInvSlotHandleLButtonCoreTargetRva);
            monomyth::logger::Log(core_message);
        }

        g_original_invslot_handle_lbutton_core_late_branch_gate_b =
            reinterpret_cast<InvSlotHandleLButtonCoreLateBranchGateBFn>(
                module_base + kInvSlotHandleLButtonCoreLateBranchGateBTargetRva);
        if (!InstallCallsitePatch(
                reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchGateBCallsiteRva),
                reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchGateBCallsiteHook),
                module_base + kInvSlotHandleLButtonCoreLateBranchGateBTargetRva,
                &g_invslot_handle_lbutton_core_late_branch_gate_b_callsite_patch,
                L"InvSlotHandleLButtonCoreLateBranchGateBCallsite")) {
            RemoveInlineDetour(&g_invslot_handle_lbutton_core_detour);
            g_original_invslot_handle_lbutton_core = nullptr;
            RemoveInlineDetour(&g_move_item_slot21_lookup_detour);
            g_original_move_item_slot21_lookup = nullptr;
            RemoveInlineDetour(&g_can_equip_detour);
            g_original_can_equip = nullptr;
            g_multiclass_item_usability_enabled = false;
            return false;
        }

        {
            std::wstring auto_equip_class_gate_message =
                L"hook_manager: auto equip class gate integrated via shared 0x44c430 hook target_rva=";
            auto_equip_class_gate_message += Hex32(kAutoEquipClassGateTargetRva);
            auto_equip_class_gate_message += L" filtered_caller_return_rvas=";
            auto_equip_class_gate_message += Hex32(kAutoEquipClassGateCallerReturnARva);
            auto_equip_class_gate_message += L",";
            auto_equip_class_gate_message += Hex32(kAutoEquipClassGateCallerReturnBRva);
            monomyth::logger::Log(auto_equip_class_gate_message);
        }

        constexpr std::array<std::uint8_t, 8> kInventoryWindowWndNotificationEntryBytes = {
            0x6a, 0xff, 0x68, 0xa8, 0xa3, 0x98, 0x00, 0x64};
        std::array<std::uint8_t, kInventoryWindowWndNotificationEntryBytes.size()>
            live_inventory_wnd_notification_entry = {};
        const bool inventory_wnd_notification_entry_copied = TryCopyBytes(
            reinterpret_cast<const void*>(module_base + kInventoryWindowWndNotificationTargetRva),
            live_inventory_wnd_notification_entry.size(),
            live_inventory_wnd_notification_entry.data());
        const bool inventory_wnd_notification_entry_matches =
            inventory_wnd_notification_entry_copied &&
            std::memcmp(
                live_inventory_wnd_notification_entry.data(),
                kInventoryWindowWndNotificationEntryBytes.data(),
                kInventoryWindowWndNotificationEntryBytes.size()) == 0;
        if (!inventory_wnd_notification_entry_matches) {
            std::wstring inventory_wnd_notification_message =
                L"hook_manager: inventory window wnd notification trace denied target=InventoryWindowWndNotification validation=entry_bytes_mismatch expected=\"";
            inventory_wnd_notification_message += HexBytes(
                kInventoryWindowWndNotificationEntryBytes.data(),
                kInventoryWindowWndNotificationEntryBytes.size());
            inventory_wnd_notification_message += L"\" live=\"";
            inventory_wnd_notification_message += HexBytes(
                live_inventory_wnd_notification_entry.data(),
                live_inventory_wnd_notification_entry.size());
            inventory_wnd_notification_message += L"\" address=";
            inventory_wnd_notification_message +=
                HexPtr(module_base + kInventoryWindowWndNotificationTargetRva);
            inventory_wnd_notification_message += L" target_rva=";
            inventory_wnd_notification_message +=
                Hex32(kInventoryWindowWndNotificationTargetRva);
            monomyth::logger::Log(inventory_wnd_notification_message);
        } else if (!InstallInlineDetour(
                       reinterpret_cast<void*>(
                           module_base + kInventoryWindowWndNotificationTargetRva),
                       reinterpret_cast<void*>(&InventoryWindowWndNotificationHook),
                       &g_inventory_window_wnd_notification_detour,
                       reinterpret_cast<void**>(&g_original_inventory_window_wnd_notification),
                       L"CInventoryWindow::WndNotification trace")) {
            RemoveInlineDetour(&g_inventory_window_wnd_notification_detour);
            g_original_inventory_window_wnd_notification = nullptr;
        } else {
            std::wstring inventory_wnd_notification_message =
                L"hook_manager: inventory window wnd notification trace installed address=";
            inventory_wnd_notification_message +=
                HexPtr(module_base + kInventoryWindowWndNotificationTargetRva);
            inventory_wnd_notification_message += L" target_rva=";
            inventory_wnd_notification_message +=
                Hex32(kInventoryWindowWndNotificationTargetRva);
            monomyth::logger::Log(inventory_wnd_notification_message);
        }

        constexpr std::array<std::uint8_t, 8> kInventorySummaryRefreshCandidateAEntryBytes = {
            0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x6a, 0xff};
        std::array<std::uint8_t, kInventorySummaryRefreshCandidateAEntryBytes.size()>
            live_inventory_summary_candidate_a_entry = {};
        const bool inventory_summary_candidate_a_entry_copied = TryCopyBytes(
            reinterpret_cast<const void*>(module_base + kInventorySummaryRefreshCandidateARva),
            live_inventory_summary_candidate_a_entry.size(),
            live_inventory_summary_candidate_a_entry.data());
        const bool inventory_summary_candidate_a_entry_matches =
            inventory_summary_candidate_a_entry_copied &&
            std::memcmp(
                live_inventory_summary_candidate_a_entry.data(),
                kInventorySummaryRefreshCandidateAEntryBytes.data(),
                kInventorySummaryRefreshCandidateAEntryBytes.size()) == 0;
        if (!inventory_summary_candidate_a_entry_matches) {
            monomyth::logger::Log(
                L"hook_manager: inventory summary candidate trace denied target=InventorySummaryRefreshCandidateA validation=entry_bytes_mismatch");
        } else if (!InstallInlineDetour(
                       reinterpret_cast<void*>(
                           module_base + kInventorySummaryRefreshCandidateARva),
                       reinterpret_cast<void*>(&InventorySummaryRefreshCandidateAHook),
                       &g_inventory_summary_refresh_candidate_a_detour,
                       reinterpret_cast<void**>(&g_original_inventory_summary_refresh_candidate_a),
                       L"InventorySummaryRefreshCandidateA trace")) {
            RemoveInlineDetour(&g_inventory_summary_refresh_candidate_a_detour);
            g_original_inventory_summary_refresh_candidate_a = nullptr;
        } else {
            std::wstring candidate_message =
                L"hook_manager: inventory summary candidate trace installed target=InventorySummaryRefreshCandidateA address=";
            candidate_message += HexPtr(module_base + kInventorySummaryRefreshCandidateARva);
            candidate_message += L" target_rva=";
            candidate_message += Hex32(kInventorySummaryRefreshCandidateARva);
            monomyth::logger::Log(candidate_message);
        }

        constexpr std::array<std::uint8_t, 8> kInventorySummaryRefreshCandidateBEntryBytes = {
            0x83, 0xec, 0x24, 0x53, 0x57, 0x8b, 0xf9, 0x8b};
        std::array<std::uint8_t, kInventorySummaryRefreshCandidateBEntryBytes.size()>
            live_inventory_summary_candidate_b_entry = {};
        const bool inventory_summary_candidate_b_entry_copied = TryCopyBytes(
            reinterpret_cast<const void*>(module_base + kInventorySummaryRefreshCandidateBRva),
            live_inventory_summary_candidate_b_entry.size(),
            live_inventory_summary_candidate_b_entry.data());
        const bool inventory_summary_candidate_b_entry_matches =
            inventory_summary_candidate_b_entry_copied &&
            std::memcmp(
                live_inventory_summary_candidate_b_entry.data(),
                kInventorySummaryRefreshCandidateBEntryBytes.data(),
                kInventorySummaryRefreshCandidateBEntryBytes.size()) == 0;
        if (!inventory_summary_candidate_b_entry_matches) {
            monomyth::logger::Log(
                L"hook_manager: inventory summary candidate trace denied target=InventorySummaryRefreshCandidateB validation=entry_bytes_mismatch");
        } else if (!InstallInlineDetour(
                       reinterpret_cast<void*>(
                           module_base + kInventorySummaryRefreshCandidateBRva),
                       reinterpret_cast<void*>(&InventorySummaryRefreshCandidateBHook),
                       &g_inventory_summary_refresh_candidate_b_detour,
                       reinterpret_cast<void**>(&g_original_inventory_summary_refresh_candidate_b),
                       L"InventorySummaryRefreshCandidateB trace")) {
            RemoveInlineDetour(&g_inventory_summary_refresh_candidate_b_detour);
            g_original_inventory_summary_refresh_candidate_b = nullptr;
        } else {
            std::wstring candidate_message =
                L"hook_manager: inventory summary candidate trace installed target=InventorySummaryRefreshCandidateB address=";
            candidate_message += HexPtr(module_base + kInventorySummaryRefreshCandidateBRva);
            candidate_message += L" target_rva=";
            candidate_message += Hex32(kInventorySummaryRefreshCandidateBRva);
            monomyth::logger::Log(candidate_message);
        }

        constexpr std::array<std::uint8_t, 8> kInventorySummaryRefreshCandidateCEntryBytes = {
            0x8b, 0x8e, 0x44, 0x02, 0x00, 0x00, 0x8b, 0x01};
        std::array<std::uint8_t, kInventorySummaryRefreshCandidateCEntryBytes.size()>
            live_inventory_summary_candidate_c_entry = {};
        const bool inventory_summary_candidate_c_entry_copied = TryCopyBytes(
            reinterpret_cast<const void*>(module_base + kInventorySummaryRefreshCandidateCRva),
            live_inventory_summary_candidate_c_entry.size(),
            live_inventory_summary_candidate_c_entry.data());
        const bool inventory_summary_candidate_c_entry_matches =
            inventory_summary_candidate_c_entry_copied &&
            std::memcmp(
                live_inventory_summary_candidate_c_entry.data(),
                kInventorySummaryRefreshCandidateCEntryBytes.data(),
                kInventorySummaryRefreshCandidateCEntryBytes.size()) == 0;
        if (!inventory_summary_candidate_c_entry_matches) {
            std::wstring candidate_message =
                L"hook_manager: inventory summary candidate trace denied target=InventorySummaryRefreshCandidateC validation=entry_bytes_mismatch expected=\"";
            candidate_message += HexBytes(
                kInventorySummaryRefreshCandidateCEntryBytes.data(),
                kInventorySummaryRefreshCandidateCEntryBytes.size());
            candidate_message += L"\" live=\"";
            candidate_message += HexBytes(
                live_inventory_summary_candidate_c_entry.data(),
                live_inventory_summary_candidate_c_entry.size());
            candidate_message += L"\" address=";
            candidate_message += HexPtr(module_base + kInventorySummaryRefreshCandidateCRva);
            candidate_message += L" target_rva=";
            candidate_message += Hex32(kInventorySummaryRefreshCandidateCRva);
            monomyth::logger::Log(candidate_message);
        } else if (!InstallInlineDetour(
                       reinterpret_cast<void*>(
                           module_base + kInventorySummaryRefreshCandidateCRva),
                       reinterpret_cast<void*>(&InventorySummaryRefreshCandidateCHook),
                       &g_inventory_summary_refresh_candidate_c_detour,
                       reinterpret_cast<void**>(&g_original_inventory_summary_refresh_candidate_c),
                       L"InventorySummaryRefreshCandidateC trace")) {
            RemoveInlineDetour(&g_inventory_summary_refresh_candidate_c_detour);
            g_original_inventory_summary_refresh_candidate_c = nullptr;
        } else {
            std::wstring candidate_message =
                L"hook_manager: inventory summary candidate trace installed target=InventorySummaryRefreshCandidateC address=";
            candidate_message += HexPtr(module_base + kInventorySummaryRefreshCandidateCRva);
            candidate_message += L" target_rva=";
            candidate_message += Hex32(kInventorySummaryRefreshCandidateCRva);
            monomyth::logger::Log(candidate_message);
        }

        constexpr std::array<std::uint8_t, 9> kInventorySummaryRefreshCandidateDEntryBytes = {
            0x51, 0x56, 0x8b, 0xf1, 0xe8, 0xa7, 0x14, 0x1d, 0x00};
        std::array<std::uint8_t, kInventorySummaryRefreshCandidateDEntryBytes.size()>
            live_inventory_summary_candidate_d_entry = {};
        const bool inventory_summary_candidate_d_entry_copied = TryCopyBytes(
            reinterpret_cast<const void*>(module_base + kInventorySummaryRefreshCandidateDRva),
            live_inventory_summary_candidate_d_entry.size(),
            live_inventory_summary_candidate_d_entry.data());
        const bool inventory_summary_candidate_d_entry_matches =
            inventory_summary_candidate_d_entry_copied &&
            std::memcmp(
                live_inventory_summary_candidate_d_entry.data(),
                kInventorySummaryRefreshCandidateDEntryBytes.data(),
                kInventorySummaryRefreshCandidateDEntryBytes.size()) == 0;
        if (!inventory_summary_candidate_d_entry_matches) {
            std::wstring candidate_message =
                L"hook_manager: inventory summary candidate trace denied target=InventorySummaryRefreshCandidateD validation=entry_bytes_mismatch expected=\"";
            candidate_message += HexBytes(
                kInventorySummaryRefreshCandidateDEntryBytes.data(),
                kInventorySummaryRefreshCandidateDEntryBytes.size());
            candidate_message += L"\" live=\"";
            candidate_message += HexBytes(
                live_inventory_summary_candidate_d_entry.data(),
                live_inventory_summary_candidate_d_entry.size());
            candidate_message += L"\" address=";
            candidate_message += HexPtr(module_base + kInventorySummaryRefreshCandidateDRva);
            candidate_message += L" target_rva=";
            candidate_message += Hex32(kInventorySummaryRefreshCandidateDRva);
            monomyth::logger::Log(candidate_message);
        } else if (!InstallInlineDetour(
                       reinterpret_cast<void*>(
                           module_base + kInventorySummaryRefreshCandidateDRva),
                       reinterpret_cast<void*>(&InventorySummaryRefreshCandidateDHook),
                       &g_inventory_summary_refresh_candidate_d_detour,
                       reinterpret_cast<void**>(&g_original_inventory_summary_refresh_candidate_d),
                       L"InventorySummaryRefreshCandidateD trace")) {
            RemoveInlineDetour(&g_inventory_summary_refresh_candidate_d_detour);
            g_original_inventory_summary_refresh_candidate_d = nullptr;
        } else {
            std::wstring candidate_message =
                L"hook_manager: inventory summary candidate trace installed target=InventorySummaryRefreshCandidateD address=";
            candidate_message += HexPtr(module_base + kInventorySummaryRefreshCandidateDRva);
            candidate_message += L" target_rva=";
            candidate_message += Hex32(kInventorySummaryRefreshCandidateDRva);
            monomyth::logger::Log(candidate_message);
        }

        InstallDetailedItemUsabilityTraceCallsites(manifest, module_base);
        return true;
    }

    constexpr std::array<std::uint8_t, 8> kMoveItemDescriptorBuildEntryBytes = {
        0x8b, 0x44, 0x24, 0x04, 0x83, 0xca, 0xff, 0x89};
    std::array<std::uint8_t, kMoveItemDescriptorBuildEntryBytes.size()> live_descriptor_entry = {};
    const bool descriptor_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kMoveItemDescriptorBuildTargetRva),
        live_descriptor_entry.size(),
        live_descriptor_entry.data());
    const bool descriptor_entry_matches =
        descriptor_entry_copied &&
        std::memcmp(
            live_descriptor_entry.data(),
            kMoveItemDescriptorBuildEntryBytes.data(),
            kMoveItemDescriptorBuildEntryBytes.size()) == 0;
    if (!descriptor_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: move item descriptor build trace denied target=0x45fd30 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
            reinterpret_cast<void*>(module_base + kMoveItemDescriptorBuildTargetRva),
            reinterpret_cast<void*>(&MoveItemDescriptorBuildHook),
            &g_move_item_descriptor_build_detour,
            reinterpret_cast<void**>(&g_original_move_item_descriptor_build),
            L"MoveItemDescriptorBuild trace")) {
        RemoveInlineDetour(&g_move_item_descriptor_build_detour);
        g_original_move_item_descriptor_build = nullptr;
    } else {
        std::wstring descriptor_message =
            L"hook_manager: move item descriptor build trace installed address=";
        descriptor_message += HexPtr(module_base + kMoveItemDescriptorBuildTargetRva);
        descriptor_message += L" target_rva=";
        descriptor_message += Hex32(kMoveItemDescriptorBuildTargetRva);
        descriptor_message += L" filtered_caller_return_rva=";
        descriptor_message += Hex32(kMoveItemDescriptorBuildCallerReturnRva);
        monomyth::logger::Log(descriptor_message);
    }

    constexpr std::array<std::uint8_t, 8> kMoveItemSlotPopulateEntryBytes = {
        0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c, 0xa1, 0x80};
    std::array<std::uint8_t, kMoveItemSlotPopulateEntryBytes.size()> live_slot_populate_entry = {};
    const bool slot_populate_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kMoveItemSlotPopulateTargetRva),
        live_slot_populate_entry.size(),
        live_slot_populate_entry.data());
    const bool slot_populate_entry_matches =
        slot_populate_entry_copied &&
        std::memcmp(
            live_slot_populate_entry.data(),
            kMoveItemSlotPopulateEntryBytes.data(),
            kMoveItemSlotPopulateEntryBytes.size()) == 0;
    if (!slot_populate_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: move item slot populate trace denied target=0x7e4f40 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
            reinterpret_cast<void*>(module_base + kMoveItemSlotPopulateTargetRva),
            reinterpret_cast<void*>(&MoveItemSlotPopulateHook),
            &g_move_item_slot_populate_detour,
            reinterpret_cast<void**>(&g_original_move_item_slot_populate),
            L"MoveItemSlotPopulate trace")) {
        RemoveInlineDetour(&g_move_item_slot_populate_detour);
        g_original_move_item_slot_populate = nullptr;
    } else {
        std::wstring slot_populate_message =
            L"hook_manager: move item slot populate trace installed address=";
        slot_populate_message += HexPtr(module_base + kMoveItemSlotPopulateTargetRva);
        slot_populate_message += L" target_rva=";
        slot_populate_message += Hex32(kMoveItemSlotPopulateTargetRva);
        slot_populate_message += L" filtered_caller_return_rva=";
        slot_populate_message += Hex32(kMoveItemSlotPopulateCallerReturnRva);
        monomyth::logger::Log(slot_populate_message);
    }

    constexpr std::array<std::uint8_t, 8> kMoveItemOwnerPrimarySetterEntryBytes = {
        0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x56, 0x8b};
    std::array<std::uint8_t, kMoveItemOwnerPrimarySetterEntryBytes.size()>
        live_owner_primary_setter_entry = {};
    const bool owner_primary_setter_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kMoveItemOwnerPrimarySetterTargetRva),
        live_owner_primary_setter_entry.size(),
        live_owner_primary_setter_entry.data());
    const bool owner_primary_setter_entry_matches =
        owner_primary_setter_entry_copied &&
        std::memcmp(
            live_owner_primary_setter_entry.data(),
            kMoveItemOwnerPrimarySetterEntryBytes.data(),
            kMoveItemOwnerPrimarySetterEntryBytes.size()) == 0;
    if (!owner_primary_setter_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: move item owner primary setter trace denied target=0x7bb560 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(module_base + kMoveItemOwnerPrimarySetterTargetRva),
                   reinterpret_cast<void*>(&MoveItemOwnerPrimarySetterHook),
                   &g_move_item_owner_primary_setter_detour,
                   reinterpret_cast<void**>(&g_original_move_item_owner_primary_setter),
                   L"MoveItemOwnerPrimarySetter trace")) {
        RemoveInlineDetour(&g_move_item_owner_primary_setter_detour);
        g_original_move_item_owner_primary_setter = nullptr;
    } else {
        std::wstring owner_primary_setter_message =
            L"hook_manager: move item owner primary setter trace installed address=";
        owner_primary_setter_message +=
            HexPtr(module_base + kMoveItemOwnerPrimarySetterTargetRva);
        owner_primary_setter_message += L" target_rva=";
        owner_primary_setter_message += Hex32(kMoveItemOwnerPrimarySetterTargetRva);
        monomyth::logger::Log(owner_primary_setter_message);
    }

    constexpr std::array<std::uint8_t, 8> kEverQuestLMouseUpEntryBytes = {
        0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x6a, 0xff};
    std::array<std::uint8_t, kEverQuestLMouseUpEntryBytes.size()> live_lmouse_up_entry = {};
    const bool lmouse_up_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kEverQuestLMouseUpTargetRva),
        live_lmouse_up_entry.size(),
        live_lmouse_up_entry.data());
    const bool lmouse_up_entry_matches =
        lmouse_up_entry_copied &&
        std::memcmp(
            live_lmouse_up_entry.data(),
            kEverQuestLMouseUpEntryBytes.data(),
            kEverQuestLMouseUpEntryBytes.size()) == 0;
    if (!lmouse_up_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: everquest left mouse up trace denied target=0x4c1760 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(module_base + kEverQuestLMouseUpTargetRva),
                   reinterpret_cast<void*>(&EverQuestLMouseUpHook),
                   &g_everquest_lmouse_up_detour,
                   reinterpret_cast<void**>(&g_original_everquest_lmouse_up),
                   L"CEverQuest::LMouseUp trace")) {
        RemoveInlineDetour(&g_everquest_lmouse_up_detour);
        g_original_everquest_lmouse_up = nullptr;
    } else {
        std::wstring lmouse_up_message =
            L"hook_manager: everquest left mouse up trace installed address=";
        lmouse_up_message += HexPtr(module_base + kEverQuestLMouseUpTargetRva);
        lmouse_up_message += L" target_rva=";
        lmouse_up_message += Hex32(kEverQuestLMouseUpTargetRva);
        monomyth::logger::Log(lmouse_up_message);
    }

    constexpr std::array<std::uint8_t, 8> kCXWndHandleLButtonUpEntryBytes = {
        0x8b, 0x4c, 0x24, 0x10, 0x85, 0xc9, 0x75, 0x4c};
    std::array<std::uint8_t, kCXWndHandleLButtonUpEntryBytes.size()> live_handle_lbutton_up_entry =
        {};
    const bool handle_lbutton_up_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kCXWndHandleLButtonUpTargetRva),
        live_handle_lbutton_up_entry.size(),
        live_handle_lbutton_up_entry.data());
    const bool handle_lbutton_up_entry_matches =
        handle_lbutton_up_entry_copied &&
        std::memcmp(
            live_handle_lbutton_up_entry.data(),
            kCXWndHandleLButtonUpEntryBytes.data(),
            kCXWndHandleLButtonUpEntryBytes.size()) == 0;
    if (!handle_lbutton_up_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: cxwnd handle left button up trace denied target=0x4c1e91 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(module_base + kCXWndHandleLButtonUpTargetRva),
                   reinterpret_cast<void*>(&CXWndHandleLButtonUpHook),
                   &g_cxwnd_handle_lbutton_up_detour,
                   reinterpret_cast<void**>(&g_original_cxwnd_handle_lbutton_up),
                   L"CXWnd::HandleLButtonUp trace")) {
        RemoveInlineDetour(&g_cxwnd_handle_lbutton_up_detour);
        g_original_cxwnd_handle_lbutton_up = nullptr;
    } else {
        std::wstring handle_lbutton_up_message =
            L"hook_manager: cxwnd handle left button up trace installed address=";
        handle_lbutton_up_message +=
            HexPtr(module_base + kCXWndHandleLButtonUpTargetRva);
        handle_lbutton_up_message += L" target_rva=";
        handle_lbutton_up_message += Hex32(kCXWndHandleLButtonUpTargetRva);
        monomyth::logger::Log(handle_lbutton_up_message);
    }

    constexpr std::array<std::uint8_t, 8> kInvSlotWndHandleLButtonUpEntryBytes = {
        0x83, 0xec, 0x10, 0x56, 0x8b, 0xf1, 0x83, 0xbe};
    std::array<std::uint8_t, kInvSlotWndHandleLButtonUpEntryBytes.size()>
        live_invslot_handle_lbutton_up_entry = {};
    const bool invslot_handle_lbutton_up_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kInvSlotWndHandleLButtonUpTargetRva),
        live_invslot_handle_lbutton_up_entry.size(),
        live_invslot_handle_lbutton_up_entry.data());
    const bool invslot_handle_lbutton_up_entry_matches =
        invslot_handle_lbutton_up_entry_copied &&
        std::memcmp(
            live_invslot_handle_lbutton_up_entry.data(),
            kInvSlotWndHandleLButtonUpEntryBytes.data(),
            kInvSlotWndHandleLButtonUpEntryBytes.size()) == 0;
    if (!invslot_handle_lbutton_up_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: invslot wnd handle left button up trace denied target=0x69a5d0 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(module_base + kInvSlotWndHandleLButtonUpTargetRva),
                   reinterpret_cast<void*>(&InvSlotWndHandleLButtonUpHook),
                   &g_invslot_wnd_handle_lbutton_up_detour,
                   reinterpret_cast<void**>(&g_original_invslot_wnd_handle_lbutton_up),
                   L"CInvSlotWnd::HandleLButtonUp trace")) {
        RemoveInlineDetour(&g_invslot_wnd_handle_lbutton_up_detour);
        g_original_invslot_wnd_handle_lbutton_up = nullptr;
    } else {
        std::wstring invslot_handle_lbutton_up_message =
            L"hook_manager: invslot wnd handle left button up trace installed address=";
        invslot_handle_lbutton_up_message +=
            HexPtr(module_base + kInvSlotWndHandleLButtonUpTargetRva);
        invslot_handle_lbutton_up_message += L" target_rva=";
        invslot_handle_lbutton_up_message += Hex32(kInvSlotWndHandleLButtonUpTargetRva);
        monomyth::logger::Log(invslot_handle_lbutton_up_message);
    }

    std::array<std::uint8_t, kInvSlotWndHandleLButtonUpEntryBytes.size()>
        live_invslot_handle_lbutton_up_afterheld_entry = {};
    const bool invslot_handle_lbutton_up_afterheld_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kInvSlotWndHandleLButtonUpAfterHeldTargetRva),
        live_invslot_handle_lbutton_up_afterheld_entry.size(),
        live_invslot_handle_lbutton_up_afterheld_entry.data());
    const bool invslot_handle_lbutton_up_afterheld_entry_matches =
        invslot_handle_lbutton_up_afterheld_entry_copied &&
        std::memcmp(
            live_invslot_handle_lbutton_up_afterheld_entry.data(),
            kInvSlotWndHandleLButtonUpEntryBytes.data(),
            kInvSlotWndHandleLButtonUpEntryBytes.size()) == 0;
    if (!invslot_handle_lbutton_up_afterheld_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: invslot wnd handle left button up afterheld trace denied target=0x699c30 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(
                       module_base + kInvSlotWndHandleLButtonUpAfterHeldTargetRva),
                   reinterpret_cast<void*>(&InvSlotWndHandleLButtonUpAfterHeldHook),
                   &g_invslot_wnd_handle_lbutton_up_afterheld_detour,
                   reinterpret_cast<void**>(&g_original_invslot_wnd_handle_lbutton_up_afterheld),
                   L"CInvSlotWnd::HandleLButtonUpAfterHeld trace")) {
        RemoveInlineDetour(&g_invslot_wnd_handle_lbutton_up_afterheld_detour);
        g_original_invslot_wnd_handle_lbutton_up_afterheld = nullptr;
    } else {
        std::wstring invslot_handle_lbutton_up_afterheld_message =
            L"hook_manager: invslot wnd handle left button up afterheld trace installed address=";
        invslot_handle_lbutton_up_afterheld_message +=
            HexPtr(module_base + kInvSlotWndHandleLButtonUpAfterHeldTargetRva);
        invslot_handle_lbutton_up_afterheld_message += L" target_rva=";
        invslot_handle_lbutton_up_afterheld_message +=
            Hex32(kInvSlotWndHandleLButtonUpAfterHeldTargetRva);
        monomyth::logger::Log(invslot_handle_lbutton_up_afterheld_message);
    }

    constexpr std::array<std::uint8_t, 8> kInvSlotHandleLButtonCoreEntryBytes = {
        0x6a, 0xff, 0x68, 0x1d, 0xa8, 0x98, 0x00, 0x64};
    std::array<std::uint8_t, kInvSlotHandleLButtonCoreEntryBytes.size()>
        live_invslot_handle_lbutton_core_entry = {};
    const bool invslot_handle_lbutton_core_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kInvSlotHandleLButtonCoreTargetRva),
        live_invslot_handle_lbutton_core_entry.size(),
        live_invslot_handle_lbutton_core_entry.data());
    const bool invslot_handle_lbutton_core_entry_matches =
        invslot_handle_lbutton_core_entry_copied &&
        std::memcmp(
            live_invslot_handle_lbutton_core_entry.data(),
            kInvSlotHandleLButtonCoreEntryBytes.data(),
            kInvSlotHandleLButtonCoreEntryBytes.size()) == 0;
    if (!invslot_handle_lbutton_core_entry_matches) {
        monomyth::logger::Log(
            L"hook_manager: invslot handle left button core trace denied target=0x695670 validation=entry_bytes_mismatch");
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreTargetRva),
                   reinterpret_cast<void*>(&InvSlotHandleLButtonCoreHook),
                   &g_invslot_handle_lbutton_core_detour,
                   reinterpret_cast<void**>(&g_original_invslot_handle_lbutton_core),
                   L"CInvSlot::HandleLButtonUp core trace")) {
        RemoveInlineDetour(&g_invslot_handle_lbutton_core_detour);
        g_original_invslot_handle_lbutton_core = nullptr;
    } else {
        std::wstring invslot_handle_lbutton_core_message =
            L"hook_manager: invslot handle left button core trace installed address=";
        invslot_handle_lbutton_core_message +=
            HexPtr(module_base + kInvSlotHandleLButtonCoreTargetRva);
        invslot_handle_lbutton_core_message += L" target_rva=";
        invslot_handle_lbutton_core_message += Hex32(kInvSlotHandleLButtonCoreTargetRva);
        invslot_handle_lbutton_core_message += L" filtered_caller_return_rva=";
        invslot_handle_lbutton_core_message +=
            Hex32(kInvSlotHandleLButtonCoreCallerReturnRva);
        monomyth::logger::Log(invslot_handle_lbutton_core_message);
    }

    constexpr std::array<std::uint8_t, 8> kInventoryWindowWndNotificationEntryBytes = {
        0x6a, 0xff, 0x68, 0xa8, 0xa3, 0x98, 0x00, 0x64};
    std::array<std::uint8_t, kInventoryWindowWndNotificationEntryBytes.size()>
        live_inventory_wnd_notification_entry = {};
    const bool inventory_wnd_notification_entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(module_base + kInventoryWindowWndNotificationTargetRva),
        live_inventory_wnd_notification_entry.size(),
        live_inventory_wnd_notification_entry.data());
    const bool inventory_wnd_notification_entry_matches =
        inventory_wnd_notification_entry_copied &&
        std::memcmp(
            live_inventory_wnd_notification_entry.data(),
            kInventoryWindowWndNotificationEntryBytes.data(),
            kInventoryWindowWndNotificationEntryBytes.size()) == 0;
    if (!inventory_wnd_notification_entry_matches) {
        std::wstring inventory_wnd_notification_message =
            L"hook_manager: inventory window wnd notification trace denied target=InventoryWindowWndNotification validation=entry_bytes_mismatch expected=\"";
        inventory_wnd_notification_message += HexBytes(
            kInventoryWindowWndNotificationEntryBytes.data(),
            kInventoryWindowWndNotificationEntryBytes.size());
        inventory_wnd_notification_message += L"\" live=\"";
        inventory_wnd_notification_message += HexBytes(
            live_inventory_wnd_notification_entry.data(),
            live_inventory_wnd_notification_entry.size());
        inventory_wnd_notification_message += L"\" address=";
        inventory_wnd_notification_message +=
            HexPtr(module_base + kInventoryWindowWndNotificationTargetRva);
        inventory_wnd_notification_message += L" target_rva=";
        inventory_wnd_notification_message +=
            Hex32(kInventoryWindowWndNotificationTargetRva);
        monomyth::logger::Log(inventory_wnd_notification_message);
    } else if (!InstallInlineDetour(
                   reinterpret_cast<void*>(
                       module_base + kInventoryWindowWndNotificationTargetRva),
                   reinterpret_cast<void*>(&InventoryWindowWndNotificationHook),
                   &g_inventory_window_wnd_notification_detour,
                   reinterpret_cast<void**>(&g_original_inventory_window_wnd_notification),
                   L"CInventoryWindow::WndNotification trace")) {
        RemoveInlineDetour(&g_inventory_window_wnd_notification_detour);
        g_original_inventory_window_wnd_notification = nullptr;
    } else {
        std::wstring inventory_wnd_notification_message =
            L"hook_manager: inventory window wnd notification trace installed address=";
        inventory_wnd_notification_message +=
            HexPtr(module_base + kInventoryWindowWndNotificationTargetRva);
        inventory_wnd_notification_message += L" target_rva=";
        inventory_wnd_notification_message +=
            Hex32(kInventoryWindowWndNotificationTargetRva);
        monomyth::logger::Log(inventory_wnd_notification_message);
    }

    g_original_equip_record_lookup = reinterpret_cast<EquipRecordLookupFn>(
        module_base + kEquipRecordLookupTargetRva);
    g_original_equip_requirement_lookup = reinterpret_cast<EquipRequirementLookupFn>(
        module_base + kEquipRequirementLookupTargetRva);
    g_original_equip_nested_inventory_gate = reinterpret_cast<EquipNestedInventoryGateFn>(
        module_base + kEquipNestedInventoryGateTargetRva);
    g_original_equip_nested_validator = reinterpret_cast<EquipNestedValidatorFn>(
        module_base + kEquipNestedValidatorTargetRva);
    g_original_equip_message_resolver = reinterpret_cast<EquipMessageResolverFn>(
        module_base + kEquipMessageResolverTargetRva);
    g_original_drag_drop_silent_precheck = reinterpret_cast<DragDropSilentPrecheckFn>(
        module_base + kDragDropSilentPrecheckTargetRva);
    g_original_move_item_slot_resolve = reinterpret_cast<MoveItemSlotResolveFn>(
        module_base + kMoveItemSlotResolveTargetRva);
    g_original_move_item_branch_kind = reinterpret_cast<MoveItemBranchKindFn>(
        module_base + kMoveItemBranchKindTargetRva);
    g_original_move_item_branch_bool = reinterpret_cast<MoveItemBranchBoolFn>(
        module_base + kMoveItemBranchBoolTargetRva);
    g_original_move_item_stack_local_gate = reinterpret_cast<MoveItemStackLocalGateFn>(
        module_base + kMoveItemStackLocalGateTargetRva);
    g_original_invslot_handle_lbutton_core_precheck =
        reinterpret_cast<InvSlotHandleLButtonCorePrecheckFn>(
            module_base + kInvSlotHandleLButtonCorePrecheckTargetRva);
    g_original_invslot_handle_lbutton_core_mode_bits =
        reinterpret_cast<InvSlotHandleLButtonCoreModeBitsFn>(
            module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva);
    g_original_invslot_handle_lbutton_core_local_gate =
        reinterpret_cast<InvSlotHandleLButtonCoreLocalGateFn>(
            module_base + kInvSlotHandleLButtonCoreLocalGateTargetRva);
    g_original_invslot_handle_lbutton_core_resolve_object =
        reinterpret_cast<InvSlotHandleLButtonCoreResolveObjectFn>(
            module_base + kInvSlotHandleLButtonCoreResolveObjectTargetRva);
    g_original_invslot_handle_lbutton_core_resolved_kind = reinterpret_cast<MoveItemBranchKindFn>(
        module_base + kInvSlotHandleLButtonCoreResolvedKindTargetRva);
    g_original_invslot_handle_lbutton_core_resolved_flag =
        reinterpret_cast<InvSlotHandleLButtonCoreResolvedFlagFn>(
            module_base + kInvSlotHandleLButtonCoreResolvedFlagTargetRva);
    g_original_invslot_handle_lbutton_core_alt_dispatch =
        reinterpret_cast<InvSlotHandleLButtonCoreAltDispatchFn>(
            module_base + kInvSlotHandleLButtonCoreAltDispatchTargetRva);
    g_original_invslot_handle_lbutton_core_slot17_message =
        reinterpret_cast<InvSlotHandleLButtonCoreSlot17MessageFn>(
            module_base + kInvSlotHandleLButtonCoreSlot17MessageTargetRva);
    g_original_invslot_handle_lbutton_core_global_action49 =
        reinterpret_cast<InvSlotHandleLButtonCoreGlobalAction49Fn>(
            module_base + kInvSlotHandleLButtonCoreGlobalAction49TargetRva);
    g_original_invslot_handle_lbutton_core_item_range_gate =
        reinterpret_cast<InvSlotHandleLButtonCoreItemRangeGateFn>(
            module_base + kInvSlotHandleLButtonCoreItemRangeGateTargetRva);
    g_original_invslot_handle_lbutton_core_post_resolve_gate =
        reinterpret_cast<InvSlotHandleLButtonCorePostResolveGateFn>(
            module_base + kInvSlotHandleLButtonCorePostResolveGateTargetRva);
    g_original_invslot_handle_lbutton_core_post_lookup_mode_gate =
        reinterpret_cast<InvSlotHandleLButtonCorePostLookupModeGateFn>(
            module_base + kInvSlotHandleLButtonCorePostLookupModeGateTargetRva);
    g_original_invslot_handle_lbutton_core_post_lookup_ui_pulse =
        reinterpret_cast<InvSlotHandleLButtonCorePostLookupUiPulseFn>(
            module_base + kInvSlotHandleLButtonCorePostLookupUiPulseTargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_gate_a =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchGateAFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchGateATargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_prep =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchPrepFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchPrepTargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_gate_b =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchGateBFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchGateBTargetRva);
    g_original_invslot_handle_lbutton_core_late_branch_dispatch =
        reinterpret_cast<InvSlotHandleLButtonCoreLateBranchDispatchFn>(
            module_base + kInvSlotHandleLButtonCoreLateBranchDispatchTargetRva);
    g_original_invslot_handle_lbutton_core_late_slot17_gate =
        reinterpret_cast<InvSlotHandleLButtonCoreLateSlot17GateFn>(
            module_base + kInvSlotHandleLButtonCoreLateSlot17GateTargetRva);
    g_original_invslot_handle_lbutton_core_late_slot17_apply =
        reinterpret_cast<InvSlotHandleLButtonCoreLateSlot17ApplyFn>(
            module_base + kInvSlotHandleLButtonCoreLateSlot17ApplyTargetRva);
    g_original_move_item_ctor = reinterpret_cast<MoveItemCtorFn>(
        module_base + kMoveItemCtorTargetRva);
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipClickRecordLookupCallsiteRva),
        reinterpret_cast<void*>(&EquipClickRecordLookupCallsiteHook),
        module_base + kEquipRecordLookupTargetRva,
        &g_equip_click_record_lookup_callsite_patch,
        L"EquipClickRecordLookupCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipClickCanEquipCallsiteRva),
        reinterpret_cast<void*>(&EquipClickCanEquipCallsiteHook),
        manifest.can_equip_address,
        &g_equip_click_can_equip_callsite_patch,
        L"EquipClickCanEquipCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipClickRequirementLookupCallsiteRva),
        reinterpret_cast<void*>(&EquipClickRequirementLookupCallsiteHook),
        module_base + kEquipRequirementLookupTargetRva,
        &g_equip_click_requirement_lookup_callsite_patch,
        L"EquipClickRequirementLookupCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipLocalRecordLookupCallsiteRva),
        reinterpret_cast<void*>(&EquipLocalRecordLookupCallsiteHook),
        module_base + kEquipRecordLookupTargetRva,
        &g_equip_local_record_lookup_callsite_patch,
        L"EquipLocalRecordLookupCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipLocalRequirementLookupACallsiteRva),
        reinterpret_cast<void*>(&EquipLocalRequirementLookupACallsiteHook),
        module_base + kEquipRequirementLookupTargetRva,
        &g_equip_local_requirement_lookup_a_callsite_patch,
        L"EquipLocalRequirementLookupACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipLocalRequirementLookupBCallsiteRva),
        reinterpret_cast<void*>(&EquipLocalRequirementLookupBCallsiteHook),
        module_base + kEquipRequirementLookupTargetRva,
        &g_equip_local_requirement_lookup_b_callsite_patch,
        L"EquipLocalRequirementLookupBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipNestedInventoryGateCallsiteRva),
        reinterpret_cast<void*>(&EquipNestedInventoryGateCallsiteHook),
        module_base + kEquipNestedInventoryGateTargetRva,
        &g_equip_nested_inventory_gate_callsite_patch,
        L"EquipNestedInventoryGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipNestedValidatorCallsiteRva),
        reinterpret_cast<void*>(&EquipNestedValidatorCallsiteHook),
        module_base + kEquipNestedValidatorTargetRva,
        &g_equip_nested_validator_callsite_patch,
        L"EquipNestedValidatorCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kEquipLocalRejectMessageCallsiteRva),
        reinterpret_cast<void*>(&EquipLocalRejectMessageCallsiteHook),
        module_base + kEquipMessageResolverTargetRva,
        &g_equip_local_reject_message_callsite_patch,
        L"EquipLocalRejectMessageCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kDragDropLocalRejectMessageCallsiteRva),
        reinterpret_cast<void*>(&DragDropLocalRejectMessageCallsiteHook),
        module_base + kEquipMessageResolverTargetRva,
        &g_drag_drop_local_reject_message_callsite_patch,
        L"DragDropLocalRejectMessageCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kDragDropSilentPrecheckCallsiteRva),
        reinterpret_cast<void*>(&DragDropSilentPrecheckCallsiteHook),
        module_base + kDragDropSilentPrecheckTargetRva,
        &g_drag_drop_silent_precheck_callsite_patch,
        L"DragDropSilentPrecheckCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemFromSlotResolveCallsiteRva),
        reinterpret_cast<void*>(&MoveItemFromSlotResolveCallsiteHook),
        module_base + kMoveItemSlotResolveTargetRva,
        &g_move_item_from_slot_resolve_callsite_patch,
        L"MoveItemFromSlotResolveCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemToSlotResolveCallsiteRva),
        reinterpret_cast<void*>(&MoveItemToSlotResolveCallsiteHook),
        module_base + kMoveItemSlotResolveTargetRva,
        &g_move_item_to_slot_resolve_callsite_patch,
        L"MoveItemToSlotResolveCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemBranchKindSiteACallsiteRva),
        reinterpret_cast<void*>(&MoveItemBranchKindSiteACallsiteHook),
        module_base + kMoveItemBranchKindTargetRva,
        &g_move_item_branch_kind_site_a_callsite_patch,
        L"MoveItemBranchKindSiteACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemBranchKindSiteBCallsiteRva),
        reinterpret_cast<void*>(&MoveItemBranchKindSiteBCallsiteHook),
        module_base + kMoveItemBranchKindTargetRva,
        &g_move_item_branch_kind_site_b_callsite_patch,
        L"MoveItemBranchKindSiteBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemBranchBoolSiteACallsiteRva),
        reinterpret_cast<void*>(&MoveItemBranchBoolSiteACallsiteHook),
        module_base + kMoveItemBranchBoolTargetRva,
        &g_move_item_branch_bool_site_a_callsite_patch,
        L"MoveItemBranchBoolSiteACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemBranchBoolSiteBCallsiteRva),
        reinterpret_cast<void*>(&MoveItemBranchBoolSiteBCallsiteHook),
        module_base + kMoveItemBranchBoolTargetRva,
        &g_move_item_branch_bool_site_b_callsite_patch,
        L"MoveItemBranchBoolSiteBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemStackLocalGateSiteBCallsiteRva),
        reinterpret_cast<void*>(&MoveItemStackLocalGateSiteBCallsiteHook),
        module_base + kMoveItemStackLocalGateTargetRva,
        &g_move_item_stack_local_gate_site_b_callsite_patch,
        L"MoveItemStackLocalGateSiteBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePrecheckCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePrecheckCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_precheck_callsite_patch,
        L"InvSlotHandleLButtonCorePrecheckCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_a_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsBCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_b_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsCCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsCCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_c_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsCCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsDCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsDCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_d_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsDCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLocalGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLocalGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLocalGateTargetRva,
        &g_invslot_handle_lbutton_core_local_gate_callsite_patch,
        L"InvSlotHandleLButtonCoreLocalGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolveObjectCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolveObjectCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolveObjectTargetRva,
        &g_invslot_handle_lbutton_core_resolve_object_callsite_patch,
        L"InvSlotHandleLButtonCoreResolveObjectCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedKindACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedKindACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedKindTargetRva,
        &g_invslot_handle_lbutton_core_resolved_kind_a_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedKindACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedKindBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedKindBCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedKindTargetRva,
        &g_invslot_handle_lbutton_core_resolved_kind_b_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedKindBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedFlagACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedFlagACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedFlagTargetRva,
        &g_invslot_handle_lbutton_core_resolved_flag_a_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedFlagACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreResolvedFlagBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreResolvedFlagBCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreResolvedFlagTargetRva,
        &g_invslot_handle_lbutton_core_resolved_flag_b_callsite_patch,
        L"InvSlotHandleLButtonCoreResolvedFlagBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreModeBitsECallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreModeBitsECallsiteHook),
        module_base + kInvSlotHandleLButtonCoreModeBitsTargetRva,
        &g_invslot_handle_lbutton_core_mode_bits_e_callsite_patch,
        L"InvSlotHandleLButtonCoreModeBitsECallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreAltDispatchCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreAltDispatchCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreAltDispatchTargetRva,
        &g_invslot_handle_lbutton_core_alt_dispatch_callsite_patch,
        L"InvSlotHandleLButtonCoreAltDispatchCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreSlot17MessageCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreSlot17MessageCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreSlot17MessageTargetRva,
        &g_invslot_handle_lbutton_core_slot17_message_callsite_patch,
        L"InvSlotHandleLButtonCoreSlot17MessageCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreGlobalAction49CallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreGlobalAction49CallsiteHook),
        module_base + kInvSlotHandleLButtonCoreGlobalAction49TargetRva,
        &g_invslot_handle_lbutton_core_global_action49_callsite_patch,
        L"InvSlotHandleLButtonCoreGlobalAction49Callsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreItemRangeGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreItemRangeGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreItemRangeGateTargetRva,
        &g_invslot_handle_lbutton_core_item_range_gate_callsite_patch,
        L"InvSlotHandleLButtonCoreItemRangeGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePostResolveGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePostResolveGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePostResolveGateTargetRva,
        &g_invslot_handle_lbutton_core_post_resolve_gate_callsite_patch,
        L"InvSlotHandleLButtonCorePostResolveGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateSlot17GateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateSlot17GateCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateSlot17GateTargetRva,
        &g_invslot_handle_lbutton_core_late_slot17_gate_callsite_patch,
        L"InvSlotHandleLButtonCoreLateSlot17GateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateSlot17ApplyCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateSlot17ApplyCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateSlot17ApplyTargetRva,
        &g_invslot_handle_lbutton_core_late_slot17_apply_callsite_patch,
        L"InvSlotHandleLButtonCoreLateSlot17ApplyCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreManagerPrecheckACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreManagerPrecheckACallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_manager_precheck_a_callsite_patch,
        L"InvSlotHandleLButtonCoreManagerPrecheckACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreManagerPrecheckBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreManagerPrecheckBCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_manager_precheck_b_callsite_patch,
        L"InvSlotHandleLButtonCoreManagerPrecheckBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreManagerPrecheckCCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreManagerPrecheckCCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_manager_precheck_c_callsite_patch,
        L"InvSlotHandleLButtonCoreManagerPrecheckCCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreManagerPrecheckDCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreManagerPrecheckDCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_manager_precheck_d_callsite_patch,
        L"InvSlotHandleLButtonCoreManagerPrecheckDCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreManagerPrecheckECallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreManagerPrecheckECallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_manager_precheck_e_callsite_patch,
        L"InvSlotHandleLButtonCoreManagerPrecheckECallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreManagerPrecheckFCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreManagerPrecheckFCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePrecheckTargetRva,
        &g_invslot_handle_lbutton_core_manager_precheck_f_callsite_patch,
        L"InvSlotHandleLButtonCoreManagerPrecheckFCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePostLookupModeGateCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePostLookupModeGateCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePostLookupModeGateTargetRva,
        &g_invslot_handle_lbutton_core_post_lookup_mode_gate_callsite_patch,
        L"InvSlotHandleLButtonCorePostLookupModeGateCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCorePostLookupUiPulseCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCorePostLookupUiPulseCallsiteHook),
        module_base + kInvSlotHandleLButtonCorePostLookupUiPulseTargetRva,
        &g_invslot_handle_lbutton_core_post_lookup_ui_pulse_callsite_patch,
        L"InvSlotHandleLButtonCorePostLookupUiPulseCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchGateACallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchGateACallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateBranchGateATargetRva,
        &g_invslot_handle_lbutton_core_late_branch_gate_a_callsite_patch,
        L"InvSlotHandleLButtonCoreLateBranchGateACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchPrepCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchPrepCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateBranchPrepTargetRva,
        &g_invslot_handle_lbutton_core_late_branch_prep_callsite_patch,
        L"InvSlotHandleLButtonCoreLateBranchPrepCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchGateBCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchGateBCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateBranchGateBTargetRva,
        &g_invslot_handle_lbutton_core_late_branch_gate_b_callsite_patch,
        L"InvSlotHandleLButtonCoreLateBranchGateBCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kInvSlotHandleLButtonCoreLateBranchDispatchCallsiteRva),
        reinterpret_cast<void*>(&InvSlotHandleLButtonCoreLateBranchDispatchCallsiteHook),
        module_base + kInvSlotHandleLButtonCoreLateBranchDispatchTargetRva,
        &g_invslot_handle_lbutton_core_late_branch_dispatch_callsite_patch,
        L"InvSlotHandleLButtonCoreLateBranchDispatchCallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemCtorSiteACallsiteRva),
        reinterpret_cast<void*>(&MoveItemCtorSiteACallsiteHook),
        module_base + kMoveItemCtorTargetRva,
        &g_move_item_ctor_site_a_callsite_patch,
        L"MoveItemCtorSiteACallsite");
    InstallCallsitePatch(
        reinterpret_cast<void*>(module_base + kMoveItemCtorSiteBCallsiteRva),
        reinterpret_cast<void*>(&MoveItemCtorSiteBCallsiteHook),
        module_base + kMoveItemCtorTargetRva,
        &g_move_item_ctor_site_b_callsite_patch,
        L"MoveItemCtorSiteBCallsite");
    return true;
}

bool InstallActivatedSkillVisibilityHook(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.multiclass_skill_visibility_allowed) {
        return false;
    }

    const std::uintptr_t module_base = GetHostModuleBase();
    if (module_base == 0) {
        monomyth::logger::Log(
            L"hook_manager: activated skill visibility hook denied reason=\"module_base_unavailable\"");
        return false;
    }

    constexpr std::array<std::uint8_t, 16> kCharacterZoneClientHasSkillEntryBytes = {
        0x8b, 0x44, 0x24, 0x04, 0x50, 0xe8, 0x26, 0xfe,
        0xff, 0xff, 0x33, 0xc9, 0x85, 0xc0, 0x0f, 0x9f};
    std::array<std::uint8_t, kCharacterZoneClientHasSkillEntryBytes.size()>
        live_character_zone_client_has_skill_entry = {};
    const std::uintptr_t target_address = module_base + kCharacterZoneClientHasSkillRva;
    const bool entry_copied = TryCopyBytes(
        reinterpret_cast<const void*>(target_address),
        live_character_zone_client_has_skill_entry.size(),
        live_character_zone_client_has_skill_entry.data());
    const bool entry_matches =
        entry_copied &&
        std::memcmp(
            live_character_zone_client_has_skill_entry.data(),
            kCharacterZoneClientHasSkillEntryBytes.data(),
            kCharacterZoneClientHasSkillEntryBytes.size()) == 0;
    if (!entry_matches) {
        std::wstring message =
            L"hook_manager: activated skill visibility hook denied target=CharacterZoneClient::HasSkill expected=\"";
        message += HexBytes(
            kCharacterZoneClientHasSkillEntryBytes.data(),
            kCharacterZoneClientHasSkillEntryBytes.size());
        message += L"\" live=\"";
        message += HexBytes(
            live_character_zone_client_has_skill_entry.data(),
            live_character_zone_client_has_skill_entry.size());
        message += L"\" address=";
        message += HexPtr(target_address);
        message += L" target_rva=";
        message += Hex32(kCharacterZoneClientHasSkillRva);
        monomyth::logger::Log(message);
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(target_address),
            reinterpret_cast<void*>(&CharacterZoneClientHasSkillVisibilityHook),
            &g_character_zone_client_has_skill_detour,
            reinterpret_cast<void**>(&g_original_character_zone_client_has_skill),
            L"CharacterZoneClient::HasSkill activated skill visibility")) {
        RemoveInlineDetour(&g_character_zone_client_has_skill_detour);
        g_original_character_zone_client_has_skill = nullptr;
        return false;
    }

    g_character_zone_client_has_skill_address = target_address;
    g_character_zone_client_has_skill = g_original_character_zone_client_has_skill;
    g_multiclass_skill_visibility_enabled = true;
    g_skill_visibility_override_count = 0;

    std::wstring message =
        L"hook_manager: activated skill visibility hook installed target=CharacterZoneClient::HasSkill address=";
    message += HexPtr(target_address);
    message += L" target_rva=";
    message += Hex32(kCharacterZoneClientHasSkillRva);
    monomyth::logger::Log(message);
    return true;
}

bool InstallInvSlotMgrMoveItemTrace(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.multiclass_item_usability_allowed ||
        manifest.inv_slot_mgr_move_item_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.inv_slot_mgr_move_item_address == 0) {
        if (manifest.multiclass_item_usability_allowed) {
            std::wstring message = L"hook_manager: move item trace denied ";
            message += FormatDiscoveryDetails(
                L"CInvSlotMgr::MoveItem",
                manifest.inv_slot_mgr_move_item_evidence_source,
                manifest.inv_slot_mgr_move_item_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.inv_slot_mgr_move_item_address),
            reinterpret_cast<void*>(&InvSlotMgrMoveItemHook),
            &g_inv_slot_mgr_move_item_detour,
            reinterpret_cast<void**>(&g_original_inv_slot_mgr_move_item),
            L"CInvSlotMgr::MoveItem trace")) {
        RemoveInlineDetour(&g_inv_slot_mgr_move_item_detour);
        g_original_inv_slot_mgr_move_item = nullptr;
        return false;
    }

    g_inv_slot_mgr_move_item_address = manifest.inv_slot_mgr_move_item_address;
    std::wstring message =
        L"hook_manager: move item trace installed target=CInvSlotMgr::MoveItem address=";
    message += HexPtr(manifest.inv_slot_mgr_move_item_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallCanStartMemmingTrace(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.can_start_memming_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.can_start_memming_address == 0) {
        if (manifest.spell_usability_trace_dev_opt_in) {
            std::wstring message = L"hook_manager: spell usability trace denied ";
            message += FormatDiscoveryDetails(
                L"CanStartMemming",
                manifest.can_start_memming_evidence_source,
                manifest.can_start_memming_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.can_start_memming_address),
            reinterpret_cast<void*>(&CanStartMemmingHook),
            &g_can_start_memming_detour,
            reinterpret_cast<void**>(&g_original_can_start_memming),
            L"CanStartMemming trace")) {
        RemoveInlineDetour(&g_can_start_memming_detour);
        g_original_can_start_memming = nullptr;
        return false;
    }

    std::wstring message = L"hook_manager: spell usability trace installed target=CanStartMemming address=";
    message += HexPtr(manifest.can_start_memming_address);
    monomyth::logger::Log(message);
    g_can_start_memming_trace_count = 0;
    return true;
}

bool InstallSpellbookMemorizeSendPathTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.memorize_send_trace_allowed ||
        manifest.spellbook_memorize_send_path_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.spellbook_memorize_send_path_address == 0) {
        if (manifest.memorize_send_trace_dev_opt_in) {
            std::wstring message =
                L"hook_manager: spellbook memorize send path trace denied ";
            message += FormatDiscoveryDetails(
                L"SpellbookMemorizeSendPath",
                manifest.spellbook_memorize_send_path_evidence_source,
                manifest.spellbook_memorize_send_path_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.spellbook_memorize_send_path_address),
            reinterpret_cast<void*>(&SpellbookMemorizeSendPathHook),
            &g_spellbook_memorize_send_path_detour,
            reinterpret_cast<void**>(&g_original_spellbook_memorize_send_path),
            L"SpellbookMemorizeSendPath trace")) {
        RemoveInlineDetour(&g_spellbook_memorize_send_path_detour);
        g_original_spellbook_memorize_send_path = nullptr;
        return false;
    }

    g_spellbook_memorize_send_path_address =
        manifest.spellbook_memorize_send_path_address;
    std::wstring message =
        L"hook_manager: spellbook memorize send path trace installed target=SpellbookMemorizeSendPath address=";
    message += HexPtr(manifest.spellbook_memorize_send_path_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallSpellbookDispatcherTrace(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.spellbook_dispatcher_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.spellbook_dispatcher_address == 0) {
        if (manifest.spell_usability_trace_dev_opt_in) {
            std::wstring message = L"hook_manager: spellbook dispatcher trace denied ";
            message += FormatDiscoveryDetails(
                L"SpellbookDispatcher",
                manifest.spellbook_dispatcher_evidence_source,
                manifest.spellbook_dispatcher_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.spellbook_dispatcher_address),
            reinterpret_cast<void*>(&SpellbookDispatcherHook),
            &g_spellbook_dispatcher_detour,
            reinterpret_cast<void**>(&g_original_spellbook_dispatcher),
            L"SpellbookDispatcher trace")) {
        RemoveInlineDetour(&g_spellbook_dispatcher_detour);
        g_original_spellbook_dispatcher = nullptr;
        return false;
    }

    g_spellbook_dispatcher_address = manifest.spellbook_dispatcher_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=SpellbookDispatcher address=";
    message += HexPtr(manifest.spellbook_dispatcher_address);
    monomyth::logger::Log(message);
    g_spellbook_ui_active_correlation_id = 0;
    g_spellbook_ui_correlation_count = 0;
    return true;
}

bool InstallStartSpellScribePathTrace(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.start_spell_scribe_path_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_path_address == 0) {
        if (manifest.spell_usability_trace_dev_opt_in) {
            std::wstring message = L"hook_manager: spellbook scribe path trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePath",
                manifest.start_spell_scribe_path_evidence_source,
                manifest.start_spell_scribe_path_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_path_address),
            reinterpret_cast<void*>(&StartSpellScribePathHook),
            &g_start_spell_scribe_path_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_path),
            L"StartSpellScribePath trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_path_detour);
        g_original_start_spell_scribe_path = nullptr;
        return false;
    }

    g_start_spell_scribe_path_address = manifest.start_spell_scribe_path_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePath address=";
    message += HexPtr(manifest.start_spell_scribe_path_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckModeGetterTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.start_spell_scribe_precheck_mode_getter_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_mode_getter_address == 0) {
        if (manifest.spell_usability_trace_dev_opt_in) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck mode getter trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckModeGetter",
                manifest.start_spell_scribe_precheck_mode_getter_evidence_source,
                manifest.start_spell_scribe_precheck_mode_getter_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_mode_getter_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckModeGetterHook),
            &g_start_spell_scribe_precheck_mode_getter_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_mode_getter),
            L"StartSpellScribePrecheckModeGetter trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_mode_getter_detour);
        g_original_start_spell_scribe_precheck_mode_getter = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_mode_getter_address =
        manifest.start_spell_scribe_precheck_mode_getter_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckModeGetter address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_mode_getter_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckGateTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) ||
        manifest.start_spell_scribe_precheck_gate_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_gate_address == 0) {
        if (manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck gate trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckGate",
                manifest.start_spell_scribe_precheck_gate_evidence_source,
                manifest.start_spell_scribe_precheck_gate_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_gate_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckGateHook),
            &g_start_spell_scribe_precheck_gate_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_gate),
            L"StartSpellScribePrecheckGate trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_gate_detour);
        g_original_start_spell_scribe_precheck_gate = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_gate_address =
        manifest.start_spell_scribe_precheck_gate_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckGate address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_gate_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckLookupTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.start_spell_scribe_precheck_lookup_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_lookup_address == 0) {
        if (manifest.spell_usability_trace_dev_opt_in) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck lookup trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckLookup",
                manifest.start_spell_scribe_precheck_lookup_evidence_source,
                manifest.start_spell_scribe_precheck_lookup_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_lookup_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckLookupHook),
            &g_start_spell_scribe_precheck_lookup_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_lookup),
            L"StartSpellScribePrecheckLookup trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_lookup_detour);
        g_original_start_spell_scribe_precheck_lookup = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_lookup_address =
        manifest.start_spell_scribe_precheck_lookup_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckLookup address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_lookup_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckFastAcceptTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.start_spell_scribe_precheck_fast_accept_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_fast_accept_address == 0) {
        if (manifest.spell_usability_trace_dev_opt_in) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck fast-accept trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckFastAccept",
                manifest.start_spell_scribe_precheck_fast_accept_evidence_source,
                manifest.start_spell_scribe_precheck_fast_accept_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_fast_accept_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckFastAcceptHook),
            &g_start_spell_scribe_precheck_fast_accept_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_fast_accept),
            L"StartSpellScribePrecheckFastAccept trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_fast_accept_detour);
        g_original_start_spell_scribe_precheck_fast_accept = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_fast_accept_address =
        manifest.start_spell_scribe_precheck_fast_accept_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckFastAccept address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_fast_accept_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckClassResolverTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.start_spell_scribe_precheck_class_resolver_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_class_resolver_address == 0) {
        if (manifest.spell_usability_trace_dev_opt_in) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck class resolver trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckClassResolver",
                manifest.start_spell_scribe_precheck_class_resolver_evidence_source,
                manifest.start_spell_scribe_precheck_class_resolver_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_class_resolver_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckClassResolverHook),
            &g_start_spell_scribe_precheck_class_resolver_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_class_resolver),
            L"StartSpellScribePrecheckClassResolver trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_class_resolver_detour);
        g_original_start_spell_scribe_precheck_class_resolver = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_class_resolver_address =
        manifest.start_spell_scribe_precheck_class_resolver_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckClassResolver address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_class_resolver_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckAssignedMaskGetterTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) ||
        manifest.start_spell_scribe_precheck_assigned_mask_getter_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_assigned_mask_getter_address == 0) {
        if (manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck assigned-mask getter trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckAssignedMaskGetter",
                manifest.start_spell_scribe_precheck_assigned_mask_getter_evidence_source,
                manifest.start_spell_scribe_precheck_assigned_mask_getter_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_assigned_mask_getter_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckAssignedMaskGetterHook),
            &g_start_spell_scribe_precheck_assigned_mask_getter_detour,
            reinterpret_cast<void**>(
                &g_original_start_spell_scribe_precheck_assigned_mask_getter),
            L"StartSpellScribePrecheckAssignedMaskGetter trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_assigned_mask_getter_detour);
        g_original_start_spell_scribe_precheck_assigned_mask_getter = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_assigned_mask_getter_address =
        manifest.start_spell_scribe_precheck_assigned_mask_getter_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckAssignedMaskGetter address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_assigned_mask_getter_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckRule4462c0Trace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) ||
        manifest.start_spell_scribe_precheck_rule_4462c0_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_rule_4462c0_address == 0) {
        if (manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck rule 4462c0 trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckRule4462c0",
                manifest.start_spell_scribe_precheck_rule_4462c0_evidence_source,
                manifest.start_spell_scribe_precheck_rule_4462c0_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_rule_4462c0_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckRule4462c0Hook),
            &g_start_spell_scribe_precheck_rule_4462c0_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_rule_4462c0),
            L"StartSpellScribePrecheckRule4462c0 trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_4462c0_detour);
        g_original_start_spell_scribe_precheck_rule_4462c0 = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_rule_4462c0_address =
        manifest.start_spell_scribe_precheck_rule_4462c0_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckRule4462c0 address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_rule_4462c0_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckRule446190Trace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) ||
        manifest.start_spell_scribe_precheck_rule_446190_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_rule_446190_address == 0) {
        if (manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck rule 446190 trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckRule446190",
                manifest.start_spell_scribe_precheck_rule_446190_evidence_source,
                manifest.start_spell_scribe_precheck_rule_446190_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_rule_446190_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckRule446190Hook),
            &g_start_spell_scribe_precheck_rule_446190_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_rule_446190),
            L"StartSpellScribePrecheckRule446190 trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_446190_detour);
        g_original_start_spell_scribe_precheck_rule_446190 = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_rule_446190_address =
        manifest.start_spell_scribe_precheck_rule_446190_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckRule446190 address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_rule_446190_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckRule446200Trace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) ||
        manifest.start_spell_scribe_precheck_rule_446200_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_rule_446200_address == 0) {
        if (manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck rule 446200 trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckRule446200",
                manifest.start_spell_scribe_precheck_rule_446200_evidence_source,
                manifest.start_spell_scribe_precheck_rule_446200_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_rule_446200_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckRule446200Hook),
            &g_start_spell_scribe_precheck_rule_446200_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_rule_446200),
            L"StartSpellScribePrecheckRule446200 trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_446200_detour);
        g_original_start_spell_scribe_precheck_rule_446200 = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_rule_446200_address =
        manifest.start_spell_scribe_precheck_rule_446200_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckRule446200 address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_rule_446200_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellScribePrecheckRule446380Trace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!(manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) ||
        manifest.start_spell_scribe_precheck_rule_446380_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_scribe_precheck_rule_446380_address == 0) {
        if (manifest.spell_usability_trace_allowed ||
            manifest.multiclass_spell_usability_allowed) {
            std::wstring message =
                L"hook_manager: spellbook scribe precheck rule 446380 trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellScribePrecheckRule446380",
                manifest.start_spell_scribe_precheck_rule_446380_evidence_source,
                manifest.start_spell_scribe_precheck_rule_446380_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_scribe_precheck_rule_446380_address),
            reinterpret_cast<void*>(&StartSpellScribePrecheckRule446380Hook),
            &g_start_spell_scribe_precheck_rule_446380_detour,
            reinterpret_cast<void**>(&g_original_start_spell_scribe_precheck_rule_446380),
            L"StartSpellScribePrecheckRule446380 trace")) {
        RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_446380_detour);
        g_original_start_spell_scribe_precheck_rule_446380 = nullptr;
        return false;
    }

    g_start_spell_scribe_precheck_rule_446380_address =
        manifest.start_spell_scribe_precheck_rule_446380_address;
    std::wstring message =
        L"hook_manager: spell usability trace installed target=StartSpellScribePrecheckRule446380 address=";
    message += HexPtr(manifest.start_spell_scribe_precheck_rule_446380_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallStartSpellMemorizationPathTrace(
    const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.memorize_send_trace_allowed ||
        manifest.start_spell_memorization_path_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.start_spell_memorization_path_address == 0) {
        if (manifest.memorize_send_trace_dev_opt_in) {
            std::wstring message =
                L"hook_manager: start spell memorization path trace denied ";
            message += FormatDiscoveryDetails(
                L"StartSpellMemorizationPath",
                manifest.start_spell_memorization_path_evidence_source,
                manifest.start_spell_memorization_path_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.start_spell_memorization_path_address),
            reinterpret_cast<void*>(&StartSpellMemorizationPathHook),
            &g_start_spell_memorization_path_detour,
            reinterpret_cast<void**>(&g_original_start_spell_memorization_path),
            L"StartSpellMemorizationPath trace")) {
        RemoveInlineDetour(&g_start_spell_memorization_path_detour);
        g_original_start_spell_memorization_path = nullptr;
        return false;
    }

    g_start_spell_memorization_path_address =
        manifest.start_spell_memorization_path_address;
    std::wstring message =
        L"hook_manager: start spell memorization path trace installed target=StartSpellMemorizationPath address=";
    message += HexPtr(manifest.start_spell_memorization_path_address);
    monomyth::logger::Log(message);
    return true;
}

bool InstallMemorizeSendTrace(const monomyth::runtime::Manifest& manifest) noexcept {
    const bool enable_send_trace =
        manifest.memorize_send_trace_allowed ||
        manifest.multiclass_item_usability_allowed;
    const bool log_denial =
        manifest.memorize_send_trace_dev_opt_in ||
        manifest.multiclass_item_usability_allowed;
    if (!enable_send_trace ||
        manifest.memorize_send_packet_wrapper_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.memorize_send_packet_wrapper_address == 0) {
        if (log_denial) {
            std::wstring message = L"hook_manager: wrapper send trace denied ";
            message += FormatDiscoveryDetails(
                L"MemorizeSendPacketWrapper",
                manifest.memorize_send_packet_wrapper_evidence_source,
                manifest.memorize_send_packet_wrapper_failure_reason);
            monomyth::logger::Log(message);
        }
        return false;
    }

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.memorize_send_packet_wrapper_address),
            reinterpret_cast<void*>(&MemorizeSendPacketWrapperHook),
            &g_memorize_send_packet_wrapper_detour,
            reinterpret_cast<void**>(&g_original_memorize_send_packet_wrapper),
            L"MemorizeSendPacketWrapper trace")) {
        RemoveInlineDetour(&g_memorize_send_packet_wrapper_detour);
        g_original_memorize_send_packet_wrapper = nullptr;
        return false;
    }

    std::wstring message =
        L"hook_manager: wrapper send trace installed target=MemorizeSendPacketWrapper address=";
    message += HexPtr(manifest.memorize_send_packet_wrapper_address);
    message += L" memorize_focus=";
    message += manifest.memorize_send_trace_allowed ? L"true" : L"false";
    message += L" move_item_focus=";
    message += manifest.multiclass_item_usability_allowed ? L"true" : L"false";
    monomyth::logger::Log(message);
    g_memorize_send_packet_wrapper_address = manifest.memorize_send_packet_wrapper_address;
    g_memorize_send_trace_count = 0;
    g_memorize_send_pending_window = nullptr;
    g_memorize_send_pending_correlation_id = 0;
    g_memorize_send_correlation_count = 0;
    g_memorize_send_pending_wrapper_sends = 0;
    g_spellbook_scribe_pending_correlation_id = 0;
    g_spellbook_scribe_correlation_count = 0;
    g_spellbook_scribe_pending_wrapper_sends = 0;
    return true;
}

bool RemoveReceiveDispatchHook() noexcept {
    if (!g_receive_dispatch_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_receive_dispatch_detour)) {
        g_original_receive_dispatch = nullptr;
        monomyth::logger::Log(L"hook_manager: receive dispatcher hook removed");
        return true;
    }

    return false;
}

bool RemoveWhoClassNameDisplayHook() noexcept {
    bool ok = true;
    g_active_who_class_name_subject = nullptr;
    g_get_class_desc_seen_caller_rvas = {};
    g_get_class_desc_seen_caller_rva_count = 0;
    g_get_class_three_letter_seen_caller_rvas = {};
    g_get_class_three_letter_seen_caller_rva_count = 0;
    g_ui_class_producer_candidate_seen_caller_rvas = {};
    g_ui_class_producer_candidate_seen_caller_rva_count = 0;
    g_inventory_class_display_correlation_remaining.store(0);
    g_inventory_class_display_correlation_activation.store(0);
    g_inventory_class_display_correlation_notification_code.store(0);
    g_inventory_class_display_correlation_notification_caller_return.store(0);
    g_inventory_class_display_correlation_notification_caller_rva.store(0);
    g_inventory_class_display_correlation_sender_window.store(0);
    g_inventory_class_display_correlation_payload_like.store(0);
    g_char_select_native_class_substitution_remaining.store(0);
    g_char_select_native_class_substitution_activation.store(0);
    g_char_select_native_class_substitution_trace_count = 0;
    ok &= RemoveInlineDetour(&g_cxwnd_set_window_text_a_detour);
    ok &= RemoveInlineDetour(&g_get_deity_desc_detour);
    ok &= RemoveInlineDetour(&g_get_class_three_letter_code_detour);
    ok &= RemoveInlineDetour(&g_get_class_desc_detour);
    ok &= RemoveInlineDetour(&g_who_class_name_class_lookup_detour);
    ok &= RemoveInlineDetour(&g_who_class_name_detour);
    g_original_cxwnd_set_window_text_a = nullptr;
    g_original_get_class_three_letter_code = nullptr;
    g_original_get_class_desc = nullptr;
    g_original_get_deity_desc = nullptr;
    g_original_who_class_name = nullptr;
    g_original_who_class_name_class_lookup = nullptr;
    g_multiclass_ui_display_enabled = false;
    if (ok) {
        monomyth::logger::Log(
            L"hook_manager: multiclass UI display hook removed target=LeftClickedOnPlayerSurrogate/GetClassDesc/GetClassThreeLetterCode");
    }
    return ok;
}

bool RemoveCharacterListUpdateListHook() noexcept {
    g_character_list_window_pointer.store(0);
    g_character_list_last_forced_refresh_window.store(0);
    g_character_list_last_forced_refresh_mask.store(0);
    g_multiclass_cache_persist_pending.store(false);
    g_multiclass_cache_persist_pending_mask.store(0);
    g_character_list_update_list_trace_count = 0;
    g_character_list_forced_refresh_count = 0;
    if (!g_character_list_update_list_detour.installed) {
        g_original_character_list_update_list = nullptr;
        return true;
    }

    if (RemoveInlineDetour(&g_character_list_update_list_detour)) {
        g_original_character_list_update_list = nullptr;
        monomyth::logger::Log(
            L"hook_manager: character list refresh hook removed target=CCharacterListWnd::UpdateList");
        return true;
    }

    return false;
}

bool RemoveProgressionSelectionClassDisplayHook() noexcept {
    bool ok = true;
    g_char_select_class_name_func_trace_count = 0;
    g_char_select_late_full_name_trace_count = 0;
    ok &= RemoveInlineDetour(&g_char_select_class_name_func_detour);
    ok &= RemoveCallsitePatch(&g_progression_selection_class_lookup_callsite_patch);
    ok &= RemoveCallsitePatch(&g_char_select_late_full_name_callsite_a_patch);
    ok &= RemoveCallsitePatch(&g_char_select_late_full_name_callsite_b_patch);
    ok &= RemoveCallsitePatch(&g_char_select_late_full_name_callsite_c_patch);
    g_original_char_select_class_name_func = nullptr;
    g_original_progression_selection_class_lookup = nullptr;
    if (ok) {
        monomyth::logger::Log(
            L"hook_manager: progression selection class display hook removed target=ClassValueLabel/CharSelectClassNameFunc");
    }
    return ok;
}

bool RemoveInventoryClassTitleDisplayHook() noexcept {
    bool ok = true;
    const bool had_item_display_refresh_worker_trace =
        g_item_display_refresh_worker_trace_detour.installed;
    if (g_item_display_refresh_worker_trace_detour.installed &&
        RemoveInlineDetour(&g_item_display_refresh_worker_trace_detour)) {
        g_original_item_display_refresh_worker_trace = nullptr;
    } else if (g_item_display_refresh_worker_trace_detour.installed) {
        ok = false;
    }
    const bool had_item_display_class_row_builder =
        g_item_display_refresh_worker_detour.installed;
    if (g_item_display_refresh_worker_detour.installed &&
        RemoveInlineDetour(&g_item_display_refresh_worker_detour)) {
        g_original_item_display_class_row_builder = nullptr;
    } else if (g_item_display_refresh_worker_detour.installed) {
        ok = false;
    }
    g_item_display_class_display_correlation_remaining.store(0);
    g_item_display_class_display_correlation_activation.store(0);
    g_item_display_class_display_correlation_refresh_caller_return.store(0);
    g_item_display_class_display_correlation_refresh_caller_rva.store(0);
    g_item_display_class_display_correlation_window_this.store(0);
    g_item_display_class_display_correlation_arg1_like.store(0);
    g_item_display_class_display_correlation_arg2_like.store(0);
    g_item_display_refresh_worker_entry_trace_count = 0;
    if (ok && had_item_display_refresh_worker_trace) {
        monomyth::logger::Log(
            L"hook_manager: item display refresh worker trace removed target=ItemDisplayRefreshWorker");
    }
    if (ok && had_item_display_class_row_builder) {
        monomyth::logger::Log(
            L"hook_manager: item display class text trace removed target=ItemDisplayClassRowBuilder");
    }
    return ok;
}

bool RemoveGetSpellLevelNeededTrace() noexcept {
    if (!g_get_spell_level_needed_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_get_spell_level_needed_detour)) {
        g_original_get_spell_level_needed = nullptr;
        g_multiclass_spell_usability_enabled = false;
        monomyth::logger::Log(L"hook_manager: spell usability hook removed target=GetSpellLevelNeeded");
        return true;
    }

    return false;
}

bool RemoveScrollScribeTraceHooks() noexcept {
    bool ok = true;
    if (g_is_class_usable_predicate_detour.installed &&
        RemoveInlineDetour(&g_is_class_usable_predicate_detour)) {
        g_original_is_class_usable_predicate = nullptr;
    } else if (g_is_class_usable_predicate_detour.installed) {
        ok = false;
    }

    if (g_handle_rbutton_up_detour.installed &&
        RemoveInlineDetour(&g_handle_rbutton_up_detour)) {
        g_original_handle_rbutton_up = nullptr;
    } else if (g_handle_rbutton_up_detour.installed) {
        ok = false;
    }

    if (ok) {
        g_is_class_usable_predicate_override_count = 0;
        g_scroll_scribe_active_correlation_id = 0;
        g_scroll_scribe_active_logging = false;
        g_handle_rbutton_up_evidence_source = L"unknown";
        g_is_class_usable_predicate_evidence_source = L"unknown";
        monomyth::logger::Log(
            L"hook_manager: class usability hook removed target=EQ_Character::IsClassUsablePredicate");
    }
    return ok;
}

bool RemoveGuildTrainerHook() noexcept {
    if (!g_guild_trainer_class_lookup_callsite_patch.installed) {
        RestoreGuildTrainerSessionClassOverride(L"guild_trainer_hook_remove_inactive");
        g_original_guild_trainer_class_lookup = nullptr;
        g_guild_trainer_class_lookup_override_count = 0;
        g_pending_guild_trainer_session_class_id = 0;
        g_guild_trainer_class_lookup_override_record.fill(0);
        return true;
    }

    if (!RemoveCallsitePatch(&g_guild_trainer_class_lookup_callsite_patch)) {
        return false;
    }

    RestoreGuildTrainerSessionClassOverride(L"guild_trainer_hook_remove");
    g_original_guild_trainer_class_lookup = nullptr;
    g_guild_trainer_class_lookup_override_count = 0;
    g_pending_guild_trainer_session_class_id = 0;
    g_guild_trainer_class_lookup_override_record.fill(0);

    monomyth::logger::Log(
        L"hook_manager: guild trainer hook removed target=GuildTrainerClassLookupCallsite");
    return true;
}

bool RemoveActivatedSkillVisibilityHook() noexcept {
    if (!g_character_zone_client_has_skill_detour.installed) {
        g_original_character_zone_client_has_skill = nullptr;
        if (!g_multiclass_item_usability_enabled) {
            g_character_zone_client_has_skill = nullptr;
            g_character_zone_client_has_skill_address = 0;
        }
        g_multiclass_skill_visibility_enabled = false;
        g_skill_visibility_override_count = 0;
        return true;
    }

    if (RemoveInlineDetour(&g_character_zone_client_has_skill_detour)) {
        g_original_character_zone_client_has_skill = nullptr;
        g_character_zone_client_has_skill =
            g_multiclass_item_usability_enabled
                ? reinterpret_cast<CharacterZoneClientHasSkillFn>(
                    g_character_zone_client_has_skill_address)
                : nullptr;
        if (!g_multiclass_item_usability_enabled) {
            g_character_zone_client_has_skill_address = 0;
        }
        g_multiclass_skill_visibility_enabled = false;
        g_skill_visibility_override_count = 0;
        monomyth::logger::Log(
            L"hook_manager: activated skill visibility hook removed target=CharacterZoneClient::HasSkill");
        return true;
    }

    return false;
}

bool RemoveCanEquipHook() noexcept {
    if (g_move_item_validation_gate_detour.installed) {
        RemoveInlineDetour(&g_move_item_validation_gate_detour);
    }
    if (g_move_item_slot21_lookup_detour.installed) {
        RemoveInlineDetour(&g_move_item_slot21_lookup_detour);
    }
    if (g_inventory_summary_refresh_candidate_a_detour.installed) {
        RemoveInlineDetour(&g_inventory_summary_refresh_candidate_a_detour);
    }
    if (g_inventory_summary_refresh_candidate_b_detour.installed) {
        RemoveInlineDetour(&g_inventory_summary_refresh_candidate_b_detour);
    }
    if (g_inventory_summary_refresh_candidate_c_detour.installed) {
        RemoveInlineDetour(&g_inventory_summary_refresh_candidate_c_detour);
    }
    if (g_inventory_summary_refresh_candidate_d_detour.installed) {
        RemoveInlineDetour(&g_inventory_summary_refresh_candidate_d_detour);
    }
    if (g_inventory_window_wnd_notification_detour.installed) {
        RemoveInlineDetour(&g_inventory_window_wnd_notification_detour);
    }
    if (g_invslot_handle_lbutton_core_detour.installed) {
        RemoveInlineDetour(&g_invslot_handle_lbutton_core_detour);
    }
    if (g_invslot_wnd_handle_lbutton_up_detour.installed) {
        RemoveInlineDetour(&g_invslot_wnd_handle_lbutton_up_detour);
    }
    if (g_invslot_wnd_handle_lbutton_up_afterheld_detour.installed) {
        RemoveInlineDetour(&g_invslot_wnd_handle_lbutton_up_afterheld_detour);
    }
    if (g_cxwnd_handle_lbutton_up_detour.installed) {
        RemoveInlineDetour(&g_cxwnd_handle_lbutton_up_detour);
    }
    if (g_everquest_lmouse_up_detour.installed) {
        RemoveInlineDetour(&g_everquest_lmouse_up_detour);
    }
    if (g_move_item_owner_primary_setter_detour.installed) {
        RemoveInlineDetour(&g_move_item_owner_primary_setter_detour);
    }
    if (g_move_item_slot_populate_detour.installed) {
        RemoveInlineDetour(&g_move_item_slot_populate_detour);
    }
    if (g_move_item_descriptor_build_detour.installed) {
        RemoveInlineDetour(&g_move_item_descriptor_build_detour);
    }
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_late_slot17_apply_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_late_slot17_gate_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_post_resolve_gate_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_item_range_gate_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_global_action49_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_slot17_message_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_alt_dispatch_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_mode_bits_e_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_late_branch_dispatch_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_late_branch_gate_b_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_late_branch_prep_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_late_branch_gate_a_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_post_lookup_ui_pulse_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_post_lookup_mode_gate_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_manager_precheck_f_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_manager_precheck_e_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_manager_precheck_d_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_manager_precheck_c_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_manager_precheck_b_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_manager_precheck_a_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_resolved_flag_b_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_resolved_flag_a_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_resolved_kind_b_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_resolved_kind_a_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_resolve_object_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_local_gate_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_mode_bits_d_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_mode_bits_c_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_mode_bits_b_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_mode_bits_a_callsite_patch);
    RemoveCallsitePatch(&g_invslot_handle_lbutton_core_precheck_callsite_patch);
    RemoveCallsitePatch(&g_move_item_ctor_site_b_callsite_patch);
    RemoveCallsitePatch(&g_move_item_ctor_site_a_callsite_patch);
    RemoveCallsitePatch(&g_move_item_stack_local_gate_site_b_callsite_patch);
    RemoveCallsitePatch(&g_move_item_branch_bool_site_b_callsite_patch);
    RemoveCallsitePatch(&g_move_item_branch_bool_site_a_callsite_patch);
    RemoveCallsitePatch(&g_move_item_branch_kind_site_b_callsite_patch);
    RemoveCallsitePatch(&g_move_item_branch_kind_site_a_callsite_patch);
    RemoveCallsitePatch(&g_move_item_to_slot_resolve_callsite_patch);
    RemoveCallsitePatch(&g_move_item_from_slot_resolve_callsite_patch);
    RemoveCallsitePatch(&g_drag_drop_silent_precheck_callsite_patch);
    RemoveCallsitePatch(&g_drag_drop_local_reject_message_callsite_patch);
    RemoveCallsitePatch(&g_equip_nested_validator_callsite_patch);
    RemoveCallsitePatch(&g_equip_nested_inventory_gate_callsite_patch);
    RemoveCallsitePatch(&g_equip_local_requirement_lookup_b_callsite_patch);
    RemoveCallsitePatch(&g_equip_local_requirement_lookup_a_callsite_patch);
    RemoveCallsitePatch(&g_equip_local_record_lookup_callsite_patch);
    RemoveCallsitePatch(&g_equip_local_reject_message_callsite_patch);
    RemoveCallsitePatch(&g_equip_click_requirement_lookup_callsite_patch);
    RemoveCallsitePatch(&g_equip_click_can_equip_callsite_patch);
    RemoveCallsitePatch(&g_equip_click_record_lookup_callsite_patch);
    g_original_equip_nested_validator = nullptr;
    g_original_equip_nested_inventory_gate = nullptr;
    g_original_equip_requirement_lookup = nullptr;
    g_original_equip_record_lookup = nullptr;
    g_original_equip_message_resolver = nullptr;
    g_original_drag_drop_silent_precheck = nullptr;
    g_original_move_item_slot_resolve = nullptr;
    g_original_move_item_branch_kind = nullptr;
    g_original_move_item_branch_bool = nullptr;
    g_original_move_item_stack_local_gate = nullptr;
    g_original_inventory_window_wnd_notification = nullptr;
    g_original_inventory_summary_refresh_candidate_a = nullptr;
    g_original_inventory_summary_refresh_candidate_b = nullptr;
    g_original_inventory_summary_refresh_candidate_c = nullptr;
    g_original_inventory_summary_refresh_candidate_d = nullptr;
    g_inventory_on_process_frame_trace_count = 0;
    g_original_invslot_handle_lbutton_core = nullptr;
    g_original_invslot_handle_lbutton_core_precheck = nullptr;
    g_original_invslot_handle_lbutton_core_mode_bits = nullptr;
    g_original_invslot_handle_lbutton_core_local_gate = nullptr;
    g_original_invslot_handle_lbutton_core_resolve_object = nullptr;
    g_original_invslot_handle_lbutton_core_resolved_kind = nullptr;
    g_original_invslot_handle_lbutton_core_resolved_flag = nullptr;
    g_original_invslot_handle_lbutton_core_alt_dispatch = nullptr;
    g_original_invslot_handle_lbutton_core_slot17_message = nullptr;
    g_original_invslot_handle_lbutton_core_post_resolve_gate = nullptr;
    g_original_invslot_handle_lbutton_core_post_lookup_mode_gate = nullptr;
    g_original_invslot_handle_lbutton_core_post_lookup_ui_pulse = nullptr;
    g_original_invslot_handle_lbutton_core_late_branch_gate_a = nullptr;
    g_original_invslot_handle_lbutton_core_late_branch_prep = nullptr;
    g_original_invslot_handle_lbutton_core_late_branch_gate_b = nullptr;
    g_original_invslot_handle_lbutton_core_late_branch_dispatch = nullptr;
    g_original_invslot_handle_lbutton_core_late_slot17_gate = nullptr;
    g_original_invslot_handle_lbutton_core_late_slot17_apply = nullptr;
    g_original_invslot_wnd_handle_lbutton_up = nullptr;
    g_original_invslot_wnd_handle_lbutton_up_afterheld = nullptr;
    g_original_cxwnd_handle_lbutton_up = nullptr;
    g_original_everquest_lmouse_up = nullptr;
    g_original_move_item_owner_primary_setter = nullptr;
    g_original_move_item_validation_gate = nullptr;
    g_original_move_item_slot21_lookup = nullptr;
    g_original_move_item_descriptor_build = nullptr;
    g_original_move_item_slot_populate = nullptr;
    g_character_zone_client_has_skill = nullptr;
    g_character_zone_client_has_skill_address = 0;
    if (!g_can_equip_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_can_equip_detour)) {
        g_original_can_equip = nullptr;
        g_multiclass_item_usability_enabled = false;
        g_invslot_handle_lbutton_core_late_branch_prep_override_count = 0;
        g_invslot_handle_lbutton_core_late_branch_gate_b_override_count = 0;
        monomyth::logger::Log(
            L"hook_manager: item usability hook removed target=EQ_Character::CanEquip");
        return true;
    }

    return false;
}

bool RemoveInvSlotMgrMoveItemTrace() noexcept {
    if (!g_inv_slot_mgr_move_item_detour.installed) {
        g_original_inv_slot_mgr_move_item = nullptr;
        g_inv_slot_mgr_move_item_address = 0;
        return true;
    }

    if (RemoveInlineDetour(&g_inv_slot_mgr_move_item_detour)) {
        g_original_inv_slot_mgr_move_item = nullptr;
        g_inv_slot_mgr_move_item_address = 0;
        monomyth::logger::Log(
            L"hook_manager: move item trace removed target=CInvSlotMgr::MoveItem");
        return true;
    }

    return false;
}

bool RemoveCanStartMemmingTrace() noexcept {
    if (!g_can_start_memming_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_can_start_memming_detour)) {
        g_original_can_start_memming = nullptr;
        monomyth::logger::Log(L"hook_manager: spell usability trace removed target=CanStartMemming");
        return true;
    }

    return false;
}

bool RemoveSpellbookDispatcherTrace() noexcept {
    if (!g_spellbook_dispatcher_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_spellbook_dispatcher_detour)) {
        g_original_spellbook_dispatcher = nullptr;
        g_spellbook_dispatcher_address = 0;
        g_spellbook_ui_active_correlation_id = 0;
        g_spellbook_ui_correlation_count = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=SpellbookDispatcher");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePathTrace() noexcept {
    if (!g_start_spell_scribe_path_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_path_detour)) {
        g_original_start_spell_scribe_path = nullptr;
        g_start_spell_scribe_path_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePath");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckModeGetterTrace() noexcept {
    if (!g_start_spell_scribe_precheck_mode_getter_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_mode_getter_detour)) {
        g_original_start_spell_scribe_precheck_mode_getter = nullptr;
        g_start_spell_scribe_precheck_mode_getter_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckModeGetter");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckGateTrace() noexcept {
    if (!g_start_spell_scribe_precheck_gate_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_gate_detour)) {
        g_original_start_spell_scribe_precheck_gate = nullptr;
        g_start_spell_scribe_precheck_gate_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckGate");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckLookupTrace() noexcept {
    if (!g_start_spell_scribe_precheck_lookup_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_lookup_detour)) {
        g_original_start_spell_scribe_precheck_lookup = nullptr;
        g_start_spell_scribe_precheck_lookup_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckLookup");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckFastAcceptTrace() noexcept {
    if (!g_start_spell_scribe_precheck_fast_accept_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_fast_accept_detour)) {
        g_original_start_spell_scribe_precheck_fast_accept = nullptr;
        g_start_spell_scribe_precheck_fast_accept_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckFastAccept");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckClassResolverTrace() noexcept {
    if (!g_start_spell_scribe_precheck_class_resolver_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_class_resolver_detour)) {
        g_original_start_spell_scribe_precheck_class_resolver = nullptr;
        g_start_spell_scribe_precheck_class_resolver_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckClassResolver");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckAssignedMaskGetterTrace() noexcept {
    if (!g_start_spell_scribe_precheck_assigned_mask_getter_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_assigned_mask_getter_detour)) {
        g_original_start_spell_scribe_precheck_assigned_mask_getter = nullptr;
        g_start_spell_scribe_precheck_assigned_mask_getter_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckAssignedMaskGetter");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckRule4462c0Trace() noexcept {
    if (!g_start_spell_scribe_precheck_rule_4462c0_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_4462c0_detour)) {
        g_original_start_spell_scribe_precheck_rule_4462c0 = nullptr;
        g_start_spell_scribe_precheck_rule_4462c0_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckRule4462c0");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckRule446190Trace() noexcept {
    if (!g_start_spell_scribe_precheck_rule_446190_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_446190_detour)) {
        g_original_start_spell_scribe_precheck_rule_446190 = nullptr;
        g_start_spell_scribe_precheck_rule_446190_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckRule446190");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckRule446200Trace() noexcept {
    if (!g_start_spell_scribe_precheck_rule_446200_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_446200_detour)) {
        g_original_start_spell_scribe_precheck_rule_446200 = nullptr;
        g_start_spell_scribe_precheck_rule_446200_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckRule446200");
        return true;
    }

    return false;
}

bool RemoveStartSpellScribePrecheckRule446380Trace() noexcept {
    if (!g_start_spell_scribe_precheck_rule_446380_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_scribe_precheck_rule_446380_detour)) {
        g_original_start_spell_scribe_precheck_rule_446380 = nullptr;
        g_start_spell_scribe_precheck_rule_446380_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spell usability trace removed target=StartSpellScribePrecheckRule446380");
        return true;
    }

    return false;
}

bool RemoveSpellbookMemorizeSendPathTrace() noexcept {
    if (!g_spellbook_memorize_send_path_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_spellbook_memorize_send_path_detour)) {
        g_original_spellbook_memorize_send_path = nullptr;
        g_spellbook_memorize_send_path_address = 0;
        monomyth::logger::Log(
            L"hook_manager: spellbook memorize send path trace removed target=SpellbookMemorizeSendPath");
        return true;
    }

    return false;
}

bool RemoveStartSpellMemorizationPathTrace() noexcept {
    if (!g_start_spell_memorization_path_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_start_spell_memorization_path_detour)) {
        g_original_start_spell_memorization_path = nullptr;
        g_start_spell_memorization_path_address = 0;
        monomyth::logger::Log(
            L"hook_manager: start spell memorization path trace removed target=StartSpellMemorizationPath");
        return true;
    }

    return false;
}

bool RemoveMemorizeSendTrace() noexcept {
    if (!g_memorize_send_packet_wrapper_detour.installed) {
        return true;
    }

    if (RemoveInlineDetour(&g_memorize_send_packet_wrapper_detour)) {
        g_original_memorize_send_packet_wrapper = nullptr;
        g_memorize_send_packet_wrapper_address = 0;
        ClearPendingMemorizeSendObservation();
        g_spellbook_scribe_pending_correlation_id = 0;
        g_spellbook_scribe_pending_wrapper_sends = 0;
        monomyth::logger::Log(
            L"hook_manager: wrapper send trace removed target=MemorizeSendPacketWrapper");
        return true;
    }

    return false;
}

#else

bool InstallReceiveDispatchHook(const monomyth::runtime::Manifest&) noexcept {
    monomyth::logger::Log(
        L"hook_manager: receive dispatcher hook requires 32-bit x86 thiscall support; packet hook disabled");
    return false;
}

bool InstallWhoClassNameDisplayHook(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallCharacterListUpdateListHook() noexcept {
    return false;
}

bool InstallProgressionSelectionClassDisplayHook(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallInventoryClassTitleDisplayHook(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool RemoveReceiveDispatchHook() noexcept {
    return true;
}

bool RemoveWhoClassNameDisplayHook() noexcept {
    return true;
}

bool RemoveCharacterListUpdateListHook() noexcept {
    return true;
}

bool RemoveProgressionSelectionClassDisplayHook() noexcept {
    return true;
}

bool RemoveInventoryClassTitleDisplayHook() noexcept {
    return true;
}

bool InstallGetSpellLevelNeededHook(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallScrollScribeTraceHooks(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallGuildTrainerHook(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallCanEquipHook(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallActivatedSkillVisibilityHook(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallInvSlotMgrMoveItemTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallCanStartMemmingTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallSpellbookDispatcherTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePathTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckModeGetterTrace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckGateTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckLookupTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckFastAcceptTrace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckClassResolverTrace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckAssignedMaskGetterTrace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckRule4462c0Trace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckRule446190Trace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckRule446200Trace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellScribePrecheckRule446380Trace(
    const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallStartSpellMemorizationPathTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallSpellbookMemorizeSendPathTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallMemorizeSendTrace(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool RemoveGetSpellLevelNeededTrace() noexcept {
    return true;
}

bool RemoveScrollScribeTraceHooks() noexcept {
    return true;
}

bool RemoveGuildTrainerHook() noexcept {
    return true;
}

bool RemoveCanEquipHook() noexcept {
    return true;
}

bool RemoveActivatedSkillVisibilityHook() noexcept {
    return true;
}

bool RemoveInvSlotMgrMoveItemTrace() noexcept {
    return true;
}

bool RemoveCanStartMemmingTrace() noexcept {
    return true;
}

bool RemoveSpellbookDispatcherTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePathTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckModeGetterTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckGateTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckLookupTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckFastAcceptTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckClassResolverTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckAssignedMaskGetterTrace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckRule4462c0Trace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckRule446190Trace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckRule446200Trace() noexcept {
    return true;
}

bool RemoveStartSpellScribePrecheckRule446380Trace() noexcept {
    return true;
}

bool RemoveStartSpellMemorizationPathTrace() noexcept {
    return true;
}

bool RemoveSpellbookMemorizeSendPathTrace() noexcept {
    return true;
}

bool RemoveMemorizeSendTrace() noexcept {
    return true;
}

#endif

}  // namespace

void NotifyServerAuthStatsUpdated() noexcept {
#if defined(_M_IX86) || defined(__i386__)
    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    if (!snapshot.has_classes_bitmask || snapshot.classes_bitmask == 0) {
        return;
    }

    g_multiclass_cache_persist_pending_mask.store(snapshot.classes_bitmask);
    g_multiclass_cache_persist_pending.store(true);
    TryPersistPendingMulticlassCache(L"ServerAuthStats");

    TryForceCharacterListRefreshForAssignedMask(snapshot.classes_bitmask);
#endif
}

bool Initialize(const monomyth::runtime::Manifest& manifest) noexcept {
    if (g_initialized) {
        return true;
    }

    bool receive_hook_active = false;
    bool spell_trace_active = false;
    bool scroll_scribe_trace_active = false;
    bool memorize_send_trace_active = false;
    bool spell_behavior_active = false;
    bool item_behavior_active = false;
    bool trainer_behavior_active = false;
    bool move_item_trace_active = false;
    bool move_item_send_trace_active = false;
    bool ui_display_active = false;
    bool progression_selection_display_active = false;
    bool inventory_class_title_display_active = false;

    g_multiclass_ui_display_enabled = manifest.multiclass_ui_display_allowed;

    if (manifest.packet_hooks_allowed && !InstallReceiveDispatchHook(manifest)) {
        monomyth::packet_observer::DisableBecauseHookUnavailable(
            L"receive dispatcher hook install failed or was ambiguous");
        return false;
    }
    receive_hook_active = g_receive_dispatch_detour.installed;

    if (manifest.spell_usability_trace_allowed || manifest.multiclass_spell_usability_allowed) {
        if (InstallGetSpellLevelNeededHook(manifest)) {
            spell_trace_active = manifest.spell_usability_trace_allowed;
            spell_behavior_active = manifest.multiclass_spell_usability_allowed;
        } else if (
            manifest.get_spell_level_needed_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability hook install failed target=GetSpellLevelNeeded");
        }
    }

    if (manifest.spell_usability_trace_allowed || manifest.multiclass_spell_usability_allowed) {
        if (!InstallStartSpellScribePrecheckGateTrace(manifest) &&
            manifest.start_spell_scribe_precheck_gate_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckGate");
        }
        if (!InstallStartSpellScribePrecheckAssignedMaskGetterTrace(manifest) &&
            manifest.start_spell_scribe_precheck_assigned_mask_getter_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckAssignedMaskGetter");
        }
        if (!InstallStartSpellScribePrecheckRule4462c0Trace(manifest) &&
            manifest.start_spell_scribe_precheck_rule_4462c0_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckRule4462c0");
        }
        if (!InstallStartSpellScribePrecheckRule446190Trace(manifest) &&
            manifest.start_spell_scribe_precheck_rule_446190_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckRule446190");
        }
        if (!InstallStartSpellScribePrecheckRule446200Trace(manifest) &&
            manifest.start_spell_scribe_precheck_rule_446200_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckRule446200");
        }
        if (!InstallStartSpellScribePrecheckRule446380Trace(manifest) &&
            manifest.start_spell_scribe_precheck_rule_446380_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckRule446380");
        }
    }

    if (manifest.spell_usability_trace_allowed) {
        if (!InstallSpellbookDispatcherTrace(manifest) &&
            manifest.spellbook_dispatcher_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=SpellbookDispatcher");
        }
        if (!InstallStartSpellScribePathTrace(manifest) &&
            manifest.start_spell_scribe_path_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePath");
        }
        if (!InstallStartSpellScribePrecheckModeGetterTrace(manifest) &&
            manifest.start_spell_scribe_precheck_mode_getter_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckModeGetter");
        }
        if (!InstallStartSpellScribePrecheckLookupTrace(manifest) &&
            manifest.start_spell_scribe_precheck_lookup_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckLookup");
        }
        if (!InstallStartSpellScribePrecheckFastAcceptTrace(manifest) &&
            manifest.start_spell_scribe_precheck_fast_accept_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckFastAccept");
        }
        if (!InstallStartSpellScribePrecheckClassResolverTrace(manifest) &&
            manifest.start_spell_scribe_precheck_class_resolver_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=StartSpellScribePrecheckClassResolver");
        }
        if (InstallCanStartMemmingTrace(manifest)) {
            spell_trace_active = true;
        } else if (
            manifest.can_start_memming_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spell usability trace install failed target=CanStartMemming");
        }
    } else if (manifest.spell_usability_trace_dev_opt_in) {
        std::wstring message =
            L"hook_manager: spell usability trace skipped reason=\"";
        message += manifest.spell_usability_trace_reason.empty()
            ? L"unknown"
            : manifest.spell_usability_trace_reason;
        message += L"\"";
        monomyth::logger::Log(message);
    }

    if (manifest.scroll_scribe_trace_allowed || manifest.multiclass_item_usability_allowed) {
        if (InstallScrollScribeTraceHooks(manifest)) {
            scroll_scribe_trace_active = manifest.scroll_scribe_trace_allowed;
        } else if (
            manifest.is_class_usable_predicate_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: class usability hook install failed target=EQ_Character::IsClassUsablePredicate");
        }
    } else if (manifest.scroll_scribe_trace_dev_opt_in) {
        std::wstring message =
            L"hook_manager: scroll scribe trace skipped reason=\"";
        message += manifest.scroll_scribe_trace_reason.empty()
            ? L"unknown"
            : manifest.scroll_scribe_trace_reason;
        message += L"\"";
        monomyth::logger::Log(message);
    }

    if (receive_hook_active) {
        trainer_behavior_active = InstallGuildTrainerHook(manifest);
    }

    if (manifest.multiclass_item_usability_allowed) {
        if (InstallCanEquipHook(manifest)) {
            item_behavior_active = true;
        } else if (
            manifest.can_equip_state ==
            monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: item usability hook install failed target=EQ_Character::CanEquip");
        }
    }

    if (manifest.multiclass_skill_visibility_allowed) {
        if (!InstallActivatedSkillVisibilityHook(manifest)) {
            monomyth::logger::Log(
                L"hook_manager: activated skill visibility hook install failed target=CharacterZoneClient::HasSkill");
        }
    }

    if (manifest.multiclass_item_usability_allowed) {
        if (InstallInvSlotMgrMoveItemTrace(manifest)) {
            move_item_trace_active = true;
        } else if (
            manifest.inv_slot_mgr_move_item_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: move item trace install failed target=CInvSlotMgr::MoveItem");
        }
        if (InstallMemorizeSendTrace(manifest)) {
            move_item_send_trace_active = true;
        } else if (
            manifest.memorize_send_packet_wrapper_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: move item send trace install failed target=MemorizeSendPacketWrapper");
        }
    }

    if (manifest.memorize_send_trace_allowed) {
        if (g_memorize_send_packet_wrapper_detour.installed ||
            InstallMemorizeSendTrace(manifest)) {
            memorize_send_trace_active = true;
        } else if (
            manifest.memorize_send_packet_wrapper_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: memorize send trace install failed target=MemorizeSendPacketWrapper");
        }
        if (!InstallSpellbookMemorizeSendPathTrace(manifest) &&
            manifest.spellbook_memorize_send_path_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: spellbook memorize send path trace install failed target=SpellbookMemorizeSendPath");
        }
        if (!InstallStartSpellMemorizationPathTrace(manifest) &&
            manifest.start_spell_memorization_path_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: start spell memorization path trace install failed target=StartSpellMemorizationPath");
        }
    } else if (manifest.memorize_send_trace_dev_opt_in) {
        std::wstring message =
            L"hook_manager: memorize send trace skipped reason=\"";
        message += manifest.memorize_send_trace_reason.empty()
            ? L"unknown"
            : manifest.memorize_send_trace_reason;
        message += L"\"";
        monomyth::logger::Log(message);
    }

    if (manifest.multiclass_ui_display_allowed) {
        if (InstallWhoClassNameDisplayHook(manifest)) {
            ui_display_active = true;
        } else {
            monomyth::logger::Log(
                L"hook_manager: multiclass UI display install failed target=LeftClickedOnPlayerSurrogate/GetClassDesc/GetClassThreeLetterCode local/self path");
        }
        if (InstallCharacterListUpdateListHook()) {
            ui_display_active = true;
        }
        if (InstallProgressionSelectionClassDisplayHook(manifest)) {
            progression_selection_display_active = true;
        } else {
            monomyth::logger::Log(
                L"hook_manager: progression selection class display install failed target=ClassValueLabel");
        }
        InstallInventoryClassTitleDisplayHook(manifest);
    }
    LogMemorizeSendTraceStartupMarker(
        manifest,
        memorize_send_trace_active || move_item_send_trace_active);

    g_initialized = true;
    if (receive_hook_active || spell_trace_active || scroll_scribe_trace_active ||
        memorize_send_trace_active || spell_behavior_active || item_behavior_active ||
        trainer_behavior_active || move_item_trace_active || move_item_send_trace_active ||
        ui_display_active || progression_selection_display_active ||
        inventory_class_title_display_active) {
        std::wstring message = L"hook_manager: initialized (";
        bool first = true;
        if (receive_hook_active) {
            message += L"receive dispatcher hook";
            first = false;
        }
        if (spell_trace_active) {
            if (!first) {
                message += L", ";
            }
            message += L"spell usability trace";
            first = false;
        }
        if (scroll_scribe_trace_active) {
            if (!first) {
                message += L", ";
            }
            message += L"scroll scribe trace";
            first = false;
        }
        if (memorize_send_trace_active) {
            if (!first) {
                message += L", ";
            }
            message += L"memorize send trace";
            first = false;
        }
        if (spell_behavior_active) {
            if (!first) {
                message += L", ";
            }
            message += L"multiclass spell usability";
            first = false;
        }
        if (item_behavior_active) {
            if (!first) {
                message += L", ";
            }
            message += L"multiclass item usability";
            first = false;
        }
        if (trainer_behavior_active) {
            if (!first) {
                message += L", ";
            }
            message += L"multiclass trainer gate";
            first = false;
        }
        if (move_item_trace_active) {
            if (!first) {
                message += L", ";
            }
            message += L"move item trace";
            first = false;
        }
        if (move_item_send_trace_active) {
            if (!first) {
                message += L", ";
            }
            message += L"move item send trace";
            first = false;
        }
        if (ui_display_active) {
            if (!first) {
                message += L", ";
            }
            message += L"multiclass UI display";
            first = false;
        }
        if (progression_selection_display_active) {
            if (!first) {
                message += L", ";
            }
            message += L"progression selection class display";
            first = false;
        }
        if (inventory_class_title_display_active) {
            if (!first) {
                message += L", ";
            }
            message += L"item display class text trace";
            first = false;
        }
        message += L" active)";
        monomyth::logger::Log(message);
    } else {
        std::wstring message = L"hook_manager: initialized (no active hooks) packet_hooks_reason=\"";
        if (manifest.packet_hooks_reason.empty()) {
            message += L"unknown";
        } else {
            message += manifest.packet_hooks_reason;
        }
        message += L"\"";
        message += L" multiclass_spell_usability_reason=\"";
        message += manifest.multiclass_spell_usability_reason.empty()
            ? L"unknown"
            : manifest.multiclass_spell_usability_reason;
        message += L"\"";
        message += L" multiclass_item_usability_reason=\"";
        message += manifest.multiclass_item_usability_reason.empty()
            ? L"unknown"
            : manifest.multiclass_item_usability_reason;
        message += L"\"";
        message += L" multiclass_ui_display_reason=\"";
        message += manifest.multiclass_ui_display_reason.empty()
            ? L"unknown"
            : manifest.multiclass_ui_display_reason;
        message += L"\"";
        message += L" scroll_scribe_trace_reason=\"";
        message += manifest.scroll_scribe_trace_reason.empty()
            ? L"unknown"
            : manifest.scroll_scribe_trace_reason;
        message += L"\"";
        message += L" memorize_send_trace_reason=\"";
        message += manifest.memorize_send_trace_reason.empty()
            ? L"unknown"
            : manifest.memorize_send_trace_reason;
        message += L"\"";
        monomyth::logger::Log(message);
    }

    if (manifest.heartbeat_allowed) {
        monomyth::logger::Log(L"hook_manager: heartbeat (guard passed)");
    }
    return true;
}

void Shutdown() noexcept {
    if (!g_initialized) {
        return;
    }

    if (!RemoveReceiveDispatchHook()) {
        monomyth::logger::Log(L"hook_manager: shutdown deferred because receive hook removal failed");
        return;
    }
    if (!RemoveWhoClassNameDisplayHook()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because multiclass UI display hook removal failed");
        return;
    }
    if (!RemoveCharacterListUpdateListHook()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because character list refresh hook removal failed");
        return;
    }
    if (!RemoveProgressionSelectionClassDisplayHook()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because progression selection class display hook removal failed");
        return;
    }
    if (!RemoveInventoryClassTitleDisplayHook()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because item display class text trace removal failed");
        return;
    }
    if (!RemoveGetSpellLevelNeededTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because GetSpellLevelNeeded trace removal failed");
        return;
    }
    if (!RemoveActivatedSkillVisibilityHook()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because activated skill visibility hook removal failed");
        return;
    }
    if (!RemoveCanEquipHook()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because CanEquip hook removal failed");
        return;
    }
    if (!RemoveInvSlotMgrMoveItemTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because move item trace removal failed");
        return;
    }
    if (!RemoveScrollScribeTraceHooks()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because scroll scribe trace removal failed");
        return;
    }
    if (!RemoveGuildTrainerHook()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because guild trainer hook removal failed");
        return;
    }
    if (!RemoveCanStartMemmingTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because CanStartMemming trace removal failed");
        return;
    }
    if (!RemoveSpellbookDispatcherTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because SpellbookDispatcher trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePathTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePath trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckModeGetterTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckModeGetter trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckGateTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckGate trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckLookupTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckLookup trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckFastAcceptTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckFastAccept trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckClassResolverTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckClassResolver trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckAssignedMaskGetterTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckAssignedMaskGetter trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckRule4462c0Trace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckRule4462c0 trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckRule446190Trace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckRule446190 trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckRule446200Trace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckRule446200 trace removal failed");
        return;
    }
    if (!RemoveStartSpellScribePrecheckRule446380Trace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because StartSpellScribePrecheckRule446380 trace removal failed");
        return;
    }
    if (g_memorize_send_pending_correlation_id != 0) {
        LogPendingMemorizeSendGap(L"hook_shutdown");
    }
    if (g_spellbook_scribe_pending_correlation_id != 0) {
        LogPendingSpellbookScribeGap(L"hook_shutdown");
    }
    if (!RemoveMemorizeSendTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because memorize send trace removal failed");
        return;
    }
    if (!RemoveStartSpellMemorizationPathTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because start spell memorization path trace removal failed");
        return;
    }
    if (!RemoveSpellbookMemorizeSendPathTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because spellbook memorize send path trace removal failed");
        return;
    }

    g_initialized = false;
    monomyth::logger::Log(L"hook_manager: shutdown");
}

}  // namespace monomyth::hooks
