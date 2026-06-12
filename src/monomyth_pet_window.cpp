#include "monomyth_pet_window.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"
#include "logger.h"

#if defined(_M_IX86) || defined(__i386__)

#if defined(_MSC_VER)
#define MONOMYTH_FASTCALL __fastcall
#define MONOMYTH_THISCALL __thiscall
#else
#define MONOMYTH_FASTCALL __attribute__((fastcall))
#define MONOMYTH_THISCALL __attribute__((thiscall))
#endif

namespace monomyth::multipet_window {
namespace {

constexpr std::size_t kCxWndParentWindowOffset = 0x0174;
constexpr std::size_t kCxWndVtableWndNotificationOffset = 0x088;
constexpr std::size_t kCxWndVtableGetWindowNameOffset = 0x130;
constexpr std::uint32_t kInventoryWindowGlobalRva = 0x0091ec8c;
constexpr std::uint32_t kPetInfoWindowGlobalRva = 0x0091fbf8;
constexpr std::uint32_t kPlayerWindowGlobalRva = 0x0091fc68;
constexpr std::uint32_t kCastSpellWindowGlobalRva = 0x0091fc84;
constexpr std::uint32_t kSpellBookWindowGlobalRva = 0x0091fc88;
constexpr std::uint32_t kEqMainWindowGlobalRva = 0x00b713c0;
constexpr std::uint32_t kGroupWindowGlobalRva = 0x00b71730;
constexpr std::uint32_t kCXWndGetChildItemByNameRva = 0x00468330;
constexpr std::uint32_t kCXWndShowRva = 0x00465290;
constexpr std::uint32_t kXwmLclick = 1;
constexpr const char* kWindowScreenName = "MonomythPetWindow";

struct PetSlotDescriptor {
    int slot;
    const char* button_name;
    const char* command;
};

struct WindowAnchor {
    const wchar_t* label;
    std::uint32_t global_rva;
};

constexpr std::array<PetSlotDescriptor, 3> kPetSlots = {{
    {0, "MMPW_Slot0_Attack", "attack"},
    {1, "MMPW_Slot1_Attack", "attack"},
    {2, "MMPW_Slot2_Attack", "attack"},
}};

constexpr std::array<WindowAnchor, 7> kWindowAnchors = {{
    {L"PlayerWnd", kPlayerWindowGlobalRva},
    {L"PetInfoWnd", kPetInfoWindowGlobalRva},
    {L"CastSpellWnd", kCastSpellWindowGlobalRva},
    {L"SpellBookWnd", kSpellBookWindowGlobalRva},
    {L"GroupWnd", kGroupWindowGlobalRva},
    {L"EQMainWnd", kEqMainWindowGlobalRva},
    {L"InventoryWnd", kInventoryWindowGlobalRva},
}};

using WndNotificationFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    void* sender_window,
    std::uint32_t notification_code,
    void* payload);

using GetChildItemByNameFn = void* (MONOMYTH_THISCALL*)(
    void* this_context,
    const char* name);

using GetWindowNameFn = const void* (MONOMYTH_THISCALL*)(void* this_context);

using CXWndShowFn = int (MONOMYTH_THISCALL*)(
    void* this_context,
    int show,
    int recurse,
    int unknown);

std::wstring WidenAsciiLossy(const char* text) {
    if (text == nullptr) {
        return L"(null)";
    }
    std::wstring result;
    while (*text != '\0') {
        result += static_cast<wchar_t>(static_cast<unsigned char>(*text));
        ++text;
    }
    return result;
}

std::wstring HexPtr(std::uintptr_t value) {
    wchar_t buf[20] = {};
    wsprintfW(buf, L"0x%08x", static_cast<std::uint32_t>(value));
    return buf;
}

std::string ReadCxStrAsAscii(const void* cxstr) {
    if (cxstr == nullptr) {
        return "";
    }
    const char* ptr = *reinterpret_cast<const char* const*>(cxstr);
    if (ptr == nullptr) {
        return "";
    }
    return ptr;
}

bool g_multipet_enabled = false;
bool g_window_attached = false;
void* g_window_instance = nullptr;
void** g_original_vtable_ptr = nullptr;
void* g_original_wnd_notification = nullptr;
std::array<void*, 4096> g_patched_vtable_storage = {};
std::size_t g_patched_vtable_entry_count = 0;
std::uint64_t g_attach_attempt_count = 0;
DWORD g_next_attach_attempt_tick = 0;

GetChildItemByNameFn g_get_child_item_by_name = nullptr;
CXWndShowFn g_cxwnd_show = nullptr;

int MONOMYTH_FASTCALL HookedWndNotification(
    void* this_context,
    void* /*edx_value*/,
    void* sender_window,
    std::uint32_t notification_code,
    void* payload) {
    if (notification_code == kXwmLclick && sender_window != nullptr) {
        void** sender_vtable = *reinterpret_cast<void***>(sender_window);
        if (sender_vtable != nullptr) {
            auto get_name_fn = reinterpret_cast<GetWindowNameFn>(
                sender_vtable[kCxWndVtableGetWindowNameOffset / sizeof(void*)]);
            if (get_name_fn != nullptr) {
                const void* name_cxstr = get_name_fn(sender_window);
                std::string sender_name = ReadCxStrAsAscii(name_cxstr);
                for (const auto& slot : kPetSlots) {
                    if (sender_name == slot.button_name) {
                        std::wstring message =
                            L"MonomythPetWndButtonTrace slot=";
                        message += std::to_wstring(slot.slot);
                        message += L" command=";
                        message += WidenAsciiLossy(slot.command);
                        message += L" control=";
                        message += WidenAsciiLossy(slot.button_name);
                        monomyth::logger::Log(message);
                        break;
                    }
                }
            }
        }
    }

    if (g_original_wnd_notification != nullptr) {
        auto original_fn = reinterpret_cast<WndNotificationFn>(
            g_original_wnd_notification);
        return original_fn(this_context, sender_window, notification_code, payload);
    }
    return 0;
}

bool ShouldLogAttachAttempt(std::uint64_t count) noexcept {
    return count <= 20 || (count % 100) == 0;
}

void LogAttachAttempt(
    const wchar_t* state,
    std::uintptr_t module_base,
    const WindowAnchor* anchor,
    void* anchor_window,
    void* parent_window,
    void* pet_window) noexcept {
    const std::uint64_t count = ++g_attach_attempt_count;
    if (!ShouldLogAttachAttempt(count)) {
        return;
    }

    std::wstring message = L"multipet_window: attach pending count=";
    message += std::to_wstring(count);
    message += L" state=\"";
    message += state == nullptr ? L"unknown" : state;
    message += L"\" anchor=\"";
    message += (anchor == nullptr || anchor->label == nullptr)
        ? L"unknown"
        : anchor->label;
    message += L"\" anchor_global=";
    message += HexPtr(module_base + (anchor == nullptr ? 0 : anchor->global_rva));
    message += L" anchor_window=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(anchor_window));
    message += L" parent_window=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(parent_window));
    message += L" pet_window=";
    message += HexPtr(reinterpret_cast<std::uintptr_t>(pet_window));
    monomyth::logger::Log(message);
}

void* TryReadWindowAnchor(
    std::uintptr_t module_base,
    const WindowAnchor& anchor) noexcept {
    void** global = reinterpret_cast<void**>(module_base + anchor.global_rva);
    return *global;
}

void* TryFindPetWindowFromAnchor(
    std::uintptr_t module_base,
    const WindowAnchor& anchor,
    void** anchor_window_out,
    void** parent_window_out) noexcept {
    if (anchor_window_out != nullptr) {
        *anchor_window_out = nullptr;
    }
    if (parent_window_out != nullptr) {
        *parent_window_out = nullptr;
    }

    void* anchor_window = TryReadWindowAnchor(module_base, anchor);
    if (anchor_window_out != nullptr) {
        *anchor_window_out = anchor_window;
    }
    if (anchor_window == nullptr) {
        return nullptr;
    }

    void* pet_window = g_get_child_item_by_name(
        anchor_window, kWindowScreenName);
    if (pet_window != nullptr) {
        return pet_window;
    }

    void* parent_window = *reinterpret_cast<void**>(
        reinterpret_cast<char*>(anchor_window) + kCxWndParentWindowOffset);
    if (parent_window_out != nullptr) {
        *parent_window_out = parent_window;
    }
    if (parent_window == nullptr) {
        return nullptr;
    }

    return g_get_child_item_by_name(parent_window, kWindowScreenName);
}

bool ShouldRunAttachProbe() noexcept {
    const DWORD now = GetTickCount();
    if (g_next_attach_attempt_tick != 0 &&
        static_cast<LONG>(now - g_next_attach_attempt_tick) < 0) {
        return false;
    }
    g_next_attach_attempt_tick = now + 1000;
    return true;
}

}  // namespace

bool Initialize(std::uintptr_t module_base) noexcept {
#if !MONOMYTH_ENABLE_MULTIPET_WINDOW
    (void)module_base;
    return false;
#else
    g_multipet_enabled = true;
    monomyth::logger::Log(L"multipet_window: feature enabled (built-in default)");

    g_get_child_item_by_name = reinterpret_cast<GetChildItemByNameFn>(
        module_base + kCXWndGetChildItemByNameRva);
    g_cxwnd_show = reinterpret_cast<CXWndShowFn>(
        module_base + kCXWndShowRva);

    monomyth::logger::Log(
        L"multipet_window: initialization complete");
    return true;
#endif
}

void TryAttachWindow(std::uintptr_t module_base) noexcept {
    if (!g_multipet_enabled || g_window_attached) {
        return;
    }
    if (module_base == 0 || g_get_child_item_by_name == nullptr) {
        return;
    }
    if (!ShouldRunAttachProbe()) {
        return;
    }

    const WindowAnchor* last_anchor = nullptr;
    void* last_anchor_window = nullptr;
    void* last_parent_window = nullptr;
    void* pet_window = nullptr;
    for (const auto& anchor : kWindowAnchors) {
        void* anchor_window = nullptr;
        void* parent_window = nullptr;
        pet_window = TryFindPetWindowFromAnchor(
            module_base,
            anchor,
            &anchor_window,
            &parent_window);
        last_anchor = &anchor;
        last_anchor_window = anchor_window;
        last_parent_window = parent_window;
        if (pet_window != nullptr) {
            LogAttachAttempt(
                L"custom_window_found",
                module_base,
                last_anchor,
                last_anchor_window,
                last_parent_window,
                pet_window);
            break;
        }
        if (anchor_window != nullptr) {
            LogAttachAttempt(
                parent_window == nullptr
                    ? L"anchor_parent_null"
                    : L"custom_window_missing",
                module_base,
                &anchor,
                anchor_window,
                parent_window,
                nullptr);
        }
    }
    if (pet_window == nullptr) {
        LogAttachAttempt(
            L"all_anchors_unavailable_or_missing",
            module_base,
            last_anchor,
            last_anchor_window,
            last_parent_window,
            nullptr);
        return;
    }

    void** original_vtable = *reinterpret_cast<void***>(pet_window);
    if (original_vtable == nullptr) {
        monomyth::logger::Log(
            L"multipet_window: window vtable is null");
        return;
    }

    g_original_vtable_ptr = original_vtable;
    g_original_wnd_notification = original_vtable[
        kCxWndVtableWndNotificationOffset / sizeof(void*)];

    std::size_t entry_count = 0;
    for (std::size_t i = 0; i < g_patched_vtable_storage.size(); ++i) {
        void* entry = original_vtable[i];
        if (entry == nullptr) {
            break;
        }
        g_patched_vtable_storage[i] = entry;
        ++entry_count;
    }
    g_patched_vtable_entry_count = entry_count;

    if (entry_count <= kCxWndVtableWndNotificationOffset / sizeof(void*)) {
        monomyth::logger::Log(
            L"multipet_window: vtable too short for WndNotification swap");
        return;
    }

    g_patched_vtable_storage[
        kCxWndVtableWndNotificationOffset / sizeof(void*)] =
            reinterpret_cast<void*>(&HookedWndNotification);

    DWORD old_protect = 0;
    VirtualProtect(
        &reinterpret_cast<void***>(pet_window)[0],
        sizeof(void*),
        PAGE_READWRITE,
        &old_protect);
    *reinterpret_cast<void***>(pet_window) = g_patched_vtable_storage.data();
    VirtualProtect(
        &reinterpret_cast<void***>(pet_window)[0],
        sizeof(void*),
        old_protect,
        &old_protect);

    g_window_instance = pet_window;
    g_window_attached = true;

    for (const auto& slot : kPetSlots) {
        void* button = g_get_child_item_by_name(pet_window, slot.button_name);
        std::wstring message = L"multipet_window: child lookup name=\"";
        message += WidenAsciiLossy(slot.button_name);
        message += L"\" found=";
        message += (button != nullptr) ? L"true" : L"false";
        monomyth::logger::Log(message);
    }

    std::wstring attach_message =
        L"multipet_window: window attached "
        L"window=";
    attach_message += HexPtr(reinterpret_cast<std::uintptr_t>(pet_window));
    attach_message += L" original_wnd_notification=";
    attach_message += HexPtr(
        reinterpret_cast<std::uintptr_t>(g_original_wnd_notification));
    attach_message += L" vtable_entries=";
    attach_message += std::to_wstring(entry_count);
    monomyth::logger::Log(attach_message);

    if (g_cxwnd_show != nullptr) {
        g_cxwnd_show(pet_window, 1, 1, 0);
    }
}

void Shutdown() noexcept {
    if (!g_window_attached || g_window_instance == nullptr) {
        g_multipet_enabled = false;
        return;
    }

    if (g_original_vtable_ptr != nullptr) {
        DWORD old_protect = 0;
        VirtualProtect(
            &reinterpret_cast<void***>(g_window_instance)[0],
            sizeof(void*),
            PAGE_READWRITE,
            &old_protect);
        *reinterpret_cast<void***>(g_window_instance) =
            g_original_vtable_ptr;
        VirtualProtect(
            &reinterpret_cast<void***>(g_window_instance)[0],
            sizeof(void*),
            old_protect,
            &old_protect);
    }

    g_window_attached = false;
    g_window_instance = nullptr;
    g_original_vtable_ptr = nullptr;
    g_original_wnd_notification = nullptr;
    g_patched_vtable_entry_count = 0;
    g_multipet_enabled = false;
    monomyth::logger::Log(L"multipet_window: shutdown");
}

}  // namespace monomyth::multipet_window

#endif
