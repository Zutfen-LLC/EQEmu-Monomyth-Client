#include "hook_manager.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include "logger.h"
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
#else
#define MONOMYTH_FASTCALL __attribute__((fastcall))
#define MONOMYTH_THISCALL __attribute__((thiscall))
#endif

constexpr std::size_t kJmpPatchBytes = 5;
constexpr std::size_t kMaxStolenBytes = 16;
constexpr std::size_t kIntermediateSendPrefixByteCap = 16;
constexpr std::uint64_t kSpellTraceInitialLogCount = 20;
constexpr std::uint64_t kSpellTraceLogInterval = 100;
constexpr std::uint32_t kMemorizeSpellOpcode = 0x217c;
constexpr std::uint32_t kDeleteSpellOpcode = 0x3358;
constexpr std::uint32_t kMemorizeSendCorrelationMaxWrapperSends = 8;
constexpr std::uint32_t kSpellbookScribeCorrelationMaxWrapperSends = 8;
constexpr std::size_t kScribeGateDescriptorTableOffset = 0x4;
constexpr std::size_t kScribeGateRelativeOffsetField = 0x4;
constexpr std::size_t kScribeGateLookupContextBias = 0x8;
constexpr std::size_t kScribeGateLookupKeyOffset = 0x4;
constexpr std::size_t kScribeGateNodeRecordOffset = 0x4;
constexpr std::size_t kScribeGateNodeNextOffset = 0xc;
constexpr std::size_t kScribeGateResolvedClassIdOffset = 0x3374;
constexpr std::uint32_t kScribeGateMaxRelativeLookupOffset = 0x00100000;
constexpr std::size_t kScribeGateMaxLookupNodes = 256;
constexpr wchar_t kMemorizeSendTraceSliceId[] = L"CLIENT-MEM-SEND-TRACE-001";

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

struct InlineDetour {
    std::uint8_t* target = nullptr;
    void* hook = nullptr;
    void* trampoline = nullptr;
    std::size_t patch_length = 0;
    std::array<std::uint8_t, kMaxStolenBytes> original = {};
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
ReceiveDispatchFn g_original_receive_dispatch = nullptr;
HandleRButtonUpFn g_original_handle_rbutton_up = nullptr;
GetSpellLevelNeededFn g_original_get_spell_level_needed = nullptr;
IsClassUsablePredicateFn g_original_is_class_usable_predicate = nullptr;
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
std::uint64_t g_get_spell_level_needed_trace_count = 0;
std::uint64_t g_can_start_memming_trace_count = 0;
std::uint64_t g_memorize_send_trace_count = 0;
std::uint64_t g_scroll_scribe_event_count = 0;
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
bool g_scroll_scribe_active_logging = false;
StartSpellScribePrecheckClassMaskSnapshot g_start_spell_scribe_precheck_class_mask_snapshot = {};
std::wstring g_handle_rbutton_up_evidence_source = L"unknown";
std::wstring g_is_class_usable_predicate_evidence_source = L"unknown";
bool g_multiclass_spell_usability_enabled = false;

std::wstring HexPtr(std::uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring Hex32(std::uint32_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
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

    if ((opcode >= 0x50 && opcode <= 0x5f) || opcode == 0x55 || opcode == 0x90) {
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

void LogMemorizeSendTraceStartupMarker(
    const monomyth::runtime::Manifest& manifest,
    bool hook_installed) {
    const bool target_validated =
        manifest.memorize_send_packet_wrapper_state ==
            monomyth::spell_usability_discovery::TargetState::kValidated &&
        manifest.memorize_send_packet_wrapper_address != 0;
    std::wstring message = L"MemorizeSendTraceStartup slice_id=";
    message += kMemorizeSendTraceSliceId;
    message += L" capability_enabled=";
    message += manifest.memorize_send_trace_allowed ? L"true" : L"false";
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
    int result) {
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
    suffix += std::to_wstring(result);
    suffix += L" assigned_mask=";
    suffix += FormatAssignedMask(snapshot);
    suffix += L" has_assigned_mask=";
    suffix += snapshot.has_classes_bitmask ? L"true" : L"false";
    suffix += L" evidence_source=";
    suffix += g_is_class_usable_predicate_evidence_source;
    suffix += L" scroll_hint=unknown";
    LogScrollScribeTrace(L"IsClassUsablePredicate", suffix);
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
    const bool class_id_is_playable = snapshot.class_id_copied && snapshot.class_id != 0 &&
        snapshot.class_id <= 32;
    const std::uint32_t current_class_bit = class_id_is_playable
        ? (1u << (snapshot.class_id - 1u))
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

    if (correlation_id != 0) {
        LogStartSpellScribePrecheckGateCall(
            correlation_id,
            this_context,
            descriptor_like,
            require_known_like,
            allow_recheck_like,
            GetCallerReturnAddress(),
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
    const int original_result =
        g_original_is_class_usable_predicate(this_character, class_id);
    LogIsClassUsablePredicateTrace(this_character, class_id, original_result);
    return original_result;
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
    if (!manifest.scroll_scribe_trace_allowed ||
        manifest.handle_rbutton_up_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.handle_rbutton_up_address == 0 ||
        manifest.is_class_usable_predicate_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.is_class_usable_predicate_address == 0) {
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

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.is_class_usable_predicate_address),
            reinterpret_cast<void*>(&IsClassUsablePredicateHook),
            &g_is_class_usable_predicate_detour,
            reinterpret_cast<void**>(&g_original_is_class_usable_predicate),
            L"IsClassUsablePredicate trace")) {
        RemoveInlineDetour(&g_is_class_usable_predicate_detour);
        RemoveInlineDetour(&g_handle_rbutton_up_detour);
        g_original_is_class_usable_predicate = nullptr;
        g_original_handle_rbutton_up = nullptr;
        return false;
    }

    std::wstring message =
        L"hook_manager: scroll scribe trace installed targets=CInvSlot::HandleRButtonUp,EQ_Character::IsClassUsablePredicate addresses=";
    message += HexPtr(manifest.handle_rbutton_up_address);
    message += L",";
    message += HexPtr(manifest.is_class_usable_predicate_address);
    monomyth::logger::Log(message);
    g_scroll_scribe_event_count = 0;
    g_scroll_scribe_active_correlation_id = 0;
    g_scroll_scribe_active_logging = false;
    g_handle_rbutton_up_evidence_source = manifest.handle_rbutton_up_evidence_source;
    g_is_class_usable_predicate_evidence_source =
        manifest.is_class_usable_predicate_evidence_source;
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
    if (!manifest.memorize_send_trace_allowed ||
        manifest.memorize_send_packet_wrapper_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.memorize_send_packet_wrapper_address == 0) {
        if (manifest.memorize_send_trace_dev_opt_in) {
            std::wstring message = L"hook_manager: memorize send trace denied ";
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
        L"hook_manager: memorize send trace installed target=MemorizeSendPacketWrapper address=";
    message += HexPtr(manifest.memorize_send_packet_wrapper_address);
    message += L" opcode_name=OP_MemorizeSpell";
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
        g_scroll_scribe_active_correlation_id = 0;
        g_scroll_scribe_active_logging = false;
        g_handle_rbutton_up_evidence_source = L"unknown";
        g_is_class_usable_predicate_evidence_source = L"unknown";
        monomyth::logger::Log(
            L"hook_manager: scroll scribe trace removed targets=CInvSlot::HandleRButtonUp,EQ_Character::IsClassUsablePredicate");
    }
    return ok;
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
            L"hook_manager: memorize send trace removed target=MemorizeSendPacketWrapper");
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

bool RemoveReceiveDispatchHook() noexcept {
    return true;
}

bool InstallGetSpellLevelNeededHook(const monomyth::runtime::Manifest&) noexcept {
    return false;
}

bool InstallScrollScribeTraceHooks(const monomyth::runtime::Manifest&) noexcept {
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

bool Initialize(const monomyth::runtime::Manifest& manifest) noexcept {
    if (g_initialized) {
        return true;
    }

    bool receive_hook_active = false;
    bool spell_trace_active = false;
    bool scroll_scribe_trace_active = false;
    bool memorize_send_trace_active = false;
    bool spell_behavior_active = false;

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

    if (manifest.scroll_scribe_trace_allowed) {
        if (InstallScrollScribeTraceHooks(manifest)) {
            scroll_scribe_trace_active = true;
        } else if (
            manifest.handle_rbutton_up_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated &&
            manifest.is_class_usable_predicate_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: scroll scribe trace install failed targets=CInvSlot::HandleRButtonUp,EQ_Character::IsClassUsablePredicate");
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

    if (manifest.memorize_send_trace_allowed) {
        if (InstallMemorizeSendTrace(manifest)) {
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
    LogMemorizeSendTraceStartupMarker(manifest, memorize_send_trace_active);

    g_initialized = true;
    if (receive_hook_active || spell_trace_active || scroll_scribe_trace_active ||
        memorize_send_trace_active || spell_behavior_active) {
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
    if (!RemoveGetSpellLevelNeededTrace()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because GetSpellLevelNeeded trace removal failed");
        return;
    }
    if (!RemoveScrollScribeTraceHooks()) {
        monomyth::logger::Log(
            L"hook_manager: shutdown deferred because scroll scribe trace removal failed");
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
