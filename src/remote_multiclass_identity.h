#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace monomyth::remote_multiclass_identity {

constexpr std::uint32_t kRemoteMulticlassIdentityOpcode = 0xd7f1;

struct ParsedEntry {
    std::string character_name;
    std::uint32_t native_class_id = 0;
    std::uint32_t classes_bitmask = 0;
};

struct ParseResult {
    bool valid = false;
    std::uint32_t declared_entry_count = 0;
    std::uint32_t parsed_entry_count = 0;
    std::uint32_t accepted_entry_count = 0;
    std::uint32_t rejected_entry_count = 0;
    std::vector<ParsedEntry> entries;
};

ParseResult ParsePayload(const void* payload, std::uint32_t payload_length) noexcept;
bool StoreCharacterClassMask(
    const char* server_name,
    const char* character_name,
    unsigned int native_class_id,
    std::uint32_t classes_bitmask) noexcept;
bool TryLookupCharacterClassMask(
    const char* server_name,
    const char* character_name,
    unsigned int expected_native_class_id,
    std::uint32_t* classes_bitmask) noexcept;
void Clear() noexcept;
std::size_t GetStoredEntryCountForTesting() noexcept;

}  // namespace monomyth::remote_multiclass_identity
