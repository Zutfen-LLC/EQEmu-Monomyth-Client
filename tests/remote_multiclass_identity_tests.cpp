#include "opcode_reference.h"
#include "remote_multiclass_identity.h"

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

void AppendCString(std::vector<std::uint8_t>* bytes, std::string_view value) {
    bytes->insert(bytes->end(), value.begin(), value.end());
    bytes->push_back(0);
}

void AppendBytes(
    std::vector<std::uint8_t>* bytes,
    std::initializer_list<std::uint8_t> value) {
    bytes->insert(bytes->end(), value.begin(), value.end());
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
            L"OP_RemoteMulticlassIdentity",
            &resolved_opcode),
        "OP_RemoteMulticlassIdentity resolves");
    passed &= Expect(
        resolved_opcode == monomyth::remote_multiclass_identity::kRemoteMulticlassIdentityOpcode,
        "remote identity opcode value");
    passed &= Expect(
        monomyth::opcode_reference::LookupRof2OpcodeName(
            monomyth::remote_multiclass_identity::kRemoteMulticlassIdentityOpcode) ==
            L"OP_RemoteMulticlassIdentity",
        "remote identity reverse lookup");

    std::vector<std::uint8_t> valid_payload;
    AppendU32(&valid_payload, 2);
    AppendCString(&valid_payload, "Grunge");
    AppendU32(&valid_payload, 3);
    AppendU32(&valid_payload, 0x00000054u);
    AppendCString(&valid_payload, "Squee");
    AppendU32(&valid_payload, 7);
    AppendU32(&valid_payload, 0x00000040u);

    const auto valid_result =
        monomyth::remote_multiclass_identity::ParsePayload(
            valid_payload.data(),
            static_cast<std::uint32_t>(valid_payload.size()));
    passed &= Expect(valid_result.valid, "valid payload parsed");
    passed &= Expect(valid_result.declared_entry_count == 2, "valid payload declared count");
    passed &= Expect(valid_result.parsed_entry_count == 2, "valid payload parsed count");
    passed &= Expect(valid_result.accepted_entry_count == 2, "valid payload accepted count");
    passed &= Expect(valid_result.rejected_entry_count == 0, "valid payload rejected count");
    passed &= Expect(valid_result.entries.size() == 2, "valid payload entry vector size");
    passed &= Expect(
        valid_result.entries[0].character_name == "Grunge" &&
            valid_result.entries[0].native_class_id == 3 &&
            valid_result.entries[0].classes_bitmask == 0x00000054u,
        "first entry decoded");
    passed &= Expect(
        valid_result.entries[1].character_name == "Squee" &&
            valid_result.entries[1].native_class_id == 7 &&
            valid_result.entries[1].classes_bitmask == 0x00000040u,
        "second entry decoded");

    std::vector<std::uint8_t> invalid_payload;
    AppendU32(&invalid_payload, 5);
    AppendCString(&invalid_payload, "");
    AppendU32(&invalid_payload, 7);
    AppendU32(&invalid_payload, 0x00000040u);
    AppendCString(&invalid_payload, "BadClass");
    AppendU32(&invalid_payload, 17);
    AppendU32(&invalid_payload, 0x00010000u);
    AppendCString(&invalid_payload, "ZeroMask");
    AppendU32(&invalid_payload, 3);
    AppendU32(&invalid_payload, 0x00000000u);
    AppendCString(&invalid_payload, "MissingNativeBit");
    AppendU32(&invalid_payload, 3);
    AppendU32(&invalid_payload, 0x00000040u);
    AppendCString(&invalid_payload, "GoodEntry");
    AppendU32(&invalid_payload, 9);
    AppendU32(&invalid_payload, 0x00000100u);

    const auto invalid_result =
        monomyth::remote_multiclass_identity::ParsePayload(
            invalid_payload.data(),
            static_cast<std::uint32_t>(invalid_payload.size()));
    passed &= Expect(invalid_result.valid, "invalid entries payload still parses");
    passed &= Expect(invalid_result.declared_entry_count == 5, "invalid entries declared count");
    passed &= Expect(invalid_result.parsed_entry_count == 5, "invalid entries parsed count");
    passed &= Expect(invalid_result.accepted_entry_count == 1, "invalid entries accepted count");
    passed &= Expect(invalid_result.rejected_entry_count == 4, "invalid entries rejected count");
    passed &= Expect(
        invalid_result.entries.size() == 1 &&
            invalid_result.entries[0].character_name == "GoodEntry",
        "only valid entry retained");

    std::vector<std::uint8_t> truncated_payload;
    AppendU32(&truncated_payload, 1);
    AppendCString(&truncated_payload, "Truncated");
    AppendU32(&truncated_payload, 3);
    const auto truncated_result =
        monomyth::remote_multiclass_identity::ParsePayload(
            truncated_payload.data(),
            static_cast<std::uint32_t>(truncated_payload.size()));
    passed &= Expect(!truncated_result.valid, "truncated payload rejected");

    std::vector<std::uint8_t> impossible_count_payload;
    AppendU32(&impossible_count_payload, 5);
    AppendCString(&impossible_count_payload, "");
    const auto impossible_count_result =
        monomyth::remote_multiclass_identity::ParsePayload(
            impossible_count_payload.data(),
            static_cast<std::uint32_t>(impossible_count_payload.size()));
    passed &= Expect(!impossible_count_result.valid, "impossible entry count rejected");
    passed &= Expect(
        impossible_count_result.declared_entry_count == 5,
        "impossible entry count declared count preserved");
    passed &= Expect(
        impossible_count_result.parsed_entry_count == 0,
        "impossible entry count parsed count remains zero");
    passed &= Expect(
        impossible_count_result.entries.empty(),
        "impossible entry count yields no entries");

    std::vector<std::uint8_t> ascii_header_payload;
    AppendBytes(&ascii_header_payload, {0x53, 0x55, 0x52, 0x46});
    AppendCString(&ascii_header_payload, "surface");
    const auto ascii_header_result =
        monomyth::remote_multiclass_identity::ParsePayload(
            ascii_header_payload.data(),
            static_cast<std::uint32_t>(ascii_header_payload.size()));
    passed &= Expect(!ascii_header_result.valid, "ascii header payload rejected");
    passed &= Expect(
        ascii_header_result.declared_entry_count == 0x46525553u,
        "ascii header first dword captured as declared count");
    passed &= Expect(
        ascii_header_result.parsed_entry_count == 0,
        "ascii header parsed count remains zero");
    passed &= Expect(
        ascii_header_result.entries.empty(),
        "ascii header yields no entries");

    monomyth::remote_multiclass_identity::Clear();
    passed &= Expect(
        monomyth::remote_multiclass_identity::GetStoredEntryCountForTesting() == 0,
        "store cleared before insert");

    passed &= Expect(
        monomyth::remote_multiclass_identity::StoreCharacterClassMask(
            "Monomyth EQ - Prod",
            "Grunge",
            3,
            0x00000054u),
        "store first remote identity");
    std::uint32_t classes_bitmask = 0;
    passed &= Expect(
        monomyth::remote_multiclass_identity::TryLookupCharacterClassMask(
            "monomyth eq - prod",
            "GRUNGE",
            3,
            &classes_bitmask),
        "lookup is case-insensitive");
    passed &= Expect(classes_bitmask == 0x00000054u, "lookup returns stored mask");

    passed &= Expect(
        !monomyth::remote_multiclass_identity::TryLookupCharacterClassMask(
            "monomyth eq - prod",
            "GRUNGE",
            7,
            &classes_bitmask),
        "lookup rejects native class mismatch");

    passed &= Expect(
        monomyth::remote_multiclass_identity::StoreCharacterClassMask(
            "Monomyth EQ - Prod",
            "Grunge",
            3,
            0x0000005cu),
        "duplicate store overwrites prior mask");
    passed &= Expect(
        monomyth::remote_multiclass_identity::TryLookupCharacterClassMask(
            "Monomyth EQ - Prod",
            "Grunge",
            3,
            &classes_bitmask),
        "lookup after overwrite succeeds");
    passed &= Expect(classes_bitmask == 0x0000005cu, "overwrite keeps latest mask");
    passed &= Expect(
        monomyth::remote_multiclass_identity::GetStoredEntryCountForTesting() == 1,
        "overwrite does not add duplicate entry");

    passed &= Expect(
        monomyth::remote_multiclass_identity::StoreCharacterClassMask(
            "Monomyth EQ - Dev",
            "Grunge",
            3,
            0x00000054u),
        "same name allowed on different server");
    passed &= Expect(
        monomyth::remote_multiclass_identity::GetStoredEntryCountForTesting() == 2,
        "different server adds second entry");

    monomyth::remote_multiclass_identity::Clear();
    passed &= Expect(
        monomyth::remote_multiclass_identity::GetStoredEntryCountForTesting() == 0,
        "store cleared after insert");
    passed &= Expect(
        !monomyth::remote_multiclass_identity::TryLookupCharacterClassMask(
            "Monomyth EQ - Prod",
            "Grunge",
            3,
            &classes_bitmask),
        "lookup fails after clear");

    return passed ? 0 : 1;
}
