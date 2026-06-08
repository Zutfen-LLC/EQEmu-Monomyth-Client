#include "opcode_reference.h"
#include "remote_multipet_status.h"

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

void AppendCString(std::vector<std::uint8_t>* bytes, std::string_view value) {
    bytes->insert(bytes->end(), value.begin(), value.end());
    bytes->push_back(0);
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
        monomyth::opcode_reference::TryLookupRof2OpcodeValue(
            L"OP_RemoteMultiPetStatus",
            &resolved_opcode),
        "OP_RemoteMultiPetStatus resolves");
    passed &= Expect(
        resolved_opcode == monomyth::remote_multipet_status::kRemoteMultiPetStatusOpcode,
        "remote multi-pet status opcode value");
    passed &= Expect(
        monomyth::opcode_reference::LookupRof2OpcodeName(
            monomyth::remote_multipet_status::kRemoteMultiPetStatusOpcode) ==
            L"OP_RemoteMultiPetStatus",
        "remote multi-pet status reverse lookup");

    std::vector<std::uint8_t> valid_payload;
    AppendU32(&valid_payload, 2);
    AppendU32(&valid_payload, 0);
    AppendCString(&valid_payload, "SplashA");
    AppendU32(&valid_payload, 1);
    AppendCString(&valid_payload, "SplashB");

    const auto valid_result =
        monomyth::remote_multipet_status::ParsePayload(
            valid_payload.data(),
            static_cast<std::uint32_t>(valid_payload.size()));
    passed &= Expect(valid_result.valid, "valid payload parsed");
    passed &= Expect(valid_result.declared_entry_count == 2, "valid declared count");
    passed &= Expect(valid_result.parsed_entry_count == 2, "valid parsed count");
    passed &= Expect(valid_result.accepted_entry_count == 2, "valid accepted count");
    passed &= Expect(valid_result.rejected_entry_count == 0, "valid rejected count");
    passed &= Expect(valid_result.has_name[0], "slot0 has name");
    passed &= Expect(valid_result.pet_name[0] == "SplashA", "slot0 name");
    passed &= Expect(valid_result.has_name[1], "slot1 has name");
    passed &= Expect(valid_result.pet_name[1] == "SplashB", "slot1 name");

    std::vector<std::uint8_t> invalid_slot_payload;
    AppendU32(&invalid_slot_payload, 2);
    AppendU32(&invalid_slot_payload, 0);
    AppendCString(&invalid_slot_payload, "SplashA");
    AppendU32(&invalid_slot_payload, 9);
    AppendCString(&invalid_slot_payload, "Ignored");
    const auto invalid_slot_result =
        monomyth::remote_multipet_status::ParsePayload(
            invalid_slot_payload.data(),
            static_cast<std::uint32_t>(invalid_slot_payload.size()));
    passed &= Expect(invalid_slot_result.valid, "invalid slot payload parses");
    passed &= Expect(invalid_slot_result.accepted_entry_count == 1, "invalid slot accepted count");
    passed &= Expect(invalid_slot_result.rejected_entry_count == 1, "invalid slot rejected count");
    passed &= Expect(invalid_slot_result.has_name[0], "invalid slot keeps slot0");
    passed &= Expect(!invalid_slot_result.has_name[1], "invalid slot leaves slot1 empty");

    std::vector<std::uint8_t> empty_name_payload;
    AppendU32(&empty_name_payload, 2);
    AppendU32(&empty_name_payload, 0);
    AppendCString(&empty_name_payload, "");
    AppendU32(&empty_name_payload, 1);
    AppendCString(&empty_name_payload, "SplashB");
    const auto empty_name_result =
        monomyth::remote_multipet_status::ParsePayload(
            empty_name_payload.data(),
            static_cast<std::uint32_t>(empty_name_payload.size()));
    passed &= Expect(empty_name_result.valid, "empty name payload parses");
    passed &= Expect(!empty_name_result.has_name[0], "empty slot0 name treated as missing");
    passed &= Expect(empty_name_result.pet_name[0].empty(), "empty slot0 name stored empty");
    passed &= Expect(empty_name_result.has_name[1], "slot1 still has name");

    std::vector<std::uint8_t> truncated_payload;
    AppendU32(&truncated_payload, 1);
    AppendU32(&truncated_payload, 0);
    truncated_payload.push_back('A');
    const auto truncated_result =
        monomyth::remote_multipet_status::ParsePayload(
            truncated_payload.data(),
            static_cast<std::uint32_t>(truncated_payload.size()));
    passed &= Expect(!truncated_result.valid, "truncated payload rejected");

    monomyth::remote_multipet_status::Clear();
    monomyth::remote_multipet_status::ObserveReceivePayload(
        valid_payload.data(),
        static_cast<std::uint32_t>(valid_payload.size()));
    const auto snapshot = monomyth::remote_multipet_status::GetSnapshot();
    passed &= Expect(snapshot.has_name[0], "snapshot slot0 has name");
    passed &= Expect(snapshot.pet_name[0] == "SplashA", "snapshot slot0 name");
    passed &= Expect(snapshot.has_name[1], "snapshot slot1 has name");
    passed &= Expect(snapshot.pet_name[1] == "SplashB", "snapshot slot1 name");

    monomyth::remote_multipet_status::Clear();
    const auto cleared_snapshot = monomyth::remote_multipet_status::GetSnapshot();
    passed &= Expect(!cleared_snapshot.has_name[0], "cleared snapshot slot0");
    passed &= Expect(!cleared_snapshot.has_name[1], "cleared snapshot slot1");

    return passed ? 0 : 1;
}
