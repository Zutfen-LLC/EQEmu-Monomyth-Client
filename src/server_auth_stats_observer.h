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
    bool has_activated_skill_mask_low = false;
    std::uint64_t activated_skill_mask_low = 0;
    bool duplicate_activated_skill_mask_low = false;
    bool has_activated_skill_mask_high = false;
    std::uint64_t activated_skill_mask_high = 0;
    bool duplicate_activated_skill_mask_high = false;
    bool has_extra_pet_hp_1000[2] = {false, false};
    std::uint64_t extra_pet_hp_1000[2] = {0, 0};
    bool duplicate_extra_pet_hp_1000[2] = {false, false};
    bool has_focused_pet_id = false;
    std::uint32_t focused_pet_id = 0;
    bool duplicate_focused_pet_id = false;
    std::uint32_t recognized_entry_count = 0;
    std::uint32_t unknown_entry_count = 0;
    const wchar_t* malformed_reason = L"not parsed";
};

struct Snapshot {
    bool has_classes_bitmask = false;
    std::uint32_t classes_bitmask = 0;
    bool has_activated_skill_mask_low = false;
    std::uint64_t activated_skill_mask_low = 0;
    bool has_activated_skill_mask_high = false;
    std::uint64_t activated_skill_mask_high = 0;
    bool has_extra_pet_hp_1000[2] = {false, false};
    std::uint64_t extra_pet_hp_1000[2] = {0, 0};
    bool has_focused_pet_id = false;
    std::uint32_t focused_pet_id = 0;
};

ParseResult ParsePayload(const void* payload, std::uint32_t payload_length) noexcept;
void ObserveReceivePayload(const void* payload, std::uint32_t payload_length) noexcept;
Snapshot GetSnapshot() noexcept;

}  // namespace monomyth::server_auth_stats
