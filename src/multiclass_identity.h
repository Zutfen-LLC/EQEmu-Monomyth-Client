#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace monomyth::multiclass_identity {

constexpr unsigned int kFirstPlayableClassId = 1;
constexpr unsigned int kLastPlayableClassId = 16;
constexpr std::uint32_t kPlayableClassMask = 0x0000ffff;
constexpr std::uint32_t kClientItemPlayableClassMask = 0x0000ffff;
constexpr std::size_t kPlayableClassCount = 16;

enum class ClassDisplayStyle {
    kFullName,
    kThreeLetterCode,
};

enum class HandEquipConflict {
    kNone,
    kPrimaryTwoHandedBlockedByOccupiedSecondary,
    kSecondaryBlockedByPrimaryTwoHanded,
};

struct OrderedClassIds {
    std::array<unsigned int, kPlayableClassCount> class_ids = {};
    std::size_t count = 0;
};

bool IsPlayableClassId(unsigned int class_id) noexcept;
bool IsCastingClassId(unsigned int class_id) noexcept;
bool IsClientBaseManaIntClassId(unsigned int class_id) noexcept;
bool IsClientBaseManaWisClassId(unsigned int class_id) noexcept;
bool IsHeroicIntManaClassId(unsigned int class_id) noexcept;
bool IsHeroicWisManaClassId(unsigned int class_id) noexcept;
std::uint32_t ClassBit(unsigned int class_id) noexcept;
std::uint32_t ClientItemClassBit(unsigned int class_id) noexcept;
bool IsPlayableClassMask(std::uint32_t class_mask) noexcept;
bool HasClass(std::uint32_t class_mask, unsigned int class_id) noexcept;
bool HasClientItemClass(std::uint32_t class_mask, unsigned int class_id) noexcept;
bool ClassMaskUsesClientBaseMana(std::uint32_t class_mask) noexcept;
bool ClassMaskUsesClientHeroicIntMana(std::uint32_t class_mask) noexcept;
bool ClassMaskUsesClientHeroicWisMana(std::uint32_t class_mask) noexcept;
bool HasAuthoritativeClass(
    bool has_class_mask,
    std::uint32_t class_mask,
    unsigned int class_id) noexcept;
bool HasAnyAuthoritativeClientItemClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint32_t client_item_class_mask) noexcept;
bool HasAnyAuthoritativeCastingClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask) noexcept;
bool HasAnyAuthoritativeDualWieldClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask) noexcept;
bool HasAuthoritativeOffhandWeaponClassAndDualWield(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint32_t client_item_class_mask,
    bool has_dual_wield_entitlement) noexcept;
bool IsTwoHandedWeaponItemClass(std::uint8_t item_class) noexcept;
bool NeedsHandEquipConflictCheck(
    bool target_is_primary,
    bool target_is_secondary,
    bool candidate_is_weapon,
    bool candidate_is_two_handed_weapon) noexcept;
HandEquipConflict EvaluateHandEquipConflict(
    bool target_is_primary,
    bool target_is_secondary,
    bool candidate_is_weapon,
    bool candidate_is_two_handed_weapon,
    bool current_primary_blocks_secondary,
    bool current_secondary_occupied) noexcept;
const wchar_t* ClassDisplayToken(
    unsigned int class_id,
    ClassDisplayStyle style) noexcept;
const char* ClassDisplayTokenAscii(
    unsigned int class_id,
    ClassDisplayStyle style) noexcept;
OrderedClassIds BuildOrderedClassIdList(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask) noexcept;
bool CanFormatClassDisplay(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask) noexcept;
std::wstring FormatClassDisplay(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask,
    ClassDisplayStyle style);
std::string FormatClassDisplayAscii(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask,
    ClassDisplayStyle style);

}  // namespace monomyth::multiclass_identity
