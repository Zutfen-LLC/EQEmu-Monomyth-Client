#include "spell_level_selection.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

constexpr std::uint32_t ClassMask(unsigned int class_id) {
    return 1u << (class_id - 1u);
}

struct SpellLevels {
    std::array<std::uint8_t, 17> levels = {};
};

std::uint8_t QueryLevel(void* context, const void*, unsigned int class_id) noexcept {
    const auto* spell_levels = static_cast<const SpellLevels*>(context);
    if (spell_levels == nullptr || class_id >= spell_levels->levels.size()) {
        return 255;
    }

    return spell_levels->levels[class_id];
}

bool Expect(bool condition, std::string_view name) {
    if (condition) {
        return true;
    }

    std::cerr << "failed: " << name << "\n";
    return false;
}

}  // namespace

int main() {
    bool passed = true;

    SpellLevels spell = {};
    spell.levels[1] = 20;   // Warrior
    spell.levels[2] = 5;    // Cleric, intentionally unassigned in selection tests
    spell.levels[3] = 9;    // Paladin
    spell.levels[4] = 25;   // Ranger
    spell.levels[13] = 16;  // Magician

    const auto no_auth = monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
        QueryLevel,
        &spell,
        nullptr,
        13,
        false,
        ClassMask(3) | ClassMask(13),
        42);
    passed &= Expect(!no_auth.used_assigned_class, "no auth falls back");
    passed &= Expect(no_auth.level == 42, "no auth returns original level");

    const auto empty_mask = monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
        QueryLevel,
        &spell,
        nullptr,
        13,
        true,
        0,
        43);
    passed &= Expect(!empty_mask.used_assigned_class, "empty mask falls back");
    passed &= Expect(empty_mask.level == 43, "empty mask returns original level");

    const auto invalid_mask = monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
        QueryLevel,
        &spell,
        nullptr,
        13,
        true,
        0x00010000,
        44);
    passed &= Expect(!invalid_mask.used_assigned_class, "invalid mask falls back");
    passed &= Expect(invalid_mask.level == 44, "invalid mask returns original level");

    const auto paladin_magician =
        monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
            QueryLevel,
            &spell,
            nullptr,
            13,
            true,
            ClassMask(3) | ClassMask(13),
            16);
    passed &= Expect(paladin_magician.used_assigned_class, "assigned classes selected");
    passed &= Expect(paladin_magician.selected_class == 3, "paladin class selected");
    passed &= Expect(paladin_magician.level == 9, "lowest assigned level selected");

    const auto unassigned_ignored =
        monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
            QueryLevel,
            &spell,
            nullptr,
            13,
            true,
            ClassMask(1) | ClassMask(13),
            16);
    passed &= Expect(unassigned_ignored.used_assigned_class, "unassigned lower class ignored");
    passed &= Expect(unassigned_ignored.selected_class == 13, "magician selected without cleric");
    passed &= Expect(unassigned_ignored.level == 16, "unassigned lower level ignored");

    SpellLevels sentinels = {};
    sentinels.levels[3] = 255;
    sentinels.levels[4] = 25;
    sentinels.levels[13] = 0;
    const auto sentinels_ignored =
        monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
            QueryLevel,
            &sentinels,
            nullptr,
            13,
            true,
            ClassMask(3) | ClassMask(4) | ClassMask(13),
            60);
    passed &= Expect(sentinels_ignored.used_assigned_class, "sentinel values ignored");
    passed &= Expect(sentinels_ignored.selected_class == 4, "valid class selected after sentinels");
    passed &= Expect(sentinels_ignored.level == 25, "valid level selected after sentinels");

    const auto no_valid =
        monomyth::spell_level_selection::SelectLowestValidRequiredLevel(
            QueryLevel,
            &sentinels,
            nullptr,
            13,
            true,
            ClassMask(3) | ClassMask(13),
            61);
    passed &= Expect(!no_valid.used_assigned_class, "no valid assigned class falls back");
    passed &= Expect(no_valid.level == 61, "no valid assigned class returns original level");

    return passed ? 0 : 1;
}
