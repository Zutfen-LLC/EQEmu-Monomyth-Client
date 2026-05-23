#include "multiclass_skill_visibility.h"

#include <array>
#include <cstdint>

namespace monomyth::multiclass_skill_visibility {
namespace {

constexpr int kLowSkillLimit = 64;
constexpr int kHighSkillLimit = 128;

struct ActivatedSkillEntry {
    int skill_id;
    const wchar_t* name;
};

constexpr std::array<ActivatedSkillEntry, 26> kAdvertisedActivatedSkills = {{
    {6, L"Apply Poison"},
    {8, L"Backstab"},
    {9, L"Bind Wound"},
    {10, L"Bash/Slam"},
    {16, L"Disarm"},
    {17, L"Disarm Traps"},
    {21, L"Dragon Punch"},
    {23, L"Eagle Strike"},
    {25, L"Feign Death"},
    {26, L"Flying Kick"},
    {27, L"Forage"},
    {29, L"Hide"},
    {30, L"Kick"},
    {32, L"Mend"},
    {35, L"Pick Lock"},
    {38, L"Round Kick"},
    {40, L"Sense Heading"},
    {42, L"Sneak"},
    {48, L"Pick Pockets"},
    {52, L"Tiger Claw"},
    {53, L"Tracking"},
    {62, L"Sense Traps"},
    {67, L"Begging"},
    {71, L"Intimidation"},
    {73, L"Taunt"},
    {74, L"Frenzy"},
}};

}  // namespace

bool IsAdvertisedActivatedSkill(int skill_id) noexcept {
    for (const ActivatedSkillEntry& entry : kAdvertisedActivatedSkills) {
        if (entry.skill_id == skill_id) {
            return true;
        }
    }
    return false;
}

const wchar_t* ActivatedSkillName(int skill_id) noexcept {
    for (const ActivatedSkillEntry& entry : kAdvertisedActivatedSkills) {
        if (entry.skill_id == skill_id) {
            return entry.name;
        }
    }
    return L"unknown";
}

bool HasAuthoritativeActivatedSkill(
    const monomyth::server_auth_stats::Snapshot& snapshot,
    int skill_id) noexcept {
    if (!snapshot.has_activated_skill_mask_low ||
        !snapshot.has_activated_skill_mask_high ||
        skill_id < 0 ||
        skill_id >= kHighSkillLimit ||
        !IsAdvertisedActivatedSkill(skill_id)) {
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
