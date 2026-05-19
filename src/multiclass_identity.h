#pragma once

#include <cstdint>

namespace monomyth::multiclass_identity {

constexpr unsigned int kFirstPlayableClassId = 1;
constexpr unsigned int kLastPlayableClassId = 16;
constexpr std::uint32_t kPlayableClassMask = 0x0000ffff;
constexpr std::uint32_t kClientItemPlayableClassMask = 0x0000ffff;

bool IsPlayableClassId(unsigned int class_id) noexcept;
std::uint32_t ClassBit(unsigned int class_id) noexcept;
std::uint32_t ClientItemClassBit(unsigned int class_id) noexcept;
bool IsPlayableClassMask(std::uint32_t class_mask) noexcept;
bool HasClass(std::uint32_t class_mask, unsigned int class_id) noexcept;
bool HasClientItemClass(std::uint32_t class_mask, unsigned int class_id) noexcept;
bool HasAuthoritativeClass(
    bool has_class_mask,
    std::uint32_t class_mask,
    unsigned int class_id) noexcept;
bool HasAnyAuthoritativeClientItemClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint32_t client_item_class_mask) noexcept;

}  // namespace monomyth::multiclass_identity
