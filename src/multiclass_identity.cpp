#include "multiclass_identity.h"

namespace monomyth::multiclass_identity {

bool IsPlayableClassId(unsigned int class_id) noexcept {
    return class_id >= kFirstPlayableClassId && class_id <= kLastPlayableClassId;
}

std::uint32_t ClassBit(unsigned int class_id) noexcept {
    if (!IsPlayableClassId(class_id)) {
        return 0;
    }

    return 1u << (class_id - 1u);
}

std::uint32_t ClientItemClassBit(unsigned int class_id) noexcept {
    if (!IsPlayableClassId(class_id)) {
        return 0;
    }

    return 1u << (class_id - 1u);
}

bool IsPlayableClassMask(std::uint32_t class_mask) noexcept {
    return (class_mask & ~kPlayableClassMask) == 0;
}

bool HasClass(std::uint32_t class_mask, unsigned int class_id) noexcept {
    const std::uint32_t class_bit = ClassBit(class_id);
    return class_bit != 0 && (class_mask & class_bit) != 0;
}

bool HasClientItemClass(std::uint32_t class_mask, unsigned int class_id) noexcept {
    const std::uint32_t class_bit = ClientItemClassBit(class_id);
    return class_bit != 0 && (class_mask & class_bit) != 0;
}

bool HasAuthoritativeClass(
    bool has_class_mask,
    std::uint32_t class_mask,
    unsigned int class_id) noexcept {
    return has_class_mask &&
        class_mask != 0 &&
        IsPlayableClassMask(class_mask) &&
        HasClass(class_mask, class_id);
}

bool HasAnyAuthoritativeClientItemClass(
    bool has_class_mask,
    std::uint32_t authoritative_class_mask,
    std::uint32_t client_item_class_mask) noexcept {
    if (!has_class_mask ||
        authoritative_class_mask == 0 ||
        !IsPlayableClassMask(authoritative_class_mask) ||
        (client_item_class_mask & ~kClientItemPlayableClassMask) != 0) {
        return false;
    }

    for (unsigned int class_id = kFirstPlayableClassId;
         class_id <= kLastPlayableClassId;
         ++class_id) {
        if (HasClass(authoritative_class_mask, class_id) &&
            HasClientItemClass(client_item_class_mask, class_id)) {
            return true;
        }
    }

    return false;
}

}  // namespace monomyth::multiclass_identity
