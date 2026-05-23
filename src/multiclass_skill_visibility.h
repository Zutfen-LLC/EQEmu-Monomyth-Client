#pragma once

#include "server_auth_stats_observer.h"

namespace monomyth::multiclass_skill_visibility {

bool IsAdvertisedActivatedSkill(int skill_id) noexcept;

const wchar_t* ActivatedSkillName(int skill_id) noexcept;

bool HasAuthoritativeActivatedSkill(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    int skill_id) noexcept;

}  // namespace monomyth::multiclass_skill_visibility
