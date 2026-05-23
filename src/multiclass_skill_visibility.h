#pragma once

#include "server_auth_stats_observer.h"

namespace monomyth::multiclass_skill_visibility {

bool HasAuthoritativeActivatedSkill(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    int skill_id) noexcept;

}  // namespace monomyth::multiclass_skill_visibility
