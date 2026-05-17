#include "spell_level_selection.h"

#include <limits>

namespace monomyth::spell_level_selection {
namespace {

constexpr unsigned int kFirstPlayableClass = 1;
constexpr unsigned int kLastPlayableClass = 16;
constexpr std::uint32_t kPlayableClassMask = 0x0000ffff;

std::uint32_t ClassBit(unsigned int class_id) noexcept {
    return 1u << (class_id - 1u);
}

SelectionResult Fallback(std::uint8_t original_level, const wchar_t* reason) noexcept {
    SelectionResult result = {};
    result.level = original_level;
    result.fallback_reason = reason;
    return result;
}

}  // namespace

bool IsValidRequiredLevel(std::uint8_t level) noexcept {
    return level != 0 && level != std::numeric_limits<std::uint8_t>::max();
}

SelectionResult SelectLowestValidRequiredLevel(
    QuerySpellLevelFn query_spell_level,
    void* query_context,
    const void* this_spell,
    unsigned int,
    bool has_authoritative_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint8_t original_level) noexcept {
    if (!has_authoritative_class_mask) {
        return Fallback(original_level, L"no_authoritative_class_mask");
    }

    if (authoritative_class_mask == 0) {
        return Fallback(original_level, L"empty_class_mask");
    }

    if ((authoritative_class_mask & ~kPlayableClassMask) != 0) {
        return Fallback(original_level, L"invalid_class_mask");
    }

    if (query_spell_level == nullptr) {
        return Fallback(original_level, L"original_unavailable");
    }

    SelectionResult selected = {};
    selected.level = original_level;
    selected.fallback_reason = L"no_valid_assigned_class";

    for (unsigned int class_id = kFirstPlayableClass; class_id <= kLastPlayableClass; ++class_id) {
        if ((authoritative_class_mask & ClassBit(class_id)) == 0) {
            continue;
        }

        const std::uint8_t candidate_level =
            query_spell_level(query_context, this_spell, class_id);
        if (!IsValidRequiredLevel(candidate_level)) {
            continue;
        }

        if (!selected.used_assigned_class || candidate_level < selected.level) {
            selected.level = candidate_level;
            selected.selected_class = class_id;
            selected.used_assigned_class = true;
            selected.fallback_reason = L"";
        }
    }

    if (!selected.used_assigned_class) {
        return Fallback(original_level, L"no_valid_assigned_class");
    }

    return selected;
}

}  // namespace monomyth::spell_level_selection
