#pragma once

#include <string>
#include <cstdint>

namespace monomyth::multiclass_cache {

std::wstring GetCacheFilePathForLogging() noexcept;
std::wstring GetServerConfigPathForLogging() noexcept;
bool TryGetCurrentServerName(std::string* server_name) noexcept;
bool TryLookupCharacterClassMask(
    const char* character_name,
    unsigned int expected_native_class_id,
    const char* expected_server_name,
    std::uint32_t* classes_bitmask) noexcept;
bool TryLookupUniqueClassMaskForNativeClassId(
    unsigned int native_class_id,
    const char* expected_server_name,
    std::uint32_t* classes_bitmask) noexcept;
bool StoreCharacterClassMask(
    const char* character_name,
    std::uint32_t classes_bitmask,
    unsigned int native_class_id,
    const char* server_name) noexcept;

}  // namespace monomyth::multiclass_cache
