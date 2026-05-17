#include "server_auth_stats_observer.h"
#include "opcode_reference.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

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
    AppendU32(&key_scan, 2);
    AppendEntry(&key_scan, 0, 0xffff);
    AppendEntry(&key_scan, 1, 7);
    const auto key_scan_result =
        monomyth::server_auth_stats::ParsePayload(key_scan.data(), static_cast<std::uint32_t>(key_scan.size()));
    passed &= Expect(key_scan_result.valid, "key scan packet valid");
    passed &= Expect(key_scan_result.has_classes_bitmask, "key scan has class mask");
    passed &= Expect(key_scan_result.classes_bitmask == 0x00000007, "key scan class mask value");

    std::vector<std::uint8_t> without_key;
    AppendU32(&without_key, 2);
    AppendEntry(&without_key, 0, 0);
    AppendEntry(&without_key, 2, 1234);
    const auto without_key_result =
        monomyth::server_auth_stats::ParsePayload(without_key.data(), static_cast<std::uint32_t>(without_key.size()));
    passed &= Expect(without_key_result.valid, "without key packet valid");
    passed &= Expect(!without_key_result.has_classes_bitmask, "without key has no class mask");

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
