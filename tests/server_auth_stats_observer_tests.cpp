#include "server_auth_stats_observer.h"
#include "opcode_reference.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace monomyth::logger {

void Log(std::wstring_view) noexcept {}
void Log(const wchar_t*) noexcept {}
void Flush() noexcept {}

}  // namespace monomyth::logger

namespace {

void AppendU32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
    bytes->push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes->push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    bytes->push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    bytes->push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

void AppendU64(std::vector<std::uint8_t>* bytes, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes->push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}

void AppendEntry(std::vector<std::uint8_t>* bytes, std::uint32_t key, std::uint64_t value) {
    AppendU32(bytes, key);
    AppendU64(bytes, value);
}

bool Expect(bool condition, std::string_view name) {
    if (condition) {
        return true;
    }

    std::cerr << "failed: " << name << "\n";
    return false;
}

}  // namespace

int main() {
    bool passed = true;

    std::uint32_t resolved_opcode = 0;
    passed &= Expect(
        monomyth::opcode_reference::TryLookupRof2OpcodeValue(L"OP_ServerAuthStats", &resolved_opcode),
        "OP_ServerAuthStats resolves");
    passed &= Expect(resolved_opcode == 0x1338, "OP_ServerAuthStats resolves to 0x1338");
    passed &= Expect(
        monomyth::opcode_reference::LookupRof2OpcodeName(0x1338) == L"OP_ServerAuthStats",
        "0x1338 reverse lookup");

    std::vector<std::uint8_t> single;
    AppendU32(&single, 1);
    AppendEntry(&single, 1, 7);
    const auto single_result =
        monomyth::server_auth_stats::ParsePayload(single.data(), static_cast<std::uint32_t>(single.size()));
    passed &= Expect(single_result.valid, "single packet valid");
    passed &= Expect(single_result.has_classes_bitmask, "single has class mask");
    passed &= Expect(single_result.classes_bitmask == 0x00000007, "single class mask value");

    std::vector<std::uint8_t> key_scan;
    AppendU32(&key_scan, 4);
    AppendEntry(&key_scan, 0, 0xffff);
    AppendEntry(&key_scan, 1, 7);
    AppendEntry(&key_scan, 2, 0x0000000000000400ull);
    AppendEntry(&key_scan, 3, 0x0000000000000400ull);
    const auto key_scan_result =
        monomyth::server_auth_stats::ParsePayload(key_scan.data(), static_cast<std::uint32_t>(key_scan.size()));
    passed &= Expect(key_scan_result.valid, "key scan packet valid");
    passed &= Expect(key_scan_result.has_classes_bitmask, "key scan has class mask");
    passed &= Expect(key_scan_result.classes_bitmask == 0x00000007, "key scan class mask value");
    passed &= Expect(key_scan_result.has_activated_skill_mask_low, "key scan has low skill mask");
    passed &= Expect(
        key_scan_result.activated_skill_mask_low == 0x0000000000000400ull,
        "key scan low skill mask value");
    passed &= Expect(key_scan_result.has_activated_skill_mask_high, "key scan has high skill mask");
    passed &= Expect(
        key_scan_result.activated_skill_mask_high == 0x0000000000000400ull,
        "key scan high skill mask value");
    passed &= Expect(key_scan_result.recognized_entry_count == 3, "key scan recognized stat count");
    passed &= Expect(key_scan_result.unknown_entry_count == 1, "key scan unknown stat count");

    std::vector<std::uint8_t> without_key;
    AppendU32(&without_key, 2);
    AppendEntry(&without_key, 99, 0);
    AppendEntry(&without_key, 100, 1234);
    const auto without_key_result =
        monomyth::server_auth_stats::ParsePayload(without_key.data(), static_cast<std::uint32_t>(without_key.size()));
    passed &= Expect(without_key_result.valid, "without key packet valid");
    passed &= Expect(!without_key_result.has_classes_bitmask, "without key has no class mask");
    passed &= Expect(without_key_result.recognized_entry_count == 1, "without key recognized stat count");
    passed &= Expect(without_key_result.unknown_entry_count == 1, "without key unknown stat count");
    passed &= Expect(
        !without_key_result.has_activated_skill_mask_low,
        "without key has no low activated skill mask");
    passed &= Expect(
        !without_key_result.has_activated_skill_mask_high,
        "without key has no high activated skill mask");
    passed &= Expect(without_key_result.has_extra_pet_hp_1000[0], "without key has extra pet slot0");
    passed &= Expect(without_key_result.extra_pet_hp_1000[0] == 1234, "without key slot0 hp value");
    passed &= Expect(!without_key_result.has_extra_pet_hp_1000[1], "without key slot1 absent");
    passed &= Expect(!without_key_result.has_focused_pet_id, "without key focused pet absent");

    std::vector<std::uint8_t> extra_pet_hp;
    AppendU32(&extra_pet_hp, 4);
    AppendEntry(&extra_pet_hp, 1, 7);
    AppendEntry(&extra_pet_hp, 100, 345);
    AppendEntry(&extra_pet_hp, 101, 678);
    AppendEntry(&extra_pet_hp, 101, 910);
    const auto extra_pet_hp_result =
        monomyth::server_auth_stats::ParsePayload(
            extra_pet_hp.data(),
            static_cast<std::uint32_t>(extra_pet_hp.size()));
    passed &= Expect(extra_pet_hp_result.valid, "extra pet hp packet valid");
    passed &= Expect(extra_pet_hp_result.has_extra_pet_hp_1000[0], "extra pet hp slot0 present");
    passed &= Expect(extra_pet_hp_result.extra_pet_hp_1000[0] == 345, "extra pet hp slot0 value");
    passed &= Expect(extra_pet_hp_result.has_extra_pet_hp_1000[1], "extra pet hp slot1 present");
    passed &= Expect(extra_pet_hp_result.extra_pet_hp_1000[1] == 910, "extra pet hp slot1 last value wins");
    passed &= Expect(
        extra_pet_hp_result.duplicate_extra_pet_hp_1000[1],
        "extra pet hp duplicate slot1 marked");

    std::vector<std::uint8_t> focused_pet_id;
    AppendU32(&focused_pet_id, 3);
    AppendEntry(&focused_pet_id, 100, 345);
    AppendEntry(&focused_pet_id, 102, 517);
    AppendEntry(&focused_pet_id, 102, 516);
    const auto focused_pet_id_result =
        monomyth::server_auth_stats::ParsePayload(
            focused_pet_id.data(),
            static_cast<std::uint32_t>(focused_pet_id.size()));
    passed &= Expect(focused_pet_id_result.valid, "focused pet id packet valid");
    passed &= Expect(focused_pet_id_result.has_focused_pet_id, "focused pet id present");
    passed &= Expect(focused_pet_id_result.focused_pet_id == 516, "focused pet id last value wins");
    passed &= Expect(
        focused_pet_id_result.duplicate_focused_pet_id,
        "focused pet id duplicate marked");

    std::vector<std::uint8_t> duplicate_masks;
    AppendU32(&duplicate_masks, 4);
    AppendEntry(&duplicate_masks, 2, 0x1);
    AppendEntry(&duplicate_masks, 2, 0x2);
    AppendEntry(&duplicate_masks, 3, 0x4);
    AppendEntry(&duplicate_masks, 3, 0x8);
    const auto duplicate_masks_result =
        monomyth::server_auth_stats::ParsePayload(
            duplicate_masks.data(),
            static_cast<std::uint32_t>(duplicate_masks.size()));
    passed &= Expect(duplicate_masks_result.valid, "duplicate mask packet valid");
    passed &= Expect(
        duplicate_masks_result.duplicate_activated_skill_mask_low,
        "duplicate low mask marked");
    passed &= Expect(
        duplicate_masks_result.activated_skill_mask_low == 0x2,
        "duplicate low mask last value wins");
    passed &= Expect(
        duplicate_masks_result.duplicate_activated_skill_mask_high,
        "duplicate high mask marked");
    passed &= Expect(
        duplicate_masks_result.activated_skill_mask_high == 0x8,
        "duplicate high mask last value wins");

    std::vector<std::uint8_t> partial_masks;
    AppendU32(&partial_masks, 2);
    AppendEntry(&partial_masks, 1, 7);
    AppendEntry(&partial_masks, 2, 0x10);
    const auto partial_masks_result =
        monomyth::server_auth_stats::ParsePayload(
            partial_masks.data(),
            static_cast<std::uint32_t>(partial_masks.size()));
    passed &= Expect(partial_masks_result.valid, "partial mask packet valid");
    passed &= Expect(partial_masks_result.has_activated_skill_mask_low, "partial has low mask");
    passed &= Expect(!partial_masks_result.has_activated_skill_mask_high, "partial missing high mask");

    std::vector<std::uint8_t> truncated;
    AppendU32(&truncated, 1);
    AppendU32(&truncated, 1);
    const auto truncated_result =
        monomyth::server_auth_stats::ParsePayload(truncated.data(), static_cast<std::uint32_t>(truncated.size()));
    passed &= Expect(!truncated_result.valid, "truncated packet rejected");

    std::vector<std::uint8_t> wide_value;
    AppendU32(&wide_value, 1);
    AppendEntry(&wide_value, 1, 0x100000000ull);
    const auto wide_value_result =
        monomyth::server_auth_stats::ParsePayload(wide_value.data(), static_cast<std::uint32_t>(wide_value.size()));
    passed &= Expect(wide_value_result.valid, "wide value packet valid");
    passed &= Expect(!wide_value_result.has_classes_bitmask, "wide value ignored");
    passed &= Expect(wide_value_result.invalid_classes_bitmask, "wide value marked invalid");

    return passed ? 0 : 1;
}
