#include "multiclass_combat_ability.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    monomyth::server_auth_stats::Snapshot empty = {};
    Expect(
        !monomyth::multiclass_combat_ability::ShouldResolveAuthoritativeCombatAbility(
            empty,
            0,
            0),
        "empty snapshot should not allow authoritative combat ability resolution");

    monomyth::server_auth_stats::Snapshot valid = {};
    valid.has_classes_bitmask = true;
    valid.classes_bitmask = 0x00000044u;  // PAL + MNK

    Expect(
        monomyth::multiclass_combat_ability::ShouldResolveAuthoritativeCombatAbility(
            valid,
            42,
            0),
        "valid class mask should allow fallback when native result is zero");
    Expect(
        !monomyth::multiclass_combat_ability::ShouldResolveAuthoritativeCombatAbility(
            valid,
            42,
            1234),
        "positive native result should not trigger combat ability fallback");
    Expect(
        !monomyth::multiclass_combat_ability::ShouldResolveAuthoritativeCombatAbility(
            valid,
            300,
            0),
        "out-of-range combat ability slots should not trigger fallback");

    const auto preferred_order =
        monomyth::multiclass_combat_ability::BuildAuthoritativeCombatAbilityClassOrder(
            valid,
            7);
    Expect(preferred_order.count == 2, "expected two authoritative combat ability classes");
    Expect(
        preferred_order.class_ids[0] == 7 && preferred_order.class_ids[1] == 3,
        "preferred class should lead the authoritative combat ability order");

    const auto fallback_order =
        monomyth::multiclass_combat_ability::BuildAuthoritativeCombatAbilityClassOrder(
            valid,
            12);
    Expect(fallback_order.count == 2, "fallback order should preserve authoritative classes");
    Expect(
        fallback_order.class_ids[0] == 3 && fallback_order.class_ids[1] == 7,
        "missing preferred class should fall back to mask order");

    std::array<int, monomyth::multiclass_combat_ability::kMaxLearnedDisciplineIndex>
        learned_disciplines = {};
    learned_disciplines[0] = 101;
    learned_disciplines[1] = 202;
    learned_disciplines[2] = 101;
    learned_disciplines[3] = 0;
    learned_disciplines[4] = 303;

    std::array<int, monomyth::multiclass_combat_ability::kMaxCombatAbilityIndex>
        combat_abilities = {};
    std::size_t nonzero_entries = 0;
    const std::size_t written =
        monomyth::multiclass_combat_ability::BuildCombatAbilityArrayFromLearnedDisciplines(
            learned_disciplines,
            &combat_abilities,
            &nonzero_entries);
    Expect(written == 3, "expected three unique learned disciplines to be copied");
    Expect(nonzero_entries == 3, "expected nonzero count to match copied disciplines");
    Expect(combat_abilities[0] == 101, "expected first learned discipline to stay first");
    Expect(combat_abilities[1] == 202, "expected second learned discipline to stay second");
    Expect(combat_abilities[2] == 303, "expected later unique discipline to be retained");
    Expect(combat_abilities[3] == 0, "expected trailing combat ability slots to stay zero");

    return 0;
}
