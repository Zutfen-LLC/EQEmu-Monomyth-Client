#include "multiclass_combat_ability.h"

#include <algorithm>
#include <unordered_set>

namespace monomyth::multiclass_combat_ability {

bool ShouldResolveAuthoritativeCombatAbility(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    int ability_index,
    int native_result) noexcept {
    return native_result <= 0 &&
        ability_index >= 0 &&
        ability_index < kMaxCombatAbilityIndex &&
        snapshot.has_classes_bitmask &&
        snapshot.classes_bitmask != 0 &&
        monomyth::multiclass_identity::IsPlayableClassMask(snapshot.classes_bitmask);
}

monomyth::multiclass_identity::OrderedClassIds BuildAuthoritativeCombatAbilityClassOrder(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    unsigned int preferred_class_id) noexcept {
    if (!snapshot.has_classes_bitmask ||
        snapshot.classes_bitmask == 0 ||
        !monomyth::multiclass_identity::IsPlayableClassMask(snapshot.classes_bitmask)) {
        return {};
    }

    const unsigned int authoritative_preferred_class_id =
        monomyth::multiclass_identity::HasClass(snapshot.classes_bitmask, preferred_class_id)
            ? preferred_class_id
            : 0;

    return monomyth::multiclass_identity::BuildOrderedClassIdList(
        authoritative_preferred_class_id,
        snapshot.has_classes_bitmask,
        snapshot.classes_bitmask);
}

std::size_t BuildCombatAbilityArrayFromLearnedDisciplines(
    const std::array<int, kMaxLearnedDisciplineIndex>& learned_disciplines,
    std::array<int, kMaxCombatAbilityIndex>* combat_abilities,
    std::size_t* nonzero_entries) noexcept {
    if (combat_abilities == nullptr || nonzero_entries == nullptr) {
        return 0;
    }

    combat_abilities->fill(0);
    *nonzero_entries = 0;

    std::unordered_set<int> seen_spell_ids;
    seen_spell_ids.reserve(kMaxLearnedDisciplineIndex);

    std::size_t write_index = 0;
    for (const int discipline_spell_id : learned_disciplines) {
        if (discipline_spell_id <= 0 ||
            !seen_spell_ids.insert(discipline_spell_id).second) {
            continue;
        }

        if (write_index >= combat_abilities->size()) {
            break;
        }

        (*combat_abilities)[write_index++] = discipline_spell_id;
    }

    *nonzero_entries = write_index;
    return write_index;
}

}  // namespace monomyth::multiclass_combat_ability
