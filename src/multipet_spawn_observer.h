#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace monomyth::multipet_spawn_observer {

struct ParsedSpawnEntry {
    bool valid = false;
    std::uint32_t spawn_id = 0;
    std::uint32_t owner_id = 0;
    std::string name;
};

struct Snapshot {
    std::uint32_t focused_pet_id = 0;
    std::array<bool, 2> has_other_pet_name = {false, false};
    std::array<std::string, 2> other_pet_name = {};
};

bool ParseSingleSpawnPayload(
    const void* payload,
    std::uint32_t payload_length,
    ParsedSpawnEntry* entry_out) noexcept;

void ObserveSpawnPayload(
    const void* payload,
    std::uint32_t payload_length) noexcept;

void ObserveDeleteSpawnPayload(
    const void* payload,
    std::uint32_t payload_length) noexcept;

Snapshot GetSnapshot() noexcept;
void Clear() noexcept;

}  // namespace monomyth::multipet_spawn_observer
