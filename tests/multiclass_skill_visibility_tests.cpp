#include "multiclass_skill_visibility.h"

#include <array>
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

std::uint64_t MaskForSkill(int skill_id) {
    return std::uint64_t{1} << (skill_id % 64);
}

}  // namespace

int main() {
    bool passed = true;

    constexpr std::array<int, 26> kAdvertisedSkillIds = {{
        6, 8, 9, 10, 16, 17, 21, 23, 25, 26, 27, 29, 30,
        32, 35, 38, 40, 42, 48, 52, 53, 62, 67, 71, 73, 74,
    }};

    const auto low_skill_snapshot = SnapshotWithMasks(MaskForSkill(30), 0);
    passed &= Expect(
        monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            low_skill_snapshot,
            30),
        "Kick bit 30 in low mask accepted");
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            low_skill_snapshot,
            29),
        "unset low skill bit rejected");

    const auto high_skill_snapshot = SnapshotWithMasks(0, MaskForSkill(74));
    passed &= Expect(
        monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            high_skill_snapshot,
            74),
        "Frenzy bit 74 in high mask accepted");
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

    for (const int advertised_skill_id : kAdvertisedSkillIds) {
        const std::uint64_t low =
            advertised_skill_id < 64 ? MaskForSkill(advertised_skill_id) : 0;
        const std::uint64_t high =
            advertised_skill_id >= 64 ? MaskForSkill(advertised_skill_id) : 0;
        const auto exact_snapshot = SnapshotWithMasks(low, high);
        passed &= Expect(
            monomyth::multiclass_skill_visibility::IsAdvertisedActivatedSkill(
                advertised_skill_id),
            "advertised skill id is known");
        passed &= Expect(
            monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
                exact_snapshot,
                advertised_skill_id),
            "advertised skill accepted only when its bit is present");
        passed &= Expect(
            !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
                SnapshotWithMasks(0, 0),
                advertised_skill_id),
            "advertised skill unset bit rejected");
    }

    const auto unadvertised_skill_snapshot = SnapshotWithMasks(1ull << 11, 0);
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::IsAdvertisedActivatedSkill(11),
        "unadvertised skill id is not known");
    passed &= Expect(
        !monomyth::multiclass_skill_visibility::HasAuthoritativeActivatedSkill(
            unadvertised_skill_snapshot,
            11),
        "unadvertised skill bit fails closed");

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
