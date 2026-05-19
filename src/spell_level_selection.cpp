#include "spell_level_selection.h"

#include <limits>

#include "multiclass_identity.h"

namespace monomyth::spell_level_selection {
namespace {

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

    if (!monomyth::multiclass_identity::IsPlayableClassMask(authoritative_class_mask)) {
        return Fallback(original_level, L"invalid_class_mask");
    }

    if (query_spell_level == nullptr) {
        return Fallback(original_level, L"original_unavailable");
    }

    SelectionResult selected = {};
    selected.level = original_level;
    selected.fallback_reason = L"no_valid_assigned_class";

    for (unsigned int class_id = monomyth::multiclass_identity::kFirstPlayableClassId;
         class_id <= monomyth::multiclass_identity::kLastPlayableClassId;
         ++class_id) {
        if (!monomyth::multiclass_identity::HasClass(authoritative_class_mask, class_id)) {
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
