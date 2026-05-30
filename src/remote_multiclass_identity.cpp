#include "remote_multiclass_identity.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

#include "multiclass_identity.h"

namespace monomyth::remote_multiclass_identity {
namespace {

constexpr std::uint32_t kRemoteIdentityHeaderBytes = sizeof(std::uint32_t);
constexpr std::uint32_t kRemoteIdentityMinimumEntryBytes = 9;

struct StoredEntry {
    std::string normalized_server_name;
    std::string normalized_character_name;
    std::uint8_t native_class_id = 0;
    std::uint32_t classes_bitmask = 0;
};

std::mutex g_remote_identity_mutex;
std::vector<StoredEntry> g_remote_identity_entries;

std::string NormalizeName(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        if (ch == '\0') {
            break;
        }

        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

bool TryReadU32(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::uint32_t* value_out) noexcept {
    if (payload == nullptr || value_out == nullptr ||
        offset > payload_length || (payload_length - offset) < sizeof(std::uint32_t)) {
        return false;
    }

    std::uint32_t value = 0;
    std::memcpy(&value, payload + offset, sizeof(value));
    *value_out = value;
    return true;
}

bool TryReadCString(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::string* value_out,
    std::uint32_t* next_offset_out) noexcept {
    if (payload == nullptr || value_out == nullptr || next_offset_out == nullptr ||
        offset >= payload_length) {
        return false;
    }

    std::uint32_t cursor = offset;
    while (cursor < payload_length && payload[cursor] != 0) {
        ++cursor;
    }
    if (cursor >= payload_length) {
        return false;
    }

    value_out->assign(
        reinterpret_cast<const char*>(payload + offset),
        reinterpret_cast<const char*>(payload + cursor));
    *next_offset_out = cursor + 1;
    return true;
}

bool IsAcceptableRemoteIdentityEntry(
    std::string_view character_name,
    unsigned int native_class_id,
    std::uint32_t classes_bitmask) noexcept {
    if (character_name.empty() ||
        !monomyth::multiclass_identity::IsPlayableClassId(native_class_id) ||
        classes_bitmask == 0 ||
        !monomyth::multiclass_identity::IsPlayableClassMask(classes_bitmask) ||
        !monomyth::multiclass_identity::HasClass(classes_bitmask, native_class_id)) {
        return false;
    }
    return true;
}

std::uint32_t MaxPossibleEntriesForPayloadLength(
    std::uint32_t payload_length) noexcept {
    if (payload_length <= kRemoteIdentityHeaderBytes) {
        return 0;
    }

    const std::uint32_t remaining_bytes = payload_length - kRemoteIdentityHeaderBytes;
    return remaining_bytes / kRemoteIdentityMinimumEntryBytes;
}

}  // namespace

ParseResult ParsePayload(const void* payload, std::uint32_t payload_length) noexcept {
    ParseResult result = {};
    if (payload == nullptr || payload_length < kRemoteIdentityHeaderBytes) {
        return result;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    std::uint32_t entry_count = 0;
    if (!TryReadU32(bytes, payload_length, 0, &entry_count)) {
        return result;
    }

    result.declared_entry_count = entry_count;
    const std::uint32_t max_possible_entries =
        MaxPossibleEntriesForPayloadLength(payload_length);
    if (entry_count > max_possible_entries) {
        return result;
    }

    std::uint32_t cursor = kRemoteIdentityHeaderBytes;
    result.entries.reserve(entry_count);
    for (std::uint32_t entry_index = 0; entry_index < entry_count; ++entry_index) {
        ParsedEntry entry = {};
        if (!TryReadCString(
                bytes,
                payload_length,
                cursor,
                &entry.character_name,
                &cursor) ||
            !TryReadU32(bytes, payload_length, cursor, &entry.native_class_id) ||
            !TryReadU32(
                bytes,
                payload_length,
                cursor + sizeof(std::uint32_t),
                &entry.classes_bitmask)) {
            result.valid = false;
            result.entries.clear();
            result.accepted_entry_count = 0;
            result.rejected_entry_count = 0;
            return result;
        }

        cursor += sizeof(std::uint32_t) * 2u;
        ++result.parsed_entry_count;
        if (!IsAcceptableRemoteIdentityEntry(
                entry.character_name,
                entry.native_class_id,
                entry.classes_bitmask)) {
            ++result.rejected_entry_count;
            continue;
        }

        result.entries.push_back(std::move(entry));
        ++result.accepted_entry_count;
    }

    result.valid = true;
    return result;
}

bool StoreCharacterClassMask(
    const char* server_name,
    const char* character_name,
    unsigned int native_class_id,
    std::uint32_t classes_bitmask) noexcept {
    if (server_name == nullptr || character_name == nullptr) {
        return false;
    }

    const std::string normalized_server_name = NormalizeName(server_name);
    const std::string normalized_character_name = NormalizeName(character_name);
    if (normalized_server_name.empty() ||
        normalized_character_name.empty() ||
        !IsAcceptableRemoteIdentityEntry(
            character_name,
            native_class_id,
            classes_bitmask)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_remote_identity_mutex);
    for (StoredEntry& entry : g_remote_identity_entries) {
        if (entry.normalized_server_name == normalized_server_name &&
            entry.normalized_character_name == normalized_character_name) {
            entry.native_class_id = static_cast<std::uint8_t>(native_class_id);
            entry.classes_bitmask = classes_bitmask;
            return true;
        }
    }

    StoredEntry entry = {};
    entry.normalized_server_name = normalized_server_name;
    entry.normalized_character_name = normalized_character_name;
    entry.native_class_id = static_cast<std::uint8_t>(native_class_id);
    entry.classes_bitmask = classes_bitmask;
    g_remote_identity_entries.push_back(std::move(entry));
    return true;
}

bool TryLookupCharacterClassMask(
    const char* server_name,
    const char* character_name,
    unsigned int expected_native_class_id,
    std::uint32_t* classes_bitmask) noexcept {
    if (server_name == nullptr || character_name == nullptr || classes_bitmask == nullptr) {
        return false;
    }

    const std::string normalized_server_name = NormalizeName(server_name);
    const std::string normalized_character_name = NormalizeName(character_name);
    if (normalized_server_name.empty() || normalized_character_name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_remote_identity_mutex);
    for (const StoredEntry& entry : g_remote_identity_entries) {
        if (entry.normalized_server_name != normalized_server_name ||
            entry.normalized_character_name != normalized_character_name) {
            continue;
        }

        if (expected_native_class_id != 0 &&
            entry.native_class_id != 0 &&
            entry.native_class_id != expected_native_class_id) {
            return false;
        }
        if (!IsAcceptableRemoteIdentityEntry(
                character_name,
                entry.native_class_id,
                entry.classes_bitmask)) {
            return false;
        }

        *classes_bitmask = entry.classes_bitmask;
        return true;
    }

    return false;
}

void Clear() noexcept {
    std::lock_guard<std::mutex> lock(g_remote_identity_mutex);
    g_remote_identity_entries.clear();
}

std::size_t GetStoredEntryCountForTesting() noexcept {
    std::lock_guard<std::mutex> lock(g_remote_identity_mutex);
    return g_remote_identity_entries.size();
}

}  // namespace monomyth::remote_multiclass_identity
