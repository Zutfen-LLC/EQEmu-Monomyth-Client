#include "remote_multipet_status.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>

#include "logger.h"

namespace monomyth::remote_multipet_status {
namespace {

constexpr std::uint32_t kHeaderBytes = sizeof(std::uint32_t);
constexpr std::uint32_t kMinimumEntryBytes = sizeof(std::uint32_t) + 1;

std::mutex g_snapshot_mutex;
Snapshot g_snapshot = {};
std::uint64_t g_valid_log_count = 0;

std::wstring WidenAsciiLossyLocal(const std::string& value) {
    std::wstring widened;
    widened.reserve(value.size());
    for (unsigned char ch : value) {
        widened.push_back(static_cast<wchar_t>(ch));
    }
    return widened;
}

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

bool TryReadU32(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::uint32_t* value_out) noexcept {
    if (payload == nullptr || value_out == nullptr ||
        offset > payload_length || (payload_length - offset) < sizeof(std::uint32_t)) {
        return false;
    }

    std::array<std::uint8_t, sizeof(std::uint32_t)> bytes = {};
    if (!SafeCopy(payload + offset, bytes.data(), bytes.size())) {
        return false;
    }

    *value_out = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
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

std::uint32_t MaxPossibleEntriesForPayloadLength(std::uint32_t payload_length) noexcept {
    if (payload_length <= kHeaderBytes) {
        return 0;
    }

    return (payload_length - kHeaderBytes) / kMinimumEntryBytes;
}

bool IsAcceptableSlot(std::uint32_t slot) noexcept {
    return slot < kRemoteMultiPetStatusSlotCount;
}

void StoreSnapshot(const ParseResult& result) noexcept {
    Snapshot snapshot = {};
    snapshot.has_name = result.has_name;
    snapshot.pet_name = result.pet_name;

    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    g_snapshot = std::move(snapshot);
}

void LogValid(const ParseResult& result) {
    const std::uint64_t count = ++g_valid_log_count;
    if (count > 20 && (count % 100) != 0) {
        return;
    }

    std::wstringstream message;
    message << L"RemoteMultiPetStatus valid=true"
            << L" log_index=" << count
            << L" declared_entry_count=" << result.declared_entry_count
            << L" parsed_entry_count=" << result.parsed_entry_count
            << L" accepted_entry_count=" << result.accepted_entry_count
            << L" rejected_entry_count=" << result.rejected_entry_count
            << L" slot0_has_name=" << (result.has_name[0] ? L"true" : L"false")
            << L" slot0_name=\"" << WidenAsciiLossyLocal(result.pet_name[0]) << L"\""
            << L" slot1_has_name=" << (result.has_name[1] ? L"true" : L"false")
            << L" slot1_name=\"" << WidenAsciiLossyLocal(result.pet_name[1]) << L"\"";
    monomyth::logger::Log(message.str());
}

}  // namespace

ParseResult ParsePayload(const void* payload, std::uint32_t payload_length) noexcept {
    ParseResult result = {};
    if (payload == nullptr || payload_length < kHeaderBytes) {
        return result;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    std::uint32_t entry_count = 0;
    if (!TryReadU32(bytes, payload_length, 0, &entry_count)) {
        return result;
    }

    result.declared_entry_count = entry_count;
    if (entry_count > MaxPossibleEntriesForPayloadLength(payload_length)) {
        return result;
    }

    std::uint32_t cursor = kHeaderBytes;
    for (std::uint32_t entry_index = 0; entry_index < entry_count; ++entry_index) {
        std::uint32_t slot = 0;
        std::string name;
        if (!TryReadU32(bytes, payload_length, cursor, &slot)) {
            return result;
        }
        cursor += sizeof(std::uint32_t);
        if (!TryReadCString(bytes, payload_length, cursor, &name, &cursor)) {
            return result;
        }

        ++result.parsed_entry_count;
        if (!IsAcceptableSlot(slot)) {
            ++result.rejected_entry_count;
            continue;
        }

        result.has_name[slot] = !name.empty();
        result.pet_name[slot] = std::move(name);
        ++result.accepted_entry_count;
    }

    result.valid = true;
    return result;
}

void ObserveReceivePayload(const void* payload, std::uint32_t payload_length) noexcept {
    const ParseResult result = ParsePayload(payload, payload_length);
    if (!result.valid) {
        return;
    }

    StoreSnapshot(result);
    LogValid(result);
}

Snapshot GetSnapshot() noexcept {
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    return g_snapshot;
}

void Clear() noexcept {
    std::lock_guard<std::mutex> lock(g_snapshot_mutex);
    g_snapshot = {};
}

}  // namespace monomyth::remote_multipet_status
