#include "multipet_spawn_observer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "logger.h"
#include "server_auth_stats_observer.h"

namespace monomyth::multipet_spawn_observer {
namespace {

struct ObservedPetEntry {
    std::uint32_t spawn_id = 0;
    std::uint32_t owner_id = 0;
    std::uint64_t first_seen_order = 0;
    bool active = false;
    std::string name;
};

std::mutex g_mutex;
std::vector<ObservedPetEntry> g_entries;
std::uint64_t g_next_first_seen_order = 1;
std::uint64_t g_spawn_observe_log_count = 0;
std::uint64_t g_delete_observe_log_count = 0;

bool ShouldLog(std::uint64_t count) noexcept {
    return count <= 20 || (count % 100) == 0;
}

bool TryReadBytes(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    void* destination,
    std::size_t length) noexcept {
    if (payload == nullptr || destination == nullptr || length == 0 ||
        offset > payload_length ||
        static_cast<std::uint64_t>(offset) + length > payload_length) {
        return false;
    }

    std::memcpy(destination, payload + offset, length);
    return true;
}

bool TryReadU8(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::uint8_t* value_out) noexcept {
    return TryReadBytes(payload, payload_length, offset, value_out, sizeof(*value_out));
}

bool TryReadU32(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::uint32_t* value_out) noexcept {
    return TryReadBytes(payload, payload_length, offset, value_out, sizeof(*value_out));
}

bool TryAdvance(
    std::uint32_t payload_length,
    std::uint32_t* cursor,
    std::uint32_t bytes) noexcept {
    if (cursor == nullptr || *cursor > payload_length ||
        static_cast<std::uint64_t>(*cursor) + bytes > payload_length) {
        return false;
    }

    *cursor += bytes;
    return true;
}

bool TryReadCString(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t* cursor,
    std::string* value_out) noexcept {
    if (payload == nullptr || cursor == nullptr || value_out == nullptr ||
        *cursor >= payload_length) {
        return false;
    }

    const std::uint32_t start = *cursor;
    std::uint32_t end = start;
    while (end < payload_length && payload[end] != 0) {
        ++end;
    }
    if (end >= payload_length) {
        return false;
    }

    value_out->assign(
        reinterpret_cast<const char*>(payload + start),
        reinterpret_cast<const char*>(payload + end));
    *cursor = end + 1;
    return true;
}

ObservedPetEntry* FindEntryBySpawnId(std::uint32_t spawn_id) noexcept {
    for (auto& entry : g_entries) {
        if (entry.spawn_id == spawn_id) {
            return &entry;
        }
    }

    return nullptr;
}

std::wstring WidenAsciiLossy(std::string_view value) {
    std::wstring widened;
    widened.reserve(value.size());
    for (unsigned char ch : value) {
        widened.push_back(static_cast<wchar_t>(ch));
    }
    return widened;
}

std::string BuildDisplayName(std::string_view raw_name) {
    std::string display(raw_name);
    if (display.size() > 3 && display.ends_with("000")) {
        display.resize(display.size() - 3);
    }
    return display;
}

void LogSpawnObserve(const ParsedSpawnEntry& entry) {
    const std::uint64_t count = ++g_spawn_observe_log_count;
    if (!ShouldLog(count)) {
        return;
    }

    std::wstringstream message;
    message << L"MultiPetSpawnRosterObserve"
            << L" count=" << count
            << L" spawn_id=" << entry.spawn_id
            << L" owner_id=" << entry.owner_id
            << L" name=\"" << WidenAsciiLossy(entry.name) << L"\"";
    monomyth::logger::Log(message.str());
}

void LogDeleteObserve(std::uint32_t spawn_id, bool removed) {
    const std::uint64_t count = ++g_delete_observe_log_count;
    if (!ShouldLog(count)) {
        return;
    }

    std::wstringstream message;
    message << L"MultiPetSpawnRosterDelete"
            << L" count=" << count
            << L" spawn_id=" << spawn_id
            << L" removed=" << (removed ? L"true" : L"false");
    monomyth::logger::Log(message.str());
}

}  // namespace

bool ParseSingleSpawnPayload(
    const void* payload,
    std::uint32_t payload_length,
    ParsedSpawnEntry* entry_out) noexcept {
    if (entry_out == nullptr) {
        return false;
    }

    *entry_out = {};
    if (payload == nullptr || payload_length < 8) {
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    std::uint32_t cursor = 0;

    if (!TryReadCString(bytes, payload_length, &cursor, &entry_out->name) ||
        entry_out->name.empty()) {
        return false;
    }

    if (!TryReadU32(bytes, payload_length, cursor, &entry_out->spawn_id) ||
        !TryAdvance(payload_length, &cursor, sizeof(std::uint32_t))) {
        return false;
    }

    if (!TryAdvance(payload_length, &cursor, 1 + 4 + 1 + 4 + 1 + 4 + 4)) {
        return false;
    }

    std::uint8_t property_count = 0;
    if (!TryReadU8(bytes, payload_length, cursor, &property_count) ||
        !TryAdvance(payload_length, &cursor, sizeof(property_count))) {
        return false;
    }

    if (!TryAdvance(
            payload_length,
            &cursor,
            static_cast<std::uint32_t>(property_count) * sizeof(std::uint32_t))) {
        return false;
    }

    if (!TryAdvance(
            payload_length,
            &cursor,
            7 + 12 + 4 + 4 + 1 + 4 + 4 + 4 + 1 + 4 + 4 + 4 + 1 + 1 + 1 + 1 + 1)) {
        return false;
    }

    std::string ignored_last_name;
    if (!TryReadCString(bytes, payload_length, &cursor, &ignored_last_name)) {
        return false;
    }

    if (!TryAdvance(payload_length, &cursor, 4 + 1 + 1) ||
        !TryReadU32(bytes, payload_length, cursor, &entry_out->owner_id)) {
        return false;
    }

    entry_out->valid = true;
    return true;
}

void ObserveSpawnPayload(
    const void* payload,
    std::uint32_t payload_length) noexcept {
    ParsedSpawnEntry entry = {};
    if (!ParseSingleSpawnPayload(payload, payload_length, &entry) ||
        !entry.valid || entry.owner_id == 0 || entry.name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (ObservedPetEntry* existing = FindEntryBySpawnId(entry.spawn_id)) {
        existing->owner_id = entry.owner_id;
        existing->name = entry.name;
        existing->active = true;
    } else {
        g_entries.push_back(ObservedPetEntry{
            entry.spawn_id,
            entry.owner_id,
            g_next_first_seen_order++,
            true,
            entry.name});
    }

    LogSpawnObserve(entry);
}

void ObserveDeleteSpawnPayload(
    const void* payload,
    std::uint32_t payload_length) noexcept {
    if (payload == nullptr || payload_length < sizeof(std::uint32_t)) {
        return;
    }

    std::uint32_t spawn_id = 0;
    if (!TryReadU32(
            static_cast<const std::uint8_t*>(payload),
            payload_length,
            0,
            &spawn_id)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    bool removed = false;
    if (ObservedPetEntry* existing = FindEntryBySpawnId(spawn_id)) {
        existing->active = false;
        removed = true;
    }
    LogDeleteObserve(spawn_id, removed);
}

Snapshot GetSnapshot() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);

    Snapshot snapshot = {};
    const auto auth_snapshot = monomyth::server_auth_stats::GetSnapshot();
    snapshot.focused_pet_id = auth_snapshot.focused_pet_id;
    if (!auth_snapshot.has_focused_pet_id || snapshot.focused_pet_id == 0) {
        return snapshot;
    }

    const ObservedPetEntry* focused_entry = nullptr;
    for (const auto& entry : g_entries) {
        if (entry.active && entry.spawn_id == snapshot.focused_pet_id) {
            focused_entry = &entry;
            break;
        }
    }
    if (focused_entry == nullptr || focused_entry->owner_id == 0) {
        return snapshot;
    }

    std::vector<const ObservedPetEntry*> candidates;
    for (const auto& entry : g_entries) {
        if (!entry.active || entry.owner_id != focused_entry->owner_id ||
            entry.spawn_id == snapshot.focused_pet_id || entry.name.empty()) {
            continue;
        }
        candidates.push_back(&entry);
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const ObservedPetEntry* a, const ObservedPetEntry* b) {
            if (a->first_seen_order != b->first_seen_order) {
                return a->first_seen_order < b->first_seen_order;
            }
            return a->spawn_id < b->spawn_id;
        });

    for (std::size_t i = 0; i < snapshot.has_other_pet_name.size() && i < candidates.size(); ++i) {
        snapshot.has_other_pet_name[i] = true;
        snapshot.has_other_pet_spawn_id[i] = true;
        snapshot.other_pet_name[i] = BuildDisplayName(candidates[i]->name);
        snapshot.other_pet_spawn_id[i] = candidates[i]->spawn_id;
    }

    return snapshot;
}

void Clear() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
    g_next_first_seen_order = 1;
    g_spawn_observe_log_count = 0;
    g_delete_observe_log_count = 0;
}

}  // namespace monomyth::multipet_spawn_observer
