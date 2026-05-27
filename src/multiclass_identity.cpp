#include "multiclass_identity.h"

#include <string>

namespace monomyth::multiclass_identity {
namespace {

constexpr std::array<std::wstring_view, kPlayableClassCount + 1> kFullClassNames = {{
    L"",
    L"Warrior",
    L"Cleric",
    L"Paladin",
    L"Ranger",
    L"Shadow Knight",
    L"Druid",
    L"Monk",
    L"Bard",
    L"Rogue",
    L"Shaman",
    L"Necromancer",
    L"Wizard",
    L"Magician",
    L"Enchanter",
    L"Beastlord",
    L"Berserker",
}};

constexpr std::array<std::wstring_view, kPlayableClassCount + 1> kThreeLetterClassCodes = {{
    L"",
    L"WAR",
    L"CLR",
    L"PAL",
    L"RNG",
    L"SHD",
    L"DRU",
    L"MNK",
    L"BRD",
    L"ROG",
    L"SHM",
    L"NEC",
    L"WIZ",
    L"MAG",
    L"ENC",
    L"BST",
    L"BER",
}};

constexpr std::array<std::string_view, kPlayableClassCount + 1> kFullClassNamesAscii = {{
    "",
    "Warrior",
    "Cleric",
    "Paladin",
    "Ranger",
    "Shadow Knight",
    "Druid",
    "Monk",
    "Bard",
    "Rogue",
    "Shaman",
    "Necromancer",
    "Wizard",
    "Magician",
    "Enchanter",
    "Beastlord",
    "Berserker",
}};

constexpr std::array<std::string_view, kPlayableClassCount + 1> kThreeLetterClassCodesAscii = {{
    "",
    "WAR",
    "CLR",
    "PAL",
    "RNG",
    "SHD",
    "DRU",
    "MNK",
    "BRD",
    "ROG",
    "SHM",
    "NEC",
    "WIZ",
    "MAG",
    "ENC",
    "BST",
    "BER",
}};

constexpr std::uint32_t kDualWieldClassMask = 0x000041c9u;
constexpr std::uint32_t kCastingClassMask = 0x00007ebeu;

void AppendClassIfMissing(
    OrderedClassIds* ordered_ids,
    unsigned int class_id) noexcept {
    if (ordered_ids == nullptr || !IsPlayableClassId(class_id)) {
        return;
    }

    for (std::size_t i = 0; i < ordered_ids->count; ++i) {
        if (ordered_ids->class_ids[i] == class_id) {
            return;
        }
    }

    if (ordered_ids->count >= ordered_ids->class_ids.size()) {
        return;
    }

    ordered_ids->class_ids[ordered_ids->count] = class_id;
    ++ordered_ids->count;
}

}  // namespace

bool IsPlayableClassId(unsigned int class_id) noexcept {
    return class_id >= kFirstPlayableClassId && class_id <= kLastPlayableClassId;
}

bool IsCastingClassId(unsigned int class_id) noexcept {
    return HasClass(kCastingClassMask, class_id);
}

std::uint32_t ClassBit(unsigned int class_id) noexcept {
    if (!IsPlayableClassId(class_id)) {
        return 0;
    }

    return 1u << (class_id - 1u);
}

std::uint32_t ClientItemClassBit(unsigned int class_id) noexcept {
    if (!IsPlayableClassId(class_id)) {
        return 0;
    }

    return 1u << (class_id - 1u);
}

bool IsPlayableClassMask(std::uint32_t class_mask) noexcept {
    return (class_mask & ~kPlayableClassMask) == 0;
}

bool HasClass(std::uint32_t class_mask, unsigned int class_id) noexcept {
    const std::uint32_t class_bit = ClassBit(class_id);
    return class_bit != 0 && (class_mask & class_bit) != 0;
}

bool HasClientItemClass(std::uint32_t class_mask, unsigned int class_id) noexcept {
    const std::uint32_t class_bit = ClientItemClassBit(class_id);
    return class_bit != 0 && (class_mask & class_bit) != 0;
}

bool HasAuthoritativeClass(
    bool has_class_mask,
    std::uint32_t class_mask,
    unsigned int class_id) noexcept {
    return has_class_mask &&
        class_mask != 0 &&
        IsPlayableClassMask(class_mask) &&
        HasClass(class_mask, class_id);
}

bool HasAnyAuthoritativeClientItemClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint32_t client_item_class_mask) noexcept {
    if (!has_class_mask ||
        authoritative_class_mask == 0 ||
        !IsPlayableClassMask(authoritative_class_mask) ||
        (client_item_class_mask & ~kClientItemPlayableClassMask) != 0) {
        return false;
    }

    for (unsigned int class_id = kFirstPlayableClassId;
         class_id <= kLastPlayableClassId;
         ++class_id) {
        if (HasClass(authoritative_class_mask, class_id) &&
            HasClientItemClass(client_item_class_mask, class_id)) {
            return true;
        }
    }

    return false;
}

bool HasAnyAuthoritativeCastingClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask) noexcept {
    return has_class_mask &&
        authoritative_class_mask != 0 &&
        IsPlayableClassMask(authoritative_class_mask) &&
        (authoritative_class_mask & kCastingClassMask) != 0;
}

bool HasAnyAuthoritativeDualWieldClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask) noexcept {
    return has_class_mask &&
        authoritative_class_mask != 0 &&
        IsPlayableClassMask(authoritative_class_mask) &&
        (authoritative_class_mask & kDualWieldClassMask) != 0;
}

bool HasAuthoritativeOffhandWeaponClassAndDualWield(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint32_t client_item_class_mask,
    bool has_dual_wield_entitlement) noexcept {
    return has_dual_wield_entitlement &&
        HasAnyAuthoritativeClientItemClass(
            has_class_mask,
            authoritative_class_mask,
            client_item_class_mask);
}

const wchar_t* ClassDisplayToken(
    unsigned int class_id,
    ClassDisplayStyle style) noexcept {
    if (!IsPlayableClassId(class_id)) {
        return nullptr;
    }

    const auto& tokens =
        style == ClassDisplayStyle::kThreeLetterCode ? kThreeLetterClassCodes : kFullClassNames;
    return tokens[class_id].data();
}

const char* ClassDisplayTokenAscii(
    unsigned int class_id,
    ClassDisplayStyle style) noexcept {
    if (!IsPlayableClassId(class_id)) {
        return nullptr;
    }

    const auto& tokens = style == ClassDisplayStyle::kThreeLetterCode
        ? kThreeLetterClassCodesAscii
        : kFullClassNamesAscii;
    return tokens[class_id].data();
}

OrderedClassIds BuildOrderedClassIdList(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask) noexcept {
    OrderedClassIds ordered_ids = {};
    if (!has_class_mask || class_mask == 0 || !IsPlayableClassMask(class_mask)) {
        return ordered_ids;
    }

    AppendClassIfMissing(&ordered_ids, primary_class_id);

    for (unsigned int class_id = kFirstPlayableClassId;
         class_id <= kLastPlayableClassId;
         ++class_id) {
        if (HasClass(class_mask, class_id)) {
            AppendClassIfMissing(&ordered_ids, class_id);
        }
    }

    return ordered_ids;
}

bool CanFormatClassDisplay(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask) noexcept {
    return BuildOrderedClassIdList(primary_class_id, has_class_mask, class_mask).count != 0;
}

std::wstring FormatClassDisplay(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask,
    ClassDisplayStyle style) {
    const OrderedClassIds ordered_ids =
        BuildOrderedClassIdList(primary_class_id, has_class_mask, class_mask);
    if (ordered_ids.count == 0) {
        return L"";
    }

    std::wstring formatted;
    for (std::size_t i = 0; i < ordered_ids.count; ++i) {
        const wchar_t* token = ClassDisplayToken(ordered_ids.class_ids[i], style);
        if (token == nullptr || token[0] == L'\0') {
            continue;
        }

        if (!formatted.empty()) {
            formatted += L"/";
        }
        formatted += token;
    }

    return formatted;
}

std::string FormatClassDisplayAscii(
    unsigned int primary_class_id,
    bool has_class_mask,
    std::uint32_t class_mask,
    ClassDisplayStyle style) {
    const OrderedClassIds ordered_ids =
        BuildOrderedClassIdList(primary_class_id, has_class_mask, class_mask);
    if (ordered_ids.count == 0) {
        return "";
    }

    std::string formatted;
    for (std::size_t i = 0; i < ordered_ids.count; ++i) {
        const char* token = ClassDisplayTokenAscii(ordered_ids.class_ids[i], style);
        if (token == nullptr || token[0] == '\0') {
            continue;
        }

        if (!formatted.empty()) {
            formatted += "/";
        }
        formatted += token;
    }

    return formatted;
}

}  // namespace monomyth::multiclass_identity
