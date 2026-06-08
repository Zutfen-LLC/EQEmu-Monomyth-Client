#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace monomyth::remote_multipet_status {

constexpr std::uint32_t kRemoteMultiPetStatusOpcode = 0xd7f2;
constexpr std::size_t kRemoteMultiPetStatusSlotCount = 2;

struct ParsedEntry {
    std::uint32_t slot = 0;
    std::string pet_name;
};

struct ParseResult {
    bool valid = false;
    std::uint32_t declared_entry_count = 0;
    std::uint32_t parsed_entry_count = 0;
    std::uint32_t accepted_entry_count = 0;
    std::uint32_t rejected_entry_count = 0;
    std::array<bool, kRemoteMultiPetStatusSlotCount> has_name = {false, false};
    std::array<std::string, kRemoteMultiPetStatusSlotCount> pet_name = {};
};

struct Snapshot {
    std::array<bool, kRemoteMultiPetStatusSlotCount> has_name = {false, false};
    std::array<std::string, kRemoteMultiPetStatusSlotCount> pet_name = {};
};

ParseResult ParsePayload(const void* payload, std::uint32_t payload_length) noexcept;
void ObserveReceivePayload(const void* payload, std::uint32_t payload_length) noexcept;
Snapshot GetSnapshot() noexcept;
void Clear() noexcept;

}  // namespace monomyth::remote_multipet_status
