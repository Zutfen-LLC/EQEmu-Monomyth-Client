#include "multiclass_skill_visibility.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

bool Expect(bool condition, std::string_view name) {
    if (condition) {
        return true;
    }

    std::cerr << "failed: " << name << "\n";
    return false;
}

monomyth::server_auth_stats::Snapshot SnapshotWithMasks(
    std::uint64_t low,
    std::uint64_t high) {
    monomyth::server_auth_stats::Snapshot snapshot = {};
    snapshot.has_activated_skill_mask_low = true;
    snapshot.activated_skill_mask_low = low;
    snapshot.has_activated_skill_mask_high = true;
    snapshot.activated_skill_mask_high = high;
    return snapshot;
}

}  // namespace

int main() {
    bool passed = true;

    const auto low_skill_snapshot = SnapshotWithMasks(1ull << 10, 0);
    passed &= Expect(
        monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            low_skill_snapshot,
            10),
        "low skill bit accepted");
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            low_skill_snapshot,
            11),
        "unset low skill bit rejected");

    const auto high_skill_snapshot = SnapshotWithMasks(0, 1ull << (74 - 64));
    passed &= Expect(
        monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            high_skill_snapshot,
            74),
        "high skill bit accepted");
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            high_skill_snapshot,
            73),
        "unset high skill bit rejected");

    monomyth::server_auth_stats::Snapshot missing_low = high_skill_snapshot;
    missing_low.has_activated_skill_mask_low = false;
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            missing_low,
            74),
        "missing low mask fails closed");

    monomyth::server_auth_stats::Snapshot missing_high = low_skill_snapshot;
    missing_high.has_activated_skill_mask_high = false;
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            missing_high,
            10),
        "missing high mask fails closed");

    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            low_skill_snapshot,
            -1),
        "negative skill id rejected");
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            high_skill_snapshot,
            128),
        "out of range skill id rejected");

    return passed ? 0 : 1;
}
