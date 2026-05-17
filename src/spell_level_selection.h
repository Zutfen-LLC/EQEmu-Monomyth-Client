#pragma once

#include <cstdint>

namespace monomyth::spell_level_selection {

using QuerySpellLevelFn = std::uint8_t (*)(
    void* context,
    const void* this_spell,
    unsigned int class_id) noexcept;

struct SelectionResult {
    std::uint8_t level = 0;
    unsigned int selected_class = 0;
    bool used_assigned_class = false;
    const wchar_t* fallback_reason = L"unknown";
};

bool IsValidRequiredLevel(std::uint8_t level) noexcept;

SelectionResult SelectLowestValidRequiredLevel(
    QuerySpellLevelFn query_spell_level,
    void* query_context,
    const void* this_spell,
    unsigned int requested_class,
    bool has_authoritative_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint8_t original_level) noexcept;

}  // namespace monomyth::spell_level_selection
