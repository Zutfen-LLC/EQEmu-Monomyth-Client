#pragma once

#include <array>
#include <cstddef>

#include "multiclass_identity.h"
#include "server_auth_stats_observer.h"

namespace monomyth::multiclass_combat_ability {

constexpr int kMaxCombatAbilityIndex = 300;
constexpr int kMaxLearnedDisciplineIndex = 200;

bool ShouldResolveAuthoritativeCombatAbility(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    int ability_index,
    int native_result) noexcept;

monomyth::multiclass_identity::OrderedClassIds BuildAuthoritativeCombatAbilityClassOrder(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    unsigned int preferred_class_id) noexcept;

std::size_t BuildCombatAbilityArrayFromLearnedDisciplines(
    const std::array<int, kMaxLearnedDisciplineIndex>& learned_disciplines,
    std::array<int, kMaxCombatAbilityIndex>* combat_abilities,
    std::size_t* nonzero_entries) noexcept;

}  // namespace monomyth::multiclass_combat_ability
