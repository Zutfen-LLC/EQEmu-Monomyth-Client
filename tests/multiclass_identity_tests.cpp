#include "multiclass_identity.h"

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

constexpr std::uint32_t ClassBitForTest(unsigned int class_id) {
    return 1u << (class_id - 1u);
}

constexpr std::uint32_t ClientItemClassBitForTest(unsigned int class_id) {
    return 1u << (class_id - 1u);
}

}  // namespace

int main() {
    bool passed = true;

    using namespace monomyth::multiclass_identity;

    passed &= Expect(!IsPlayableClassId(0), "class id 0 invalid");
    passed &= Expect(IsPlayableClassId(1), "class id 1 valid");
    passed &= Expect(IsPlayableClassId(16), "class id 16 valid");
    passed &= Expect(!IsPlayableClassId(17), "class id 17 invalid");

    passed &= Expect(ClassBit(1) == 0x00000001u, "class bit warrior");
    passed &= Expect(ClassBit(3) == 0x00000004u, "class bit paladin");
    passed &= Expect(ClassBit(13) == 0x00001000u, "class bit magician");
    passed &= Expect(ClassBit(16) == 0x00008000u, "class bit berserker");
    passed &= Expect(ClassBit(0) == 0, "invalid low class bit");
    passed &= Expect(ClassBit(17) == 0, "invalid high class bit");
    passed &= Expect(ClientItemClassBit(3) == 0x00000004u, "client item paladin bit");
    passed &= Expect(ClientItemClassBit(13) == 0x00001000u, "client item magician bit");
    passed &= Expect(ClientItemClassBit(16) == 0x00008000u, "client item berserker bit");
    passed &= Expect(ClientItemClassBit(0) == 0, "invalid low client item bit");
    passed &= Expect(ClientItemClassBit(17) == 0, "invalid high client item bit");

    passed &= Expect(IsPlayableClassMask(0), "empty mask is structurally valid");
    passed &= Expect(
        IsPlayableClassMask(ClassBitForTest(3) | ClassBitForTest(13)),
        "paladin magician mask valid");
    passed &= Expect(!IsPlayableClassMask(0x00010000u), "out of range bit rejected");

    passed &= Expect(
        HasClass(ClassBitForTest(3) | ClassBitForTest(13), 3),
        "mask contains paladin");
    passed &= Expect(
        HasClass(ClassBitForTest(3) | ClassBitForTest(13), 13),
        "mask contains magician");
    passed &= Expect(
        !HasClass(ClassBitForTest(3) | ClassBitForTest(13), 16),
        "mask omits berserker");
    passed &= Expect(
        !HasClass(ClassBitForTest(3) | ClassBitForTest(13), 0),
        "invalid class lookup rejected");
    passed &= Expect(
        HasClientItemClass(
            ClientItemClassBitForTest(3) | ClientItemClassBitForTest(13),
            3),
        "client item mask contains paladin");
    passed &= Expect(
        HasClientItemClass(
            ClientItemClassBitForTest(3) | ClientItemClassBitForTest(13),
            13),
        "client item mask contains magician");
    passed &= Expect(
        !HasClientItemClass(
            ClientItemClassBitForTest(3) | ClientItemClassBitForTest(13),
            16),
        "client item mask omits berserker");

    passed &= Expect(
        HasAuthoritativeClass(true, ClassBitForTest(3) | ClassBitForTest(13), 3),
        "authoritative paladin present");
    passed &= Expect(
        HasAuthoritativeClass(true, ClassBitForTest(3) | ClassBitForTest(13), 13),
        "authoritative magician present");
    passed &= Expect(
        !HasAuthoritativeClass(false, ClassBitForTest(3) | ClassBitForTest(13), 3),
        "missing authoritative mask denies class");
    passed &= Expect(
        !HasAuthoritativeClass(true, 0, 3),
        "empty authoritative mask denies class");
    passed &= Expect(
        !HasAuthoritativeClass(true, 0x00010000u, 3),
        "invalid authoritative mask denies class");
    passed &= Expect(
        !HasAuthoritativeClass(true, ClassBitForTest(3) | ClassBitForTest(13), 16),
        "unassigned class denied");
    passed &= Expect(
        HasAnyAuthoritativeClientItemClass(
            true,
            ClassBitForTest(3) | ClassBitForTest(13),
            ClientItemClassBitForTest(3)),
        "authoritative classes intersect client item class mask");
    passed &= Expect(
        HasAnyAuthoritativeClientItemClass(
            true,
            ClassBitForTest(3) | ClassBitForTest(13),
            ClientItemClassBitForTest(13)),
        "authoritative classes match magician client item bit");
    passed &= Expect(
        !HasAnyAuthoritativeClientItemClass(
            true,
            ClassBitForTest(3) | ClassBitForTest(13),
            ClientItemClassBitForTest(16)),
        "non-overlapping client item class mask denied");
    passed &= Expect(
        !HasAnyAuthoritativeClientItemClass(
            false,
            ClassBitForTest(3) | ClassBitForTest(13),
            ClientItemClassBitForTest(3)),
        "missing authoritative client item mask denied");
    passed &= Expect(
        !HasAnyAuthoritativeClientItemClass(
            true,
            0x00010000u,
            ClientItemClassBitForTest(3)),
        "invalid authoritative server mask denied for client item mask");
    passed &= Expect(
        !HasAnyAuthoritativeClientItemClass(
            true,
            ClassBitForTest(3),
            0x00020000u),
        "invalid client item mask denied");
    passed &= Expect(
        HasAnyAuthoritativeClientItemClass(
            true,
            0x00001014u,
            0x0000839fu),
        "barbed ringmail intersects authoritative paladin shadowknight mask");

    return passed ? 0 : 1;
}
