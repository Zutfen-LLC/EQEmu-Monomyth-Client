#include "multiclass_skill_visibility.h"

#include <cstdint>

namespace monomyth::multiclass_skill_visibility {
namespace {

constexpr int kLowSkillLimit = 64;
constexpr int kHighSkillLimit = 128;

}  // namespace

bool HasAuthoritativeActivatedSkill(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    int skill_id) noexcept {
    if (!snapshot.has_activated_skill_mask_low ||
        !snapshot.has_activated_skill_mask_high ||
        skill_id < 0 ||
        skill_id >= kHighSkillLimit) {
        return false;
    }

    if (skill_id < kLowSkillLimit) {
        return (snapshot.activated_skill_mask_low &
            (std::uint64_t{1} << skill_id)) != 0;
    }

    return (snapshot.activated_skill_mask_high &
        (std::uint64_t{1} << (skill_id - kLowSkillLimit))) != 0;
}

}  // namespace monomyth::multiclass_skill_visibility
