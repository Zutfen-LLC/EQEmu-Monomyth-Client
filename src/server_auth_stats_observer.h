#pragma once

#include <cstdint>

namespace monomyth::server_auth_stats {

struct ParseResult {
    bool valid = false;
    std::uint32_t count = 0;
    bool has_classes_bitmask = false;
    std::uint32_t classes_bitmask = 0;
    bool invalid_classes_bitmask = false;
    std::uint64_t invalid_classes_bitmask_value = 0;
    bool duplicate_classes_bitmask = false;
    const wchar_t* malformed_reason = L"not parsed";
};

struct Snapshot {
    bool has_classes_bitmask = false;
    std::uint32_t classes_bitmask = 0;
};

ParseResult ParsePayload(const void* payload, std::uint32_t payload_length) noexcept;
void ObserveReceivePayload(const void* payload, std::uint32_t payload_length) noexcept;
Snapshot GetSnapshot() noexcept;

}  // namespace monomyth::server_auth_stats
