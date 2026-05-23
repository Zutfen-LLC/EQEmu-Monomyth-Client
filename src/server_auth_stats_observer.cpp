#include "server_auth_stats_observer.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include "logger.h"

namespace monomyth::server_auth_stats {
namespace {

constexpr std::uint32_t kStatClassesBitmaskKey = 1;
constexpr std::uint32_t kStatActivatedSkillMaskLowKey = 2;
constexpr std::uint32_t kStatActivatedSkillMaskHighKey = 3;
constexpr std::uint32_t kHeaderBytes = 4;
constexpr std::uint32_t kEntryBytes = 12;

std::atomic<bool> g_has_classes_bitmask = false;
std::atomic<std::uint32_t> g_classes_bitmask = 0;
std::atomic<bool> g_has_activated_skill_mask_low = false;
std::atomic<std::uint64_t> g_activated_skill_mask_low = 0;
std::atomic<bool> g_has_activated_skill_mask_high = false;
std::atomic<std::uint64_t> g_activated_skill_mask_high = 0;

struct ClassNameEntry {
    std::uint32_t bit;
    const wchar_t* name;
};

constexpr std::array<ClassNameEntry, 16> kClassNames = {{
    {0x00000001, L"Warrior"},
    {0x00000002, L"Cleric"},
    {0x00000004, L"Paladin"},
    {0x00000008, L"Ranger"},
    {0x00000010, L"ShadowKnight"},
    {0x00000020, L"Druid"},
    {0x00000040, L"Monk"},
    {0x00000080, L"Bard"},
    {0x00000100, L"Rogue"},
    {0x00000200, L"Shaman"},
    {0x00000400, L"Necromancer"},
    {0x00000800, L"Wizard"},
    {0x00001000, L"Magician"},
    {0x00002000, L"Enchanter"},
    {0x00004000, L"Beastlord"},
    {0x00008000, L"Berserker"},
}};

bool SafeCopy(const void* source, void* destination, std::size_t length) noexcept {
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

bool ReadU32Le(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::uint32_t* value) noexcept {
    if (payload == nullptr || value == nullptr || offset > payload_length ||
        payload_length - offset < sizeof(std::uint32_t)) {
        return false;
    }

    std::array<std::uint8_t, sizeof(std::uint32_t)> bytes = {};
    if (!SafeCopy(payload + offset, bytes.data(), bytes.size())) {
        return false;
    }

    *value = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

bool ReadU64Le(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::uint64_t* value) noexcept {
    if (payload == nullptr || value == nullptr || offset > payload_length ||
        payload_length - offset < sizeof(std::uint64_t)) {
        return false;
    }

    std::array<std::uint8_t, sizeof(std::uint64_t)> bytes = {};
    if (!SafeCopy(payload + offset, bytes.data(), bytes.size())) {
        return false;
    }

    std::uint64_t parsed = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        parsed |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    *value = parsed;
    return true;
}

std::wstring Hex32(std::uint32_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::setw(8) << std::setfill(L'0') << value;
    return stream.str();
}

std::wstring Hex64(std::uint64_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::setw(16) << std::setfill(L'0') << value;
    return stream.str();
}

std::wstring FormatClassNames(std::uint32_t bitmask) {
    std::wstring names;
    for (const ClassNameEntry& entry : kClassNames) {
        if ((bitmask & entry.bit) == 0) {
            continue;
        }

        if (!names.empty()) {
            names += L"|";
        }
        names += entry.name;
    }

    return names.empty() ? L"none" : names;
}

void StoreClassesBitmask(std::uint32_t bitmask) noexcept {
    g_classes_bitmask.store(bitmask);
    g_has_classes_bitmask.store(true);
}

void StoreActivatedSkillMasks(const ParseResult& result) noexcept {
    g_activated_skill_mask_low.store(result.activated_skill_mask_low);
    g_activated_skill_mask_high.store(result.activated_skill_mask_high);
    g_has_activated_skill_mask_low.store(result.has_activated_skill_mask_low);
    g_has_activated_skill_mask_high.store(result.has_activated_skill_mask_high);
}

void LogMalformed(std::uint32_t payload_length, const wchar_t* reason) {
    std::wstringstream message;
    message
        << L"ServerAuthStats malformed=true payload_length=" << payload_length
        << L" reason=\"" << ((reason == nullptr || reason[0] == L'\0') ? L"unknown" : reason)
        << L"\"";
    monomyth::logger::Log(message.str());
}

void LogInvalidClassesBitmask(std::uint32_t count, std::uint64_t value) {
    std::wstringstream message;
    message
        << L"ServerAuthStats invalid_statClassesBitmask=true"
        << L" count=" << count
        << L" value=" << Hex64(value)
        << L" reason=\"value_exceeds_uint32\"";
    monomyth::logger::Log(message.str());
}

void LogValid(const ParseResult& result) {
    std::wstringstream message;
    message
        << L"ServerAuthStats valid=true"
        << L" count=" << result.count
        << L" has_statClassesBitmask=" << (result.has_classes_bitmask ? L"true" : L"false");
    if (result.has_classes_bitmask) {
        message
            << L" statClassesBitmask=" << Hex32(result.classes_bitmask)
            << L" class_names=\"" << FormatClassNames(result.classes_bitmask) << L"\"";
    }
    if (result.duplicate_classes_bitmask) {
        message << L" duplicate_statClassesBitmask=true";
    }
    message
        << L" has_statActivatedSkillMaskLow="
        << (result.has_activated_skill_mask_low ? L"true" : L"false");
    if (result.has_activated_skill_mask_low) {
        message
            << L" statActivatedSkillMaskLow="
            << Hex64(result.activated_skill_mask_low);
    }
    if (result.duplicate_activated_skill_mask_low) {
        message << L" duplicate_statActivatedSkillMaskLow=true";
    }
    message
        << L" has_statActivatedSkillMaskHigh="
        << (result.has_activated_skill_mask_high ? L"true" : L"false");
    if (result.has_activated_skill_mask_high) {
        message
            << L" statActivatedSkillMaskHigh="
            << Hex64(result.activated_skill_mask_high);
    }
    if (result.duplicate_activated_skill_mask_high) {
        message << L" duplicate_statActivatedSkillMaskHigh=true";
    }
    monomyth::logger::Log(message.str());
}

}  // namespace

ParseResult ParsePayload(const void* payload, std::uint32_t payload_length) noexcept {
    ParseResult result = {};
    result.malformed_reason = L"unknown";

    if (payload == nullptr && payload_length != 0) {
        result.malformed_reason = L"null_payload";
        return result;
    }
    if (payload_length < kHeaderBytes) {
        result.malformed_reason = L"payload_too_short_for_count";
        return result;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    if (!ReadU32Le(bytes, payload_length, 0, &result.count)) {
        result.malformed_reason = L"count_read_fault";
        return result;
    }

    const std::uint32_t body_length = payload_length - kHeaderBytes;
    const std::uint32_t max_entries_from_payload = body_length / kEntryBytes;
    if (result.count > max_entries_from_payload) {
        result.malformed_reason = L"truncated_entries";
        return result;
    }
    const std::uint64_t expected_length =
        static_cast<std::uint64_t>(kHeaderBytes) +
        (static_cast<std::uint64_t>(result.count) * kEntryBytes);
    if (expected_length != payload_length) {
        result.malformed_reason = L"unexpected_trailing_bytes";
        return result;
    }

    for (std::uint32_t i = 0; i < result.count; ++i) {
        const std::uint64_t entry_offset =
            static_cast<std::uint64_t>(kHeaderBytes) +
            (static_cast<std::uint64_t>(i) * kEntryBytes);
        if (entry_offset > std::numeric_limits<std::uint32_t>::max()) {
            result.malformed_reason = L"entry_offset_overflow";
            return result;
        }

        const std::uint32_t key_offset = static_cast<std::uint32_t>(entry_offset);
        const std::uint32_t value_offset = key_offset + sizeof(std::uint32_t);
        std::uint32_t stat_key = 0;
        std::uint64_t stat_value = 0;
        if (!ReadU32Le(bytes, payload_length, key_offset, &stat_key)) {
            result.malformed_reason = L"stat_key_read_fault";
            return result;
        }
        if (!ReadU64Le(bytes, payload_length, value_offset, &stat_value)) {
            result.malformed_reason = L"stat_value_read_fault";
            return result;
        }

        switch (stat_key) {
        case kStatClassesBitmaskKey:
            if (stat_value > std::numeric_limits<std::uint32_t>::max()) {
                result.invalid_classes_bitmask = true;
                result.invalid_classes_bitmask_value = stat_value;
                continue;
            }

            result.duplicate_classes_bitmask = result.has_classes_bitmask;
            result.has_classes_bitmask = true;
            result.classes_bitmask = static_cast<std::uint32_t>(stat_value);
            break;
        case kStatActivatedSkillMaskLowKey:
            result.duplicate_activated_skill_mask_low =
                result.has_activated_skill_mask_low;
            result.has_activated_skill_mask_low = true;
            result.activated_skill_mask_low = stat_value;
            break;
        case kStatActivatedSkillMaskHighKey:
            result.duplicate_activated_skill_mask_high =
                result.has_activated_skill_mask_high;
            result.has_activated_skill_mask_high = true;
            result.activated_skill_mask_high = stat_value;
            break;
        default:
            break;
        }
    }

    result.valid = true;
    result.malformed_reason = L"";
    return result;
}

void ObserveReceivePayload(const void* payload, std::uint32_t payload_length) noexcept {
    try {
        const ParseResult result = ParsePayload(payload, payload_length);
        if (!result.valid) {
            LogMalformed(payload_length, result.malformed_reason);
            return;
        }

        if (result.invalid_classes_bitmask) {
            LogInvalidClassesBitmask(result.count, result.invalid_classes_bitmask_value);
        }
        if (result.has_classes_bitmask) {
            StoreClassesBitmask(result.classes_bitmask);
        }
        StoreActivatedSkillMasks(result);
        LogValid(result);
    } catch (...) {
        monomyth::logger::Log(L"ServerAuthStats malformed=true reason=\"handler_exception\"");
    }
}

Snapshot GetSnapshot() noexcept {
    Snapshot snapshot = {};
    snapshot.has_classes_bitmask = g_has_classes_bitmask.load();
    snapshot.classes_bitmask = g_classes_bitmask.load();
    snapshot.has_activated_skill_mask_low = g_has_activated_skill_mask_low.load();
    snapshot.activated_skill_mask_low = g_activated_skill_mask_low.load();
    snapshot.has_activated_skill_mask_high = g_has_activated_skill_mask_high.load();
    snapshot.activated_skill_mask_high = g_activated_skill_mask_high.load();
    return snapshot;
}

}  // namespace monomyth::server_auth_stats
