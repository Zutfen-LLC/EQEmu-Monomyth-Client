#include "hook_manager.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>

#include "logger.h"
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
constexpr std::uint64_t kSpellTraceInitialLogCount = 20;
constexpr std::uint64_t kSpellTraceLogInterval = 100;

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
using CanEquipFn = int (MONOMYTH_THISCALL*)(
    void* this_character,
    const void* item_or_contents,
    std::uint32_t arg2,
    std::uint32_t arg3);
using CanStartMemmingFn = bool (MONOMYTH_THISCALL*)(
    void* this_window,
    int spell_or_book_index);

struct InlineDetour {
    std::uint8_t* target = nullptr;
    void* hook = nullptr;
    void* trampoline = nullptr;
    std::size_t patch_length = 0;
    std::array<std::uint8_t, kMaxStolenBytes> original = {};
    std::array<std::uint8_t, kJmpPatchBytes> patch = {};
    bool installed = false;
};

InlineDetour g_receive_dispatch_detour = {};
InlineDetour g_handle_rbutton_up_detour = {};
InlineDetour g_get_spell_level_needed_detour = {};
InlineDetour g_get_usable_classes_detour = {};
InlineDetour g_can_equip_detour = {};
InlineDetour g_can_start_memming_detour = {};
ReceiveDispatchFn g_original_receive_dispatch = nullptr;
HandleRButtonUpFn g_original_handle_rbutton_up = nullptr;
GetSpellLevelNeededFn g_original_get_spell_level_needed = nullptr;
void* g_original_get_usable_classes = nullptr;
CanEquipFn g_original_can_equip = nullptr;
CanStartMemmingFn g_original_can_start_memming = nullptr;
std::uint64_t g_get_spell_level_needed_trace_count = 0;
std::uint64_t g_can_start_memming_trace_count = 0;
std::uint64_t g_scroll_scribe_event_count = 0;
std::uint32_t g_scroll_scribe_active_correlation_id = 0;
bool g_scroll_scribe_active_logging = false;
void* g_scroll_scribe_last_get_usable_classes_this = nullptr;
std::uintptr_t g_scroll_scribe_last_get_usable_classes_arg0 = 0;
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
    if (message == nullptr || g_scroll_scribe_active_correlation_id == 0) {
        return;
    }

    message->append(L" scroll_scribe_correlation=");
    message->append(std::to_wstring(g_scroll_scribe_active_correlation_id));
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
    suffix += L" scroll_hint=unknown";
    suffix += L" observed_count=";
    suffix += std::to_wstring(g_scroll_scribe_event_count);
    LogScrollScribeTrace(L"CInvSlot::HandleRButtonUp", suffix);
}

void LogGetUsableClassesTrace(
    void* this_character,
    std::uintptr_t raw_arg0,
    std::uint32_t result) {
    if (!g_scroll_scribe_active_logging) {
        return;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::wstring suffix = L"this=";
    suffix += HexPtr(reinterpret_cast<std::uintptr_t>(this_character));
    suffix += L" raw_arg0=";
    suffix += HexPtr(raw_arg0);
    suffix += L" original_result=";
    suffix += std::to_wstring(result);
    suffix += L" assigned_mask=";
    suffix += FormatAssignedMask(snapshot);
    suffix += L" has_assigned_mask=";
    suffix += snapshot.has_classes_bitmask ? L"true" : L"false";
    suffix += L" scroll_hint=unknown";
    LogScrollScribeTrace(L"GetUsableClasses", suffix);
}

void LogCanEquipTrace(
    void* this_character,
    const void* item_or_contents,
    std::uint32_t arg2,
    std::uint32_t arg3,
    int result) {
    if (!g_scroll_scribe_active_logging) {
        return;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    std::wstring suffix = L"this=";
    suffix += HexPtr(reinterpret_cast<std::uintptr_t>(this_character));
    suffix += L" item_or_contents=";
    suffix += HexPtr(reinterpret_cast<std::uintptr_t>(item_or_contents));
    suffix += L" arg2=";
    suffix += Hex32(arg2);
    suffix += L" arg3=";
    suffix += Hex32(arg3);
    suffix += L" original_result=";
    suffix += std::to_wstring(result);
    suffix += L" assigned_mask=";
    suffix += FormatAssignedMask(snapshot);
    suffix += L" has_assigned_mask=";
    suffix += snapshot.has_classes_bitmask ? L"true" : L"false";
    suffix += L" scroll_hint=unknown";
    LogScrollScribeTrace(L"CanEquip", suffix);
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

bool MONOMYTH_FASTCALL CanStartMemmingHook(
    void* this_window,
    void*,
    int spell_or_book_index) noexcept {
    const bool original_result =
        g_original_can_start_memming(this_window, spell_or_book_index);
    ++g_can_start_memming_trace_count;
    if (ShouldLogSpellTrace(g_can_start_memming_trace_count)) {
        std::wstring message = L"SpellUsabilityTrace target=CanStartMemming spell_or_book_index=";
        message += std::to_wstring(spell_or_book_index);
        message += L" original_result=";
        message += original_result ? L"true" : L"false";
        AppendActiveScrollCorrelation(&message);
        message += L" observed_count=";
        message += std::to_wstring(g_can_start_memming_trace_count);
        monomyth::logger::Log(message);
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

#if defined(_MSC_VER)
void CaptureGetUsableClassesContextFromHook(
    void* this_character,
    std::uintptr_t raw_arg0) noexcept {
    g_scroll_scribe_last_get_usable_classes_this = this_character;
    g_scroll_scribe_last_get_usable_classes_arg0 = raw_arg0;
}

void LogGetUsableClassesTraceFromHook(std::uint32_t result) noexcept {
    LogGetUsableClassesTrace(
        g_scroll_scribe_last_get_usable_classes_this,
        g_scroll_scribe_last_get_usable_classes_arg0,
        result);
}

__declspec(naked) void GetUsableClassesHook() {
    __asm {
        pushfd
        pushad
        mov eax, ecx
        mov edx, [esp + 40]
        push edx
        push eax
        call CaptureGetUsableClassesContextFromHook
        add esp, 8
        popad
        popfd
        mov eax, g_original_get_usable_classes
        call eax
        pushfd
        pushad
        mov edx, eax
        push edx
        call LogGetUsableClassesTraceFromHook
        add esp, 4
        popad
        popfd
        ret
    }
}
#endif

int MONOMYTH_FASTCALL CanEquipHook(
    void* this_character,
    void*,
    const void* item_or_contents,
    std::uint32_t arg2,
    std::uint32_t arg3) noexcept {
    const int original_result =
        g_original_can_equip(this_character, item_or_contents, arg2, arg3);
    LogCanEquipTrace(this_character, item_or_contents, arg2, arg3, original_result);
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
        manifest.get_usable_classes_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.get_usable_classes_address == 0 ||
        manifest.can_equip_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.can_equip_address == 0) {
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

#if defined(_MSC_VER)
    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.get_usable_classes_address),
            reinterpret_cast<void*>(&GetUsableClassesHook),
            &g_get_usable_classes_detour,
            &g_original_get_usable_classes,
            L"GetUsableClasses trace")) {
        RemoveInlineDetour(&g_get_usable_classes_detour);
        RemoveInlineDetour(&g_handle_rbutton_up_detour);
        g_original_get_usable_classes = nullptr;
        g_original_handle_rbutton_up = nullptr;
        return false;
    }
#else
    RemoveInlineDetour(&g_handle_rbutton_up_detour);
    g_original_handle_rbutton_up = nullptr;
    monomyth::logger::Log(
        L"hook_manager: GetUsableClasses trace requires MSVC x86 naked-hook support; scroll trace disabled");
    return false;
#endif

    if (!InstallInlineDetour(
            reinterpret_cast<void*>(manifest.can_equip_address),
            reinterpret_cast<void*>(&CanEquipHook),
            &g_can_equip_detour,
            reinterpret_cast<void**>(&g_original_can_equip),
            L"CanEquip trace")) {
        RemoveInlineDetour(&g_can_equip_detour);
        RemoveInlineDetour(&g_get_usable_classes_detour);
        RemoveInlineDetour(&g_handle_rbutton_up_detour);
        g_original_can_equip = nullptr;
        g_original_get_usable_classes = nullptr;
        g_original_handle_rbutton_up = nullptr;
        return false;
    }

    std::wstring message =
        L"hook_manager: scroll scribe trace installed targets=CInvSlot::HandleRButtonUp,GetUsableClasses,CanEquip addresses=";
    message += HexPtr(manifest.handle_rbutton_up_address);
    message += L",";
    message += HexPtr(manifest.get_usable_classes_address);
    message += L",";
    message += HexPtr(manifest.can_equip_address);
    monomyth::logger::Log(message);
    g_scroll_scribe_event_count = 0;
    g_scroll_scribe_active_correlation_id = 0;
    g_scroll_scribe_active_logging = false;
    return true;
}

bool InstallCanStartMemmingTrace(const monomyth::runtime::Manifest& manifest) noexcept {
    if (!manifest.spell_usability_trace_allowed ||
        manifest.can_start_memming_state !=
            monomyth::spell_usability_discovery::TargetState::kValidated ||
        manifest.can_start_memming_address == 0) {
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
    if (g_can_equip_detour.installed && RemoveInlineDetour(&g_can_equip_detour)) {
        g_original_can_equip = nullptr;
    } else if (g_can_equip_detour.installed) {
        ok = false;
    }

    if (g_get_usable_classes_detour.installed &&
        RemoveInlineDetour(&g_get_usable_classes_detour)) {
        g_original_get_usable_classes = nullptr;
    } else if (g_get_usable_classes_detour.installed) {
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
        monomyth::logger::Log(
            L"hook_manager: scroll scribe trace removed targets=CInvSlot::HandleRButtonUp,GetUsableClasses,CanEquip");
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

bool RemoveGetSpellLevelNeededTrace() noexcept {
    return true;
}

bool RemoveScrollScribeTraceHooks() noexcept {
    return true;
}

bool RemoveCanStartMemmingTrace() noexcept {
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

    if (manifest.spell_usability_trace_allowed) {
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
            manifest.get_usable_classes_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated &&
            manifest.can_equip_state ==
                monomyth::spell_usability_discovery::TargetState::kValidated) {
            monomyth::logger::Log(
                L"hook_manager: scroll scribe trace install failed targets=CInvSlot::HandleRButtonUp,GetUsableClasses,CanEquip");
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

    g_initialized = true;
    if (receive_hook_active || spell_trace_active || scroll_scribe_trace_active || spell_behavior_active) {
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

    g_initialized = false;
    monomyth::logger::Log(L"hook_manager: shutdown");
}

}  // namespace monomyth::hooks
