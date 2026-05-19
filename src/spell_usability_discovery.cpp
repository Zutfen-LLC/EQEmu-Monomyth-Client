#include "spell_usability_discovery.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "logger.h"
#include "spell_usability_discovery_policy.h"

namespace monomyth::spell_usability_discovery {
namespace {

constexpr std::uint32_t kGetSpellLevelNeededRva = 0x000af700;
constexpr std::uint32_t kIsClassUsablePredicateRva = 0x000a1f50;
constexpr std::uint32_t kHandleRButtonUpRva = 0x00297250;
constexpr std::uint32_t kCanEquipRva = 0x000a44c0;
constexpr std::uint32_t kSpellbookDispatcherRva = 0x0035e790;
constexpr std::uint32_t kSpellbookDispatcherCallsStartSpellScribePathAtRva = 0x0035e7cb;
constexpr std::uint32_t kSpellbookDispatcherCallsCanStartMemmingAtRva = 0x0035e7de;
constexpr std::uint32_t kCanStartMemmingRva = 0x0035bd40;
constexpr std::uint32_t kCanStartMemmingCallsGetSpellLevelNeededAtRva = 0x0035bea5;
constexpr std::uint32_t kSpellbookMemorizeSendPathRva = 0x0035db20;
constexpr std::uint32_t
    kSpellbookMemorizeSendPathCallsMemorizeSendPacketWrapperAtRva = 0x0035dd4a;
constexpr std::uint32_t kStartSpellScribePathRva = 0x0035ddf0;
constexpr std::uint32_t kStartSpellScribePrecheckModeGetterRva = 0x003affa0;
constexpr std::uint32_t kStartSpellScribePathCallsPrecheckModeGetterAtRva = 0x0035df47;
constexpr std::uint32_t kStartSpellScribePrecheckGateRva = 0x0004c430;
constexpr std::uint32_t kStartSpellScribePathCallsPrecheckGateAtRva = 0x0035df6a;
constexpr std::uint32_t kStartSpellScribePrecheckLookupRva = 0x00049cd0;
constexpr std::uint32_t kStartSpellScribePathCallsPrecheckLookupAtRva = 0x0035df8f;
constexpr std::uint32_t kStartSpellScribePrecheckFastAcceptRva = 0x003affd0;
constexpr std::uint32_t kStartSpellScribePrecheckGateCallsFastAcceptAtRva = 0x0004c43d;
constexpr std::uint32_t kStartSpellScribePrecheckClassResolverRva = 0x003db210;
constexpr std::uint32_t kStartSpellScribePrecheckGateCallsClassResolverAtRva = 0x0004c45c;
constexpr std::uint32_t kStartSpellScribePrecheckAssignedMaskGetterRva = 0x003b4ce0;
constexpr std::uint32_t
    kStartSpellScribePrecheckGateCallsAssignedMaskGetterAtRva = 0x0004c46d;
constexpr std::uint32_t kStartSpellScribePrecheckRule446190Rva = 0x00046190;
constexpr std::uint32_t kStartSpellScribePrecheckGateCallsRule446190AtRva = 0x0004c4a6;
constexpr std::uint32_t kStartSpellScribePrecheckRule446200Rva = 0x00046200;
constexpr std::uint32_t kStartSpellScribePrecheckGateCallsRule446200AtRva = 0x0004c4b3;
constexpr std::uint32_t kStartSpellScribePrecheckRule4462c0Rva = 0x000462c0;
constexpr std::uint32_t kStartSpellScribePrecheckGateCallsRule4462c0AtRva = 0x0004c499;
constexpr std::uint32_t kStartSpellScribePrecheckRule446380Rva = 0x00046380;
constexpr std::uint32_t kStartSpellScribePrecheckGateCallsRule446380AtRva = 0x0004c4ce;
constexpr std::uint32_t kStartSpellScribePathCallsGetSpellLevelNeededAtRva = 0x0035e084;
constexpr std::uint32_t kStartSpellMemorizationPathRva = 0x00262290;
constexpr std::uint32_t kSpellbookDispatcherCallsStartSpellMemorizationPathAtRva = 0x0035e7fd;
constexpr std::uint32_t kMemorizeSendPacketWrapperRva = 0x004c51f0;
constexpr std::uint32_t kStartSpellScribeLikeCallsMemorizeSendPacketWrapperAtRva = 0x0035e6fb;
constexpr wchar_t kResolverVersion[] = L"v5_memorize_send_trace_v1";
constexpr wchar_t kPacketId[] = L"CLIENT-OP_MEMORIZE_SEND-TRACE-V1";
constexpr char kExpectedEqgameSha256[] =
    "2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e";
constexpr std::array<std::uint8_t, 37> kGetSpellLevelNeededBytes = {{
    0x8b, 0x44, 0x24, 0x04, 0x8d, 0x50, 0xff, 0x83, 0xfa, 0x22, 0x77, 0x10,
    0x83, 0xf8, 0x24, 0x72, 0x01, 0xcc, 0x8a, 0x84, 0x01, 0x46, 0x02, 0x00,
    0x00, 0xc2, 0x04, 0x00, 0x8a, 0x81, 0x47, 0x02, 0x00, 0x00, 0xc2, 0x04,
    0x00,
}};
constexpr std::array<std::uint8_t, 33> kCanStartMemmingEntryBytes = {{
    0x83, 0x3d, 0xac, 0x35, 0xe6, 0x00, 0x00, 0x53, 0x56, 0x8b, 0xf1,
    0xb3, 0x01, 0x0f, 0x8f, 0x8f, 0x01, 0x00, 0x00, 0x8b, 0x0d, 0x7c,
    0xfc, 0xd1, 0x00, 0x6a, 0x01, 0xe8, 0xd0, 0x51, 0xf0, 0xff, 0x84,
}};
constexpr std::array<std::uint8_t, 52> kSpellbookMemorizeSendPathEntryBytes = {{
    0x83, 0xec, 0x14, 0x57, 0x8b, 0xf9, 0x83, 0xbf, 0x34, 0x02, 0x00, 0x00,
    0xff, 0x75, 0x07, 0x33, 0xc0, 0x5f, 0x83, 0xc4, 0x14, 0xc3, 0xa1, 0x1c,
    0x26, 0xdd, 0x00, 0x8b, 0x48, 0x08, 0x8b, 0x51, 0x04, 0x55, 0x8d, 0x4c,
    0x02, 0x08, 0x56, 0x8d, 0x49, 0x04, 0xbd, 0x01, 0x00, 0x00, 0x00, 0xe8,
    0xbc, 0xd6, 0x07, 0x00,
}};
constexpr std::array<std::uint8_t, 64> kSpellbookDispatcherEntryBytes = {{
    0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x30, 0x26, 0xdd, 0x00, 0x80, 0xb9, 0x5c,
    0x03, 0x00, 0x00, 0x6e, 0x74, 0x09, 0xe8, 0x49, 0x15, 0x17, 0x00, 0x85,
    0xc0, 0x74, 0x59, 0x8b, 0x8e, 0x2c, 0x02, 0x00, 0x00, 0x8b, 0x44, 0x24,
    0x08, 0x8b, 0x15, 0x7c, 0xfc, 0xd1, 0x00, 0x83, 0xba, 0x24, 0x02, 0x00,
    0x00, 0x02, 0x57, 0x8d, 0x3c, 0xc8, 0x8b, 0xce, 0x75, 0x0b, 0x57, 0xe8,
    0x20, 0xf6, 0xff, 0xff,
}};
constexpr std::array<std::uint8_t, 83> kStartSpellScribePathEntryBytes = {{
    0x6a, 0xff, 0x68, 0x76, 0x66, 0x99, 0x00, 0x64, 0xa1, 0x00, 0x00, 0x00,
    0x00, 0x50, 0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00, 0x81, 0xec, 0x18,
    0x02, 0x00, 0x00, 0x53, 0x55, 0x56, 0x33, 0xdb, 0x33, 0xf6, 0x8b, 0xe9,
    0x89, 0x5c, 0x24, 0x14, 0x57, 0x89, 0x6c, 0x24, 0x20, 0x39, 0x35, 0xac,
    0x35, 0xe6, 0x00, 0x7f, 0x7d, 0x83, 0xc8, 0xff, 0x39, 0x85, 0x34, 0x02,
    0x00, 0x00, 0x75, 0x72, 0x39, 0x85, 0x40, 0x02, 0x00, 0x00, 0x75, 0x6a,
    0x6a, 0x21, 0x8d, 0x44, 0x24, 0x20, 0x50, 0xa1, 0x1c, 0x26, 0xdd,
}};
constexpr std::array<std::uint8_t, 15> kStartSpellScribePrecheckModeGetterEntryBytes = {{
    0x8b, 0x01, 0x8b, 0x50, 0x08, 0xff, 0xd2, 0x0f, 0xb6, 0x80, 0xd4, 0x01,
    0x00, 0x00, 0xc3,
}};
constexpr std::array<std::uint8_t, 61> kStartSpellScribePrecheckGateEntryBytes = {{
    0x55, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x8b, 0xf1, 0x8b, 0x0f, 0x33,
    0xed, 0xe8, 0x8e, 0x3b, 0x36, 0x00, 0x85, 0xc0, 0x74, 0x08, 0x5f, 0x5e,
    0xb0, 0x01, 0x5d, 0xc2, 0x0c, 0x00, 0x8b, 0x46, 0x04, 0x8b, 0x48, 0x04,
    0x8d, 0x4c, 0x31, 0x04, 0x53, 0x8d, 0x49, 0x04, 0xe8, 0xaf, 0xed, 0x38,
    0x00, 0x8b, 0x0f, 0x8a, 0x98, 0x74, 0x33, 0x00, 0x00, 0x6a, 0x00, 0x6a,
    0x01,
}};
constexpr std::array<std::uint8_t, 31> kStartSpellScribePrecheckLookupEntryBytes = {{
    0x53, 0x8b, 0x5c, 0x24, 0x08, 0x56, 0x57, 0x8b, 0xf9, 0x33, 0xf6, 0xeb,
    0x03, 0x8d, 0x49, 0x00, 0x8b, 0x47, 0x04, 0x8b, 0x48, 0x04, 0x8d, 0x4c,
    0x39, 0x04, 0x8d, 0x49, 0x04, 0xe8, 0x1e,
}};
constexpr std::array<std::uint8_t, 15> kStartSpellScribePrecheckFastAcceptEntryBytes = {{
    0x8b, 0x01, 0x8b, 0x50, 0x08, 0xff, 0xd2, 0x0f, 0xb6, 0x80, 0x1a, 0x01,
    0x00, 0x00, 0xc3,
}};
constexpr std::array<std::uint8_t, 34> kStartSpellScribePrecheckClassResolverEntryBytes = {{
    0x8b, 0x01, 0x85, 0xc0, 0x74, 0x15, 0x8b, 0x49, 0x04, 0x8d, 0xa4, 0x24,
    0x00, 0x00, 0x00, 0x00, 0x39, 0x08, 0x74, 0x0a, 0x8b, 0x40, 0x0c, 0x85,
    0xc0, 0x75, 0xf5, 0x33, 0xc0, 0xc3, 0x8b, 0x40, 0x04, 0xc3,
}};
constexpr std::array<std::uint8_t, 44>
    kStartSpellScribePrecheckAssignedMaskGetterEntryBytes = {{
        0x55, 0x8b, 0xec, 0x83, 0xec, 0x44, 0xa1, 0x80, 0x87, 0xb6, 0x00, 0x33,
        0xc5, 0x89, 0x45, 0xfc, 0x57, 0x8b, 0xf9, 0x8b, 0x07, 0x8b, 0x50, 0x08,
        0x89, 0x7d, 0xf0, 0xff, 0xd2, 0x80, 0xb8, 0x1a, 0x01, 0x00, 0x00, 0x00,
        0x74, 0x19, 0x80, 0x7d, 0x0c, 0x00, 0x75, 0x13,
    }};
constexpr std::array<std::uint8_t, 40> kStartSpellScribePrecheckRule446190EntryBytes = {{
    0x8b, 0x44, 0x24, 0x04, 0x53, 0x56, 0x8b, 0xf1, 0x8b, 0x4e, 0x04, 0x8b,
    0x51, 0x04, 0x8d, 0x4c, 0x32, 0x04, 0x57, 0x8b, 0x38, 0x8b, 0x01, 0x8b,
    0x50, 0x68, 0xff, 0xd2, 0x6a, 0x01, 0x8b, 0xcf, 0x8b, 0xd8, 0xe8, 0x69,
    0xc6, 0x36, 0x00, 0x3b,
}};
constexpr std::array<std::uint8_t, 44> kStartSpellScribePrecheckRule446200EntryBytes = {{
    0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x47, 0x04, 0x8b, 0x48, 0x04, 0x8d, 0x4c,
    0x39, 0x04, 0x8d, 0x49, 0x04, 0xe8, 0xfa, 0x4f, 0x39, 0x00, 0x8b, 0x80,
    0x70, 0x33, 0x00, 0x00, 0x83, 0xf8, 0x0d, 0x7d, 0x04, 0x8b, 0xf0, 0xeb,
    0x3b, 0x3d, 0x4a, 0x01, 0x00, 0x00, 0x7f, 0x21,
}};
constexpr std::array<std::uint8_t, 48> kStartSpellScribePrecheckRule4462c0EntryBytes = {{
    0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x04, 0x8b, 0x48, 0x04, 0x8b, 0x54, 0x31,
    0x04, 0x8b, 0x42, 0x4c, 0x8d, 0x4c, 0x31, 0x04, 0x57, 0xff, 0xd0, 0x85,
    0xc0, 0x0f, 0x84, 0x8c, 0x00, 0x00, 0x00, 0x8b, 0x7c, 0x24, 0x0c, 0x8b,
    0x0f, 0x6a, 0x00, 0x6a, 0x01, 0xe8, 0x72, 0xbd, 0x36, 0x00, 0x85, 0xc0,
}};
constexpr std::array<std::uint8_t, 44> kStartSpellScribePrecheckRule446380EntryBytes = {{
    0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x6a, 0xff, 0x68, 0x78, 0x24, 0x97,
    0x00, 0x50, 0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x8b,
    0xf1, 0x8b, 0x46, 0x04, 0x8b, 0x48, 0x04, 0x8b, 0x54, 0x31, 0x04, 0x8b,
    0x02, 0x8d, 0x4c, 0x31, 0x04, 0x57, 0x6a, 0x13,
}};
constexpr std::array<std::uint8_t, 62> kStartSpellMemorizationPathEntryBytes = {{
    0x8b, 0x44, 0x24, 0x18, 0x83, 0xec, 0x14, 0x56, 0x8b, 0xf1, 0x8b, 0x4c,
    0x24, 0x2c, 0x50, 0x51, 0x6a, 0x00, 0x8d, 0x4c, 0x24, 0x12, 0xe8, 0xf5,
    0xa6, 0x1a, 0x00, 0x8b, 0x54, 0x24, 0x34, 0x8b, 0x4c, 0x24, 0x2c, 0x50,
    0x8b, 0x44, 0x24, 0x34, 0x52, 0x8b, 0x54, 0x24, 0x30, 0x50, 0x51, 0x52,
    0x8b, 0xce, 0xe8, 0x59, 0xf5, 0xff, 0xff, 0x5e, 0x83, 0xc4, 0x14, 0xc2,
    0x18, 0x00,
}};
constexpr std::array<std::uint8_t, 52> kMemorizeSendPacketWrapperEntryBytes = {{
    0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xd8, 0x91, 0x9a, 0x00, 0x64, 0xa1,
    0x00, 0x00, 0x00, 0x00, 0x50, 0x51, 0x53, 0x56, 0x57, 0xa1, 0x80, 0x87,
    0xb6, 0x00, 0x33, 0xc5, 0x50, 0x8d, 0x45, 0xf4, 0x64, 0xa3, 0x00, 0x00,
    0x00, 0x00, 0x8b, 0xf1, 0x8d, 0xbe, 0x54, 0x02, 0x00, 0x00, 0x8b, 0xcf,
    0x89, 0x7d, 0xf0, 0xe8,
}};
constexpr std::array<std::uint8_t, 68> kHandleRButtonUpEntryBytesPrefix = {{
    0x6a, 0xff, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x68, 0x81, 0xa8, 0x98,
    0x00, 0x50, 0xb8, 0x08, 0x21, 0x00, 0x00, 0x64, 0x89, 0x25, 0x00, 0x00,
    0x00, 0x00, 0xe8, 0x21, 0x4d, 0x24, 0x00, 0x53, 0x55, 0x33, 0xdb, 0x56,
    0x8b, 0xf1, 0x33, 0xed, 0x89, 0x5c, 0x24, 0x24, 0x38, 0x5e, 0x10, 0x0f,
    0x84, 0xa8, 0x08, 0x00, 0x00, 0x57, 0x83, 0xcf, 0xff, 0x8b, 0xc7, 0x89,
    0x44, 0x24, 0x1c, 0x66, 0x89, 0x44, 0x24, 0x20,
}};
constexpr std::array<std::uint8_t, 40> kIsClassUsablePredicateEntryBytes = {{
    0x8b, 0xc1, 0x8b, 0x4c, 0x24, 0x04, 0x8d, 0x51, 0xff, 0x83,
    0xfa, 0x0f, 0x77, 0x15, 0x8b, 0x40, 0x68, 0xba, 0x01, 0x00,
    0x00, 0x00, 0xd3, 0xe2, 0x23, 0xc2, 0xf7, 0xd8, 0x1b, 0xc0,
    0xf7, 0xd8, 0xc2, 0x04, 0x00, 0x32, 0xc0, 0xc2, 0x04, 0x00,
}};
constexpr std::array<std::uint8_t, 64> kCanEquipEntryBytes = {{
    0x51, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x14, 0x89, 0x4c, 0x24, 0x08, 0x85,
    0xff, 0x0f, 0x84, 0x5f, 0x01, 0x00, 0x00, 0x8b, 0x74, 0x24, 0x10, 0x85,
    0xf6, 0x0f, 0x84, 0x53, 0x01, 0x00, 0x00, 0x83, 0xbe, 0xd0, 0x2d, 0x00,
    0x00, 0x00, 0x0f, 0x84, 0x46, 0x01, 0x00, 0x00, 0x53, 0x8b, 0x5c, 0x24,
    0x20, 0x55, 0x84, 0xdb, 0x74, 0x14, 0x8b, 0x46, 0x08, 0x8b, 0x48, 0x04,
    0x8d, 0x4c, 0x31, 0x08,
}};

Result g_result = {};

struct Sha256Context {
    std::uint32_t state[8];
    std::uint64_t total_bytes = 0;
    std::array<std::uint8_t, 64> buffer = {};
    std::size_t buffer_size = 0;
};

struct CleanroomFingerprintStatus {
    bool available = false;
    bool matched = false;
    std::wstring failure_reason = L"fingerprint_unavailable";
    std::wstring evidence = L"sha256_status=unavailable";
};

struct ImageView {
    const std::uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS* nt = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD section_count = 0;
    std::uint32_t image_size = 0;
    const IMAGE_EXPORT_DIRECTORY* export_directory = nullptr;
    std::uint32_t export_directory_size = 0;
};

struct CandidateSource {
    EvidenceSource evidence_source = EvidenceSource::kUnavailable;
    std::uintptr_t address = 0;
    std::uint32_t rva = 0;
    std::wstring resolved_symbol;
    std::wstring discovery_method = L"image_lookup";
};

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
}};

std::wstring Hex32(std::uint32_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring HexPtr(std::uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring HexBytes(
    const std::uint8_t* bytes,
    std::size_t length) {
    if (bytes == nullptr || length == 0) {
        return L"";
    }

    static constexpr wchar_t kHexDigits[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve((length * 3) - 1);
    for (std::size_t i = 0; i < length; ++i) {
        if (i != 0) {
            result.push_back(L' ');
        }
        result.push_back(kHexDigits[(bytes[i] >> 4) & 0x0f]);
        result.push_back(kHexDigits[bytes[i] & 0x0f]);
    }
    return result;
}

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t amount) noexcept {
    return (value >> amount) | (value << (32U - amount));
}

void Sha256Init(Sha256Context* context) noexcept {
    if (context == nullptr) {
        return;
    }

    context->state[0] = 0x6a09e667;
    context->state[1] = 0xbb67ae85;
    context->state[2] = 0x3c6ef372;
    context->state[3] = 0xa54ff53a;
    context->state[4] = 0x510e527f;
    context->state[5] = 0x9b05688c;
    context->state[6] = 0x1f83d9ab;
    context->state[7] = 0x5be0cd19;
    context->total_bytes = 0;
    context->buffer.fill(0);
    context->buffer_size = 0;
}

void Sha256ProcessBlock(
    Sha256Context* context,
    const std::uint8_t* block) noexcept {
    if (context == nullptr || block == nullptr) {
        return;
    }

    std::uint32_t message_schedule[64] = {};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t offset = i * 4;
        message_schedule[i] =
            (static_cast<std::uint32_t>(block[offset]) << 24U) |
            (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
            (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
            static_cast<std::uint32_t>(block[offset + 3]);
    }

    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t sigma0 =
            RotateRight(message_schedule[i - 15], 7U) ^
            RotateRight(message_schedule[i - 15], 18U) ^
            (message_schedule[i - 15] >> 3U);
        const std::uint32_t sigma1 =
            RotateRight(message_schedule[i - 2], 17U) ^
            RotateRight(message_schedule[i - 2], 19U) ^
            (message_schedule[i - 2] >> 10U);
        message_schedule[i] =
            message_schedule[i - 16] + sigma0 + message_schedule[i - 7] + sigma1;
    }

    std::uint32_t a = context->state[0];
    std::uint32_t b = context->state[1];
    std::uint32_t c = context->state[2];
    std::uint32_t d = context->state[3];
    std::uint32_t e = context->state[4];
    std::uint32_t f = context->state[5];
    std::uint32_t g = context->state[6];
    std::uint32_t h = context->state[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t sum1 =
            RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 =
            h + sum1 + choice + kSha256RoundConstants[i] + message_schedule[i];
        const std::uint32_t sum0 =
            RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void Sha256Update(
    Sha256Context* context,
    const std::uint8_t* data,
    std::size_t length) noexcept {
    if (context == nullptr || data == nullptr || length == 0) {
        return;
    }

    context->total_bytes += length;
    std::size_t consumed = 0;
    while (consumed < length) {
        const std::size_t remaining = length - consumed;
        const std::size_t copy_bytes =
            std::min<std::size_t>(64 - context->buffer_size, remaining);
        std::memcpy(
            context->buffer.data() + context->buffer_size,
            data + consumed,
            copy_bytes);
        context->buffer_size += copy_bytes;
        consumed += copy_bytes;

        if (context->buffer_size == 64) {
            Sha256ProcessBlock(context, context->buffer.data());
            context->buffer_size = 0;
        }
    }
}

void Sha256Final(
    Sha256Context* context,
    std::array<std::uint8_t, 32>* digest) noexcept {
    if (context == nullptr || digest == nullptr) {
        return;
    }

    const std::uint64_t total_bits = context->total_bytes * 8ULL;
    context->buffer[context->buffer_size++] = 0x80;

    if (context->buffer_size > 56) {
        while (context->buffer_size < 64) {
            context->buffer[context->buffer_size++] = 0x00;
        }
        Sha256ProcessBlock(context, context->buffer.data());
        context->buffer_size = 0;
    }

    while (context->buffer_size < 56) {
        context->buffer[context->buffer_size++] = 0x00;
    }

    for (int i = 7; i >= 0; --i) {
        context->buffer[context->buffer_size++] =
            static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xffU);
    }
    Sha256ProcessBlock(context, context->buffer.data());

    for (std::size_t i = 0; i < 8; ++i) {
        (*digest)[i * 4] = static_cast<std::uint8_t>((context->state[i] >> 24U) & 0xffU);
        (*digest)[i * 4 + 1] = static_cast<std::uint8_t>((context->state[i] >> 16U) & 0xffU);
        (*digest)[i * 4 + 2] = static_cast<std::uint8_t>((context->state[i] >> 8U) & 0xffU);
        (*digest)[i * 4 + 3] = static_cast<std::uint8_t>(context->state[i] & 0xffU);
    }
}

std::wstring NarrowToWide(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring Basename(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }

    return path.substr(slash + 1);
}

std::wstring GetHostProcessPath() noexcept {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    return std::wstring(path, path + length);
}

bool ComputeFileSha256(
    const std::wstring& path,
    std::string* digest_hex) noexcept {
    if (path.empty() || digest_hex == nullptr) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    Sha256Context context = {};
    Sha256Init(&context);
    std::array<std::uint8_t, 64 * 1024> buffer = {};
    bool ok = true;
    for (;;) {
        DWORD bytes_read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)) {
            ok = false;
            break;
        }
        if (bytes_read == 0) {
            break;
        }
        Sha256Update(&context, buffer.data(), bytes_read);
    }
    CloseHandle(file);
    if (!ok) {
        return false;
    }

    std::array<std::uint8_t, 32> digest = {};
    Sha256Final(&context, &digest);
    static constexpr wchar_t kHexDigits[] = L"0123456789abcdef";
    std::wstring hex;
    hex.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        hex.push_back(kHexDigits[(byte >> 4) & 0x0f]);
        hex.push_back(kHexDigits[byte & 0x0f]);
    }
    *digest_hex = std::string(hex.begin(), hex.end());
    return true;
}

CleanroomFingerprintStatus EvaluateCleanroomFingerprint() noexcept {
    CleanroomFingerprintStatus status = {};
    const std::wstring process_path = GetHostProcessPath();
    if (process_path.empty()) {
        status.failure_reason = L"fingerprint_path_unavailable";
        status.evidence = L"sha256_status=path_unavailable";
        return status;
    }

    std::string digest_hex;
    if (!ComputeFileSha256(process_path, &digest_hex)) {
        status.failure_reason = L"fingerprint_sha256_unavailable";
        status.evidence = L"sha256_status=read_failed path=\"";
        status.evidence += Basename(process_path);
        status.evidence += L"\"";
        return status;
    }

    status.available = true;
    status.matched = digest_hex == kExpectedEqgameSha256;
    status.failure_reason = status.matched ? L"none" : L"fingerprint_mismatch";
    status.evidence = L"sha256_expected=";
    status.evidence += NarrowToWide(kExpectedEqgameSha256);
    status.evidence += L" sha256_actual=";
    status.evidence += NarrowToWide(digest_hex);
    status.evidence += L" path=\"";
    status.evidence += Basename(process_path);
    status.evidence += L"\"";
    return status;
}

bool BuildImageView(ImageView* view) noexcept {
    if (view == nullptr) {
        return false;
    }

    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return false;
    }

    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
        nt->FileHeader.NumberOfSections == 0) {
        return false;
    }

    const IMAGE_DATA_DIRECTORY& export_data =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const bool export_dir_available =
        export_data.VirtualAddress != 0 &&
        export_data.Size >= sizeof(IMAGE_EXPORT_DIRECTORY) &&
        export_data.VirtualAddress < nt->OptionalHeader.SizeOfImage &&
        static_cast<std::uint64_t>(export_data.VirtualAddress) + export_data.Size <=
            nt->OptionalHeader.SizeOfImage;

    view->base = base;
    view->nt = nt;
    view->sections = IMAGE_FIRST_SECTION(nt);
    view->section_count = nt->FileHeader.NumberOfSections;
    view->image_size = nt->OptionalHeader.SizeOfImage;
    view->export_directory = export_dir_available
        ? reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + export_data.VirtualAddress)
        : nullptr;
    view->export_directory_size = export_dir_available ? export_data.Size : 0;
    return true;
}

std::uint32_t SectionSpan(const IMAGE_SECTION_HEADER& section) noexcept {
    return std::max(section.Misc.VirtualSize, section.SizeOfRawData);
}

bool SectionContains(
    const IMAGE_SECTION_HEADER& section,
    std::uint32_t rva,
    std::size_t length) noexcept {
    const std::uint32_t span = SectionSpan(section);
    if (span == 0 || rva < section.VirtualAddress) {
        return false;
    }

    const std::uint32_t offset = rva - section.VirtualAddress;
    return offset <= span && length <= (span - offset);
}

const IMAGE_SECTION_HEADER* FindSection(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length,
    DWORD required_characteristics) noexcept {
    for (WORD i = 0; i < image.section_count; ++i) {
        const IMAGE_SECTION_HEADER& section = image.sections[i];
        if ((section.Characteristics & required_characteristics) != required_characteristics) {
            continue;
        }

        if (SectionContains(section, rva, length)) {
            return &section;
        }
    }

    return nullptr;
}

const std::uint8_t* SearchBytes(
    const std::uint8_t* haystack,
    std::size_t haystack_size,
    const std::uint8_t* needle,
    std::size_t needle_size) noexcept {
    if (haystack == nullptr || needle == nullptr || needle_size == 0 || haystack_size < needle_size) {
        return nullptr;
    }

    for (std::size_t i = 0; i <= haystack_size - needle_size; ++i) {
        if (std::memcmp(haystack + i, needle, needle_size) == 0) {
            return haystack + i;
        }
    }

    return nullptr;
}

bool IsRvaWithinImage(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length) noexcept {
    if (image.image_size == 0 || rva >= image.image_size) {
        return false;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(rva) + length;
    return end <= image.image_size;
}

bool IsExecutableAddress(
    const ImageView& image,
    std::uintptr_t address) noexcept {
    if (address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return false;
    }

    const std::uint32_t rva =
        static_cast<std::uint32_t>(address - reinterpret_cast<std::uintptr_t>(image.base));
    return FindSection(image, rva, 1, IMAGE_SCN_MEM_EXECUTE) != nullptr;
}

bool HasPlausibleX86Prologue(
    const std::uint8_t* code,
    std::size_t available) noexcept {
    if (code == nullptr || available < 3) {
        return false;
    }

    const std::array<std::array<std::uint8_t, 3>, 4> patterns = {{
        {0x55, 0x8b, 0xec},
        {0x56, 0x8b, 0xf1},
        {0x57, 0x8b, 0xf9},
        {0x8b, 0x44, 0x24},
    }};

    for (const auto& pattern : patterns) {
        if (std::memcmp(code, pattern.data(), pattern.size()) == 0) {
            return true;
        }
    }

    return code[0] == 0x8b || code[0] == 0x55 || code[0] == 0x56 || code[0] == 0x57;
}

bool ResolveRvaAddress(
    const ImageView& image,
    std::uint32_t rva,
    std::size_t length,
    std::uintptr_t* address) noexcept {
    if (address == nullptr || !IsRvaWithinImage(image, rva, length)) {
        return false;
    }

    *address = reinterpret_cast<std::uintptr_t>(image.base) + rva;
    return true;
}

bool BytesMatchAtRva(
    const ImageView& image,
    std::uint32_t rva,
    const std::uint8_t* expected,
    std::size_t length) noexcept {
    if (expected == nullptr || length == 0 || !IsRvaWithinImage(image, rva, length)) {
        return false;
    }

    return std::memcmp(image.base + rva, expected, length) == 0;
}

bool CallAtRvaTargets(
    const ImageView& image,
    std::uint32_t call_rva,
    std::uintptr_t expected_target) noexcept {
    if (!IsRvaWithinImage(image, call_rva, 5)) {
        return false;
    }

    const auto* code = image.base + call_rva;
    if (code[0] != 0xe8) {
        return false;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, code + 1, sizeof(relative));
    const std::uintptr_t call_site =
        reinterpret_cast<std::uintptr_t>(image.base) + call_rva;
    const std::uintptr_t resolved_target = call_site + 5 + relative;
    return resolved_target == expected_target;
}

bool ResolveCallTargetAtRva(
    const ImageView& image,
    std::uint32_t call_rva,
    std::uintptr_t* resolved_target,
    std::uint8_t* opcode_out) noexcept {
    if (resolved_target == nullptr || opcode_out == nullptr ||
        !IsRvaWithinImage(image, call_rva, 5)) {
        return false;
    }

    const auto* code = image.base + call_rva;
    *opcode_out = code[0];
    if (code[0] != 0xe8) {
        *resolved_target = 0;
        return false;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, code + 1, sizeof(relative));
    const std::uintptr_t call_site =
        reinterpret_cast<std::uintptr_t>(image.base) + call_rva;
    *resolved_target = call_site + 5 + relative;
    return true;
}

bool ResolveJumpTargetAtRva(
    const ImageView& image,
    std::uint32_t jump_rva,
    std::uintptr_t* resolved_target,
    std::uint8_t* opcode_out) noexcept {
    if (resolved_target == nullptr || opcode_out == nullptr ||
        !IsRvaWithinImage(image, jump_rva, 5)) {
        return false;
    }

    const auto* code = image.base + jump_rva;
    *opcode_out = code[0];
    if (code[0] != 0xe9) {
        *resolved_target = 0;
        return false;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, code + 1, sizeof(relative));
    const std::uintptr_t jump_site =
        reinterpret_cast<std::uintptr_t>(image.base) + jump_rva;
    *resolved_target = jump_site + 5 + relative;
    return true;
}

bool IsHotpatchJumpThunkShapeAtRva(
    const ImageView& image,
    std::uint32_t thunk_rva) noexcept {
    if (!IsRvaWithinImage(image, thunk_rva, 11)) {
        return false;
    }

    const auto* code = image.base + thunk_rva;
    return code[0] == 0x8b &&
        code[1] == 0xff &&
        code[2] == 0x55 &&
        code[3] == 0x8b &&
        code[4] == 0xec &&
        code[5] == 0x5d &&
        code[6] == 0xe9;
}

bool ResolveHotpatchThunkTerminalTarget(
    const ImageView& image,
    std::uint32_t thunk_rva,
    std::uintptr_t* first_target,
    std::uintptr_t* terminal_target,
    std::uint32_t* hop_count) noexcept {
    if (first_target == nullptr || terminal_target == nullptr || hop_count == nullptr) {
        return false;
    }

    *first_target = 0;
    *terminal_target = 0;
    *hop_count = 0;

    if (!IsHotpatchJumpThunkShapeAtRva(image, thunk_rva)) {
        return false;
    }

    const std::uintptr_t module_base = reinterpret_cast<std::uintptr_t>(image.base);
    std::uint32_t current_thunk_rva = thunk_rva;
    for (std::uint32_t hop = 0; hop < 2; ++hop) {
        std::uintptr_t jump_target = 0;
        std::uint8_t jump_opcode = 0;
        if (!ResolveJumpTargetAtRva(
                image,
                current_thunk_rva + 6,
                &jump_target,
                &jump_opcode)) {
            return hop != 0;
        }

        if (hop == 0) {
            *first_target = jump_target;
        }
        *terminal_target = jump_target;
        *hop_count = hop + 1;

        if (jump_target < module_base) {
            return true;
        }

        const std::uint32_t jump_target_rva =
            static_cast<std::uint32_t>(jump_target - module_base);
        if (!IsHotpatchJumpThunkShapeAtRva(image, jump_target_rva)) {
            return true;
        }

        current_thunk_rva = jump_target_rva;
    }

    return true;
}

void PopulateCandidateFields(
    const ImageView& image,
    const CandidateSource& candidate,
    TargetResult* target) {
    if (target == nullptr) {
        return;
    }

    target->module_base = reinterpret_cast<std::uintptr_t>(image.base);
    target->candidate_rva = candidate.rva;
    target->candidate_address = candidate.address;
    target->resolved_symbol = candidate.resolved_symbol;
    target->discovery_method = candidate.discovery_method;
    target->evidence_source = EvidenceSourceName(candidate.evidence_source);
}

CandidateSource BuildFingerprintCandidate(
    const ImageView& image,
    std::uintptr_t address,
    const wchar_t* discovery_method,
    const wchar_t* resolved_symbol) {
    CandidateSource candidate = {};
    if (address == 0 || address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return candidate;
    }

    candidate.evidence_source = EvidenceSource::kFingerprintRva;
    candidate.address = address;
    candidate.rva = static_cast<std::uint32_t>(
        candidate.address - reinterpret_cast<std::uintptr_t>(image.base));
    candidate.resolved_symbol = resolved_symbol == nullptr ? L"" : resolved_symbol;
    candidate.discovery_method =
        discovery_method == nullptr ? L"fingerprint_locator" : discovery_method;
    return candidate;
}

CandidateSource BuildRuntimeExportCandidate(
    const ImageView& image,
    std::uintptr_t address,
    const wchar_t* resolved_symbol) {
    CandidateSource candidate = {};
    if (address == 0 || address < reinterpret_cast<std::uintptr_t>(image.base)) {
        return candidate;
    }

    candidate.evidence_source = EvidenceSource::kRuntimeExport;
    candidate.address = address;
    candidate.rva = static_cast<std::uint32_t>(
        candidate.address - reinterpret_cast<std::uintptr_t>(image.base));
    candidate.resolved_symbol = resolved_symbol == nullptr ? L"" : resolved_symbol;
    candidate.discovery_method = L"runtime_export";
    return candidate;
}

TargetResult DiscoverRuntimeExportTarget(
    const ImageView& image,
    const wchar_t* target_name,
    const char* export_name,
    const wchar_t* resolved_symbol) {
    TargetResult target = {target_name};
    target.module_base = reinterpret_cast<std::uintptr_t>(image.base);
    target.discovery_method = L"runtime_export";
    target.evidence_source = EvidenceSourceName(EvidenceSource::kRuntimeExport);

    const HMODULE module = GetModuleHandleW(nullptr);
    FARPROC proc = nullptr;
    if (module != nullptr && export_name != nullptr) {
        proc = GetProcAddress(module, export_name);
    }

    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(proc);
    const CandidateSource candidate =
        BuildRuntimeExportCandidate(image, address, resolved_symbol);
    PopulateCandidateFields(image, candidate, &target);

    if (proc == nullptr || address == 0) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"runtime_export_missing";
        target.reason = L"runtime export resolution failed for the target symbol";
        target.validation_evidence = L"export_name=\"";
        for (const char* p = export_name; p != nullptr && *p != '\0'; ++p) {
            target.validation_evidence.push_back(static_cast<wchar_t>(*p));
        }
        target.validation_evidence += L"\" export_found=no";
        return target;
    }

    if (!IsExecutableAddress(image, address)) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"runtime_export_non_executable";
        target.reason = L"runtime export resolved outside executable code";
        target.validation_evidence = L"export_found=yes executable=no";
        return target;
    }

    const auto* code = reinterpret_cast<const std::uint8_t*>(address);
    const bool plausible_prologue = HasPlausibleX86Prologue(code, 8);
    target.exact_signature_validated = false;
    target.trace_safe = plausible_prologue;
    target.validation_evidence = L"export_found=yes executable=yes plausible_prologue=";
    target.validation_evidence += plausible_prologue ? L"yes" : L"no";

    if (!plausible_prologue) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"runtime_export_unsupported_prologue";
        target.reason = L"runtime export resolved, but the entry bytes are not a supported x86 prologue";
        target.validation_evidence += L" entry_bytes=\"";
        target.validation_evidence += HexBytes(code, 8);
        target.validation_evidence += L"\"";
        return target;
    }

    target.state = TargetState::kValidated;
    target.validation = L"passed";
    target.failure_reason = L"none";
    target.reason = L"validated by runtime export resolution and plausible x86 prologue";
    return target;
}

void ApplyDecision(
    const ImageView& image,
    const CandidateSource& candidate,
    const DecisionResult& decision,
    const std::wstring& validation_evidence,
    const wchar_t* success_reason,
    const wchar_t* failure_reason,
    TargetResult* target) {
    if (target == nullptr) {
        return;
    }

    PopulateCandidateFields(image, candidate, target);
    target->state = decision.state;
    target->exact_signature_validated = decision.exact_signature_validated;
    target->trace_safe = decision.trace_safe;
    target->validation = decision.validation;
    target->failure_reason = decision.failure_reason;
    target->evidence_source = EvidenceSourceName(decision.evidence_source);
    target->validation_evidence = validation_evidence;
    target->reason =
        decision.state == TargetState::kValidated
        ? (success_reason == nullptr ? L"validated" : success_reason)
        : (failure_reason == nullptr ? L"validation failed" : failure_reason);
}

TargetResult BuildCapabilityDeniedTarget(
    const wchar_t* target_name,
    const wchar_t* failure_reason) {
    TargetResult target = {target_name};
    target.state = TargetState::kFailed;
    target.validation = L"failed";
    target.failure_reason = failure_reason == nullptr ? L"capability_denied" : failure_reason;
    target.evidence_source = EvidenceSourceName(EvidenceSource::kNotAttempted);
    target.validation_evidence = L"capability_gate=denied";
    target.reason = L"discovery skipped by capability";
    return target;
}

TargetResult BuildImageUnavailableTarget(const wchar_t* target_name) {
    TargetResult target = {target_name};
    target.state = TargetState::kFailed;
    target.discovery_method = L"image_lookup";
    target.evidence_source = EvidenceSourceName(EvidenceSource::kUnavailable);
    target.validation = L"failed";
    target.failure_reason = L"image_view_unavailable";
    target.reason = L"host PE image unavailable";
    target.validation_evidence = L"image_view=no";
    return target;
}

TargetResult DiscoverPinnedCleanroomTarget(
    const ImageView& image,
    const wchar_t* target_name,
    const wchar_t* resolved_symbol,
    std::uint32_t rva,
    const std::uint8_t* expected_bytes,
    std::size_t expected_length,
    const wchar_t* byte_label,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {target_name};
    target.module_base = reinterpret_cast<std::uintptr_t>(image.base);
    target.discovery_method = L"cleanroom_rva";
    target.evidence_source = EvidenceSourceName(EvidenceSource::kCleanroomRva);

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(image, rva, expected_length, &function_start);
    target.candidate_rva = rva;
    target.candidate_address = function_start;
    target.resolved_symbol = resolved_symbol == nullptr ? L"" : resolved_symbol;

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    if (!rva_found || function_start == 0) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"rva_out_of_range";
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason = L"cleanroom RVA did not resolve inside the loaded image";
        return target;
    }

    const bool candidate_executable = IsExecutableAddress(image, function_start);
    const bool exact_bytes_match = BytesMatchAtRva(image, rva, expected_bytes, expected_length);
    target.exact_signature_validated = exact_bytes_match;
    target.trace_safe = candidate_executable && exact_bytes_match;
    target.validation_evidence = L"evidence_source=cleanroom_rva cleanroom_rva=";
    target.validation_evidence += Hex32(rva);
    target.validation_evidence += L" ";
    target.validation_evidence += byte_label == nullptr ? L"entry_bytes" : byte_label;
    target.validation_evidence += L"=";
    target.validation_evidence += exact_bytes_match ? L"matched" : L"mismatch";
    target.validation_evidence += L" executable=";
    target.validation_evidence += candidate_executable ? L"yes" : L"no";
    target.validation_evidence += L" ";
    target.validation_evidence += cleanroom_fingerprint.evidence;

    if (!candidate_executable) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"non_executable_target";
        target.reason = L"cleanroom RVA resolved to a non-executable region";
        target.trace_safe = false;
        return target;
    }

    if (!exact_bytes_match) {
        const auto* actual_bytes = reinterpret_cast<const std::uint8_t*>(function_start);
        const std::wstring expected_hex = HexBytes(expected_bytes, expected_length);
        const std::wstring actual_hex = HexBytes(actual_bytes, expected_length);
        target.validation_evidence += L" expected_bytes=\"";
        target.validation_evidence += expected_hex;
        target.validation_evidence += L"\" actual_bytes=\"";
        target.validation_evidence += actual_hex;
        target.validation_evidence += L"\"";
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = L"entry_bytes_mismatch";
        target.reason = L"entry-byte validation failed for the pinned cleanroom RVA";
        target.trace_safe = false;
        std::wstring message = L"SpellUsabilityDiscoveryMismatch target=";
        message += target_name == nullptr ? L"unknown" : target_name;
        message += L" rva=";
        message += Hex32(rva);
        message += L" ";
        message += byte_label == nullptr ? L"entry_bytes" : byte_label;
        message += L"_expected=\"";
        message += expected_hex;
        message += L"\" ";
        message += byte_label == nullptr ? L"entry_bytes" : byte_label;
        message += L"_actual=\"";
        message += actual_hex;
        message += L"\"";
        monomyth::logger::Log(message);
        return target;
    }

    target.state = TargetState::kValidated;
    target.validation = L"passed";
    target.failure_reason = L"none";
    target.reason = L"validated by cleanroom RVA, exact SHA-256 fingerprint, and entry-byte match";
    return target;
}

TargetResult DiscoverHandleRButtonUp(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverPinnedCleanroomTarget(
        image,
        L"CInvSlot::HandleRButtonUp",
        L"CInvSlot::HandleRButtonUp",
        kHandleRButtonUpRva,
        kHandleRButtonUpEntryBytesPrefix.data(),
        kHandleRButtonUpEntryBytesPrefix.size(),
        L"entry_bytes_prefix",
        cleanroom_fingerprint);
}

TargetResult DiscoverGetSpellLevelNeeded(const ImageView& image) {
    TargetResult target = {L"GetSpellLevelNeeded"};
    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kGetSpellLevelNeededRva,
        kGetSpellLevelNeededBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_cleanroom_rva",
        L"GetSpellLevelNeeded");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const auto* code = reinterpret_cast<const std::uint8_t*>(candidate.address);
    const bool plausible_prologue =
        candidate_executable && HasPlausibleX86Prologue(code, 8);
    const bool exact_bytes_match = rva_found && BytesMatchAtRva(
        image,
        kGetSpellLevelNeededRva,
        kGetSpellLevelNeededBytes.data(),
        kGetSpellLevelNeededBytes.size());
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        plausible_prologue && exact_bytes_match,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kGetSpellLevelNeededRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" prologue=";
    evidence += plausible_prologue ? L"plausible" : L"unsupported";
    evidence += L" exact_bytes=";
    evidence += exact_bytes_match ? L"yes" : L"no";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA and exact byte shape",
        L"GetSpellLevelNeeded candidate did not satisfy cleanroom RVA validation",
        &target);
    return target;
}

TargetResult DiscoverCanStartMemming(const ImageView& image) {
    TargetResult target = {L"CanStartMemming"};
    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kCanStartMemmingRva,
        kCanStartMemmingEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_cleanroom_rva",
        L"CanStartMemming");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kCanStartMemmingRva,
        kCanStartMemmingEntryBytes.data(),
        kCanStartMemmingEntryBytes.size());
    const bool calls_get_spell_level_needed =
        candidate_executable &&
        CallAtRvaTargets(
            image,
            kCanStartMemmingCallsGetSpellLevelNeededAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kGetSpellLevelNeededRva);
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && calls_get_spell_level_needed,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kCanStartMemmingRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" callsite_rva=";
    evidence += Hex32(kCanStartMemmingCallsGetSpellLevelNeededAtRva);
    evidence += L" calls_get_spell_level_needed=";
    evidence += calls_get_spell_level_needed ? L"yes" : L"no";
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and GetSpellLevelNeeded caller shape",
        L"CanStartMemming candidate did not satisfy cleanroom RVA validation",
        &target);
    return target;
}

TargetResult DiscoverSpellbookDispatcher(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"SpellbookDispatcher"};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kSpellbookDispatcherRva,
        kSpellbookDispatcherEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_rva",
        L"SpellbookDispatcher");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kSpellbookDispatcherRva,
        kSpellbookDispatcherEntryBytes.data(),
        kSpellbookDispatcherEntryBytes.size());
    const bool calls_start_spell_scribe_path =
        CallAtRvaTargets(
            image,
            kSpellbookDispatcherCallsStartSpellScribePathAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kStartSpellScribePathRva);
    const bool calls_can_start_memming =
        CallAtRvaTargets(
            image,
            kSpellbookDispatcherCallsCanStartMemmingAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kCanStartMemmingRva);

    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && calls_start_spell_scribe_path && calls_can_start_memming,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kSpellbookDispatcherRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" scribe_callsite_rva=";
    evidence += Hex32(kSpellbookDispatcherCallsStartSpellScribePathAtRva);
    evidence += L" calls_start_spell_scribe_path=";
    evidence += calls_start_spell_scribe_path ? L"yes" : L"no";
    evidence += L" mem_gate_callsite_rva=";
    evidence += Hex32(kSpellbookDispatcherCallsCanStartMemmingAtRva);
    evidence += L" calls_can_start_memming=";
    evidence += calls_can_start_memming ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and scribe plus mem-gate caller shape",
        L"SpellbookDispatcher candidate did not satisfy spellbook dispatcher validation",
        &target);
    return target;
}

TargetResult DiscoverStartSpellScribePath(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"StartSpellScribePath"};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kStartSpellScribePathRva,
        kStartSpellScribePathEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_rva",
        L"StartSpellScribePath");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kStartSpellScribePathRva,
        kStartSpellScribePathEntryBytes.data(),
        kStartSpellScribePathEntryBytes.size());
    const bool called_from_spellbook_dispatcher =
        CallAtRvaTargets(
            image,
            kSpellbookDispatcherCallsStartSpellScribePathAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kStartSpellScribePathRva);
    const bool calls_get_spell_level_needed =
        CallAtRvaTargets(
            image,
            kStartSpellScribePathCallsGetSpellLevelNeededAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kGetSpellLevelNeededRva);

    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && called_from_spellbook_dispatcher &&
            calls_get_spell_level_needed,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kStartSpellScribePathRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" dispatcher_callsite_rva=";
    evidence += Hex32(kSpellbookDispatcherCallsStartSpellScribePathAtRva);
    evidence += L" called_from_spellbook_dispatcher=";
    evidence += called_from_spellbook_dispatcher ? L"yes" : L"no";
    evidence += L" get_spell_level_needed_callsite_rva=";
    evidence += Hex32(kStartSpellScribePathCallsGetSpellLevelNeededAtRva);
    evidence += L" calls_get_spell_level_needed=";
    evidence += calls_get_spell_level_needed ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, dispatcher caller shape, and GetSpellLevelNeeded call",
        L"StartSpellScribePath candidate did not satisfy spellbook scribe path validation",
        &target);
    return target;
}

TargetResult DiscoverStartSpellScribePrecheckModeGetter(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"StartSpellScribePrecheckModeGetter"};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kStartSpellScribePrecheckModeGetterRva,
        kStartSpellScribePrecheckModeGetterEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_rva",
        L"StartSpellScribePrecheckModeGetter");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kStartSpellScribePrecheckModeGetterRva,
        kStartSpellScribePrecheckModeGetterEntryBytes.data(),
        kStartSpellScribePrecheckModeGetterEntryBytes.size());
    const bool called_from_start_spell_scribe_path =
        CallAtRvaTargets(
            image,
            kStartSpellScribePathCallsPrecheckModeGetterAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) +
                kStartSpellScribePrecheckModeGetterRva);

    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && called_from_start_spell_scribe_path,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kStartSpellScribePrecheckModeGetterRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" scribe_callsite_rva=";
    evidence += Hex32(kStartSpellScribePathCallsPrecheckModeGetterAtRva);
    evidence += L" called_from_start_spell_scribe_path=";
    evidence += called_from_start_spell_scribe_path ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePath caller shape",
        L"StartSpellScribePrecheckModeGetter candidate did not satisfy precheck mode validation",
        &target);
    return target;
}

TargetResult DiscoverStartSpellScribePrecheckGate(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"StartSpellScribePrecheckGate"};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kStartSpellScribePrecheckGateRva,
        kStartSpellScribePrecheckGateEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_rva",
        L"StartSpellScribePrecheckGate");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kStartSpellScribePrecheckGateRva,
        kStartSpellScribePrecheckGateEntryBytes.data(),
        kStartSpellScribePrecheckGateEntryBytes.size());
    const bool called_from_start_spell_scribe_path =
        CallAtRvaTargets(
            image,
            kStartSpellScribePathCallsPrecheckGateAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) +
                kStartSpellScribePrecheckGateRva);

    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && called_from_start_spell_scribe_path,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kStartSpellScribePrecheckGateRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" scribe_callsite_rva=";
    evidence += Hex32(kStartSpellScribePathCallsPrecheckGateAtRva);
    evidence += L" called_from_start_spell_scribe_path=";
    evidence += called_from_start_spell_scribe_path ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePath caller shape",
        L"StartSpellScribePrecheckGate candidate did not satisfy precheck gate validation",
        &target);
    return target;
}

TargetResult DiscoverStartSpellScribePrecheckLookup(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"StartSpellScribePrecheckLookup"};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kStartSpellScribePrecheckLookupRva,
        kStartSpellScribePrecheckLookupEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_rva",
        L"StartSpellScribePrecheckLookup");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kStartSpellScribePrecheckLookupRva,
        kStartSpellScribePrecheckLookupEntryBytes.data(),
        kStartSpellScribePrecheckLookupEntryBytes.size());
    const bool called_from_start_spell_scribe_path =
        CallAtRvaTargets(
            image,
            kStartSpellScribePathCallsPrecheckLookupAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) +
                kStartSpellScribePrecheckLookupRva);

    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && called_from_start_spell_scribe_path,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kStartSpellScribePrecheckLookupRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" scribe_callsite_rva=";
    evidence += Hex32(kStartSpellScribePathCallsPrecheckLookupAtRva);
    evidence += L" called_from_start_spell_scribe_path=";
    evidence += called_from_start_spell_scribe_path ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePath caller shape",
        L"StartSpellScribePrecheckLookup candidate did not satisfy precheck lookup validation",
        &target);
    return target;
}

TargetResult DiscoverStartSpellScribeNestedPrecheckTarget(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint,
    const wchar_t* target_name,
    std::uint32_t target_rva,
    const std::uint8_t* entry_bytes,
    std::size_t entry_bytes_size,
    std::uint32_t caller_callsite_rva,
    const wchar_t* success_reason,
    const wchar_t* failure_reason) {
    TargetResult target = {target_name};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        target_rva,
        entry_bytes_size,
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_rva",
        target_name);
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        target_rva,
        entry_bytes,
        entry_bytes_size);
    const bool called_from_precheck_gate =
        CallAtRvaTargets(
            image,
            caller_callsite_rva,
            reinterpret_cast<std::uintptr_t>(image.base) + target_rva);

    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && called_from_precheck_gate,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(target_rva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" precheck_gate_callsite_rva=";
    evidence += Hex32(caller_callsite_rva);
    evidence += L" called_from_precheck_gate=";
    evidence += called_from_precheck_gate ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        success_reason,
        failure_reason,
        &target);
    return target;
}

TargetResult DiscoverStartSpellScribePrecheckFastAccept(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverStartSpellScribeNestedPrecheckTarget(
        image,
        cleanroom_fingerprint,
        L"StartSpellScribePrecheckFastAccept",
        kStartSpellScribePrecheckFastAcceptRva,
        kStartSpellScribePrecheckFastAcceptEntryBytes.data(),
        kStartSpellScribePrecheckFastAcceptEntryBytes.size(),
        kStartSpellScribePrecheckGateCallsFastAcceptAtRva,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePrecheckGate caller shape",
        L"StartSpellScribePrecheckFastAccept candidate did not satisfy nested precheck validation");
}

TargetResult DiscoverStartSpellScribePrecheckClassResolver(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverStartSpellScribeNestedPrecheckTarget(
        image,
        cleanroom_fingerprint,
        L"StartSpellScribePrecheckClassResolver",
        kStartSpellScribePrecheckClassResolverRva,
        kStartSpellScribePrecheckClassResolverEntryBytes.data(),
        kStartSpellScribePrecheckClassResolverEntryBytes.size(),
        kStartSpellScribePrecheckGateCallsClassResolverAtRva,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePrecheckGate caller shape",
        L"StartSpellScribePrecheckClassResolver candidate did not satisfy nested precheck validation");
}

TargetResult DiscoverStartSpellScribePrecheckAssignedMaskGetter(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverStartSpellScribeNestedPrecheckTarget(
        image,
        cleanroom_fingerprint,
        L"StartSpellScribePrecheckAssignedMaskGetter",
        kStartSpellScribePrecheckAssignedMaskGetterRva,
        kStartSpellScribePrecheckAssignedMaskGetterEntryBytes.data(),
        kStartSpellScribePrecheckAssignedMaskGetterEntryBytes.size(),
        kStartSpellScribePrecheckGateCallsAssignedMaskGetterAtRva,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePrecheckGate caller shape",
        L"StartSpellScribePrecheckAssignedMaskGetter candidate did not satisfy nested precheck validation");
}

TargetResult DiscoverStartSpellScribePrecheckRule446190(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverStartSpellScribeNestedPrecheckTarget(
        image,
        cleanroom_fingerprint,
        L"StartSpellScribePrecheckRule446190",
        kStartSpellScribePrecheckRule446190Rva,
        kStartSpellScribePrecheckRule446190EntryBytes.data(),
        kStartSpellScribePrecheckRule446190EntryBytes.size(),
        kStartSpellScribePrecheckGateCallsRule446190AtRva,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePrecheckGate caller shape",
        L"StartSpellScribePrecheckRule446190 candidate did not satisfy nested precheck validation");
}

TargetResult DiscoverStartSpellScribePrecheckRule446200(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverStartSpellScribeNestedPrecheckTarget(
        image,
        cleanroom_fingerprint,
        L"StartSpellScribePrecheckRule446200",
        kStartSpellScribePrecheckRule446200Rva,
        kStartSpellScribePrecheckRule446200EntryBytes.data(),
        kStartSpellScribePrecheckRule446200EntryBytes.size(),
        kStartSpellScribePrecheckGateCallsRule446200AtRva,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePrecheckGate caller shape",
        L"StartSpellScribePrecheckRule446200 candidate did not satisfy nested precheck validation");
}

TargetResult DiscoverStartSpellScribePrecheckRule4462c0(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverStartSpellScribeNestedPrecheckTarget(
        image,
        cleanroom_fingerprint,
        L"StartSpellScribePrecheckRule4462c0",
        kStartSpellScribePrecheckRule4462c0Rva,
        kStartSpellScribePrecheckRule4462c0EntryBytes.data(),
        kStartSpellScribePrecheckRule4462c0EntryBytes.size(),
        kStartSpellScribePrecheckGateCallsRule4462c0AtRva,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePrecheckGate caller shape",
        L"StartSpellScribePrecheckRule4462c0 candidate did not satisfy nested precheck validation");
}

TargetResult DiscoverStartSpellScribePrecheckRule446380(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverStartSpellScribeNestedPrecheckTarget(
        image,
        cleanroom_fingerprint,
        L"StartSpellScribePrecheckRule446380",
        kStartSpellScribePrecheckRule446380Rva,
        kStartSpellScribePrecheckRule446380EntryBytes.data(),
        kStartSpellScribePrecheckRule446380EntryBytes.size(),
        kStartSpellScribePrecheckGateCallsRule446380AtRva,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and StartSpellScribePrecheckGate caller shape",
        L"StartSpellScribePrecheckRule446380 candidate did not satisfy nested precheck validation");
}

TargetResult DiscoverStartSpellMemorizationPath(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"StartSpellMemorizationPath"};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kStartSpellMemorizationPathRva,
        kStartSpellMemorizationPathEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_cleanroom_rva",
        L"StartSpellMemorizationPath");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kStartSpellMemorizationPathRva,
        kStartSpellMemorizationPathEntryBytes.data(),
        kStartSpellMemorizationPathEntryBytes.size());
    const bool spellbook_dispatch_calls_candidate =
        candidate_executable &&
        CallAtRvaTargets(
            image,
            kSpellbookDispatcherCallsStartSpellMemorizationPathAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kStartSpellMemorizationPathRva);
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && spellbook_dispatch_calls_candidate,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kStartSpellMemorizationPathRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" dispatcher_callsite_rva=";
    evidence += Hex32(kSpellbookDispatcherCallsStartSpellMemorizationPathAtRva);
    evidence += L" spellbook_dispatch_calls_candidate=";
    evidence += spellbook_dispatch_calls_candidate ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and spellbook dispatcher caller shape",
        L"StartSpellMemorizationPath candidate did not satisfy spellbook mem-start validation",
        &target);
    return target;
}

TargetResult DiscoverSpellbookMemorizeSendPath(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"SpellbookMemorizeSendPath"};

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kSpellbookMemorizeSendPathRva,
        kSpellbookMemorizeSendPathEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_cleanroom_rva",
        L"SpellbookMemorizeSendPath");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kSpellbookMemorizeSendPathRva,
        kSpellbookMemorizeSendPathEntryBytes.data(),
        kSpellbookMemorizeSendPathEntryBytes.size());
    const bool calls_memorize_send_wrapper =
        candidate_executable &&
        CallAtRvaTargets(
            image,
            kSpellbookMemorizeSendPathCallsMemorizeSendPacketWrapperAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kMemorizeSendPacketWrapperRva);
    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && calls_memorize_send_wrapper,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kSpellbookMemorizeSendPathRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    evidence += L" wrapper_callsite_rva=";
    evidence += Hex32(kSpellbookMemorizeSendPathCallsMemorizeSendPacketWrapperAtRva);
    evidence += L" calls_memorize_send_wrapper=";
    evidence += calls_memorize_send_wrapper ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by fingerprint-gated cleanroom RVA, exact entry bytes, and OP_MemorizeSpell wrapper call shape",
        L"SpellbookMemorizeSendPath candidate did not satisfy ordinary spellbook memorize send validation",
        &target);
    return target;
}

TargetResult DiscoverMemorizeSendPacketWrapper(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    TargetResult target = {L"MemorizeSendPacketWrapper"};
    std::uintptr_t function_start = 0;
    const bool rva_found = ResolveRvaAddress(
        image,
        kMemorizeSendPacketWrapperRva,
        kMemorizeSendPacketWrapperEntryBytes.size(),
        &function_start);
    CandidateSource candidate = BuildFingerprintCandidate(
        image,
        function_start,
        L"fingerprint_cleanroom_rva",
        L"MemorizeSendPacketWrapper");
    const bool candidate_executable =
        candidate.address != 0 && IsExecutableAddress(image, candidate.address);
    const bool exact_entry_bytes = rva_found && BytesMatchAtRva(
        image,
        kMemorizeSendPacketWrapperRva,
        kMemorizeSendPacketWrapperEntryBytes.data(),
        kMemorizeSendPacketWrapperEntryBytes.size());
    std::uintptr_t resolved_call_target = 0;
    std::uint8_t caller_opcode = 0;
    const bool call_rva_resolved = ResolveCallTargetAtRva(
        image,
        kStartSpellScribeLikeCallsMemorizeSendPacketWrapperAtRva,
        &resolved_call_target,
        &caller_opcode);
    const bool start_spell_scribe_like_calls_wrapper =
        candidate_executable &&
        CallAtRvaTargets(
            image,
            kStartSpellScribeLikeCallsMemorizeSendPacketWrapperAtRva,
            reinterpret_cast<std::uintptr_t>(image.base) + kMemorizeSendPacketWrapperRva);

    if (!cleanroom_fingerprint.available || !cleanroom_fingerprint.matched) {
        target.state = TargetState::kFailed;
        target.validation = L"failed";
        target.failure_reason = cleanroom_fingerprint.failure_reason;
        target.validation_evidence = cleanroom_fingerprint.evidence;
        target.reason =
            L"checked-in cleanroom SHA-256 fingerprint did not match the live eqgame.exe";
        return target;
    }

    const DecisionResult decision = EvaluateDecision({
        true,
        true,
        rva_found,
        false,
        false,
        false,
        candidate_executable,
        exact_entry_bytes && start_spell_scribe_like_calls_wrapper,
        false,
        false,
        false,
        false,
        false,
    });

    std::wstring evidence = L"cleanroom_rva=";
    evidence += Hex32(kMemorizeSendPacketWrapperRva);
    evidence += L" executable=";
    evidence += candidate_executable ? L"yes" : L"no";
    evidence += L" exact_entry_bytes=";
    evidence += exact_entry_bytes ? L"yes" : L"no";
    if (rva_found) {
        evidence += L" expected_entry_bytes=\"";
        evidence += HexBytes(
            kMemorizeSendPacketWrapperEntryBytes.data(),
            kMemorizeSendPacketWrapperEntryBytes.size());
        evidence += L"\" actual_entry_bytes=\"";
        evidence += HexBytes(
            reinterpret_cast<const std::uint8_t*>(function_start),
            kMemorizeSendPacketWrapperEntryBytes.size());
        evidence += L"\"";
    }
    evidence += L" caller_rva=";
    evidence += Hex32(kStartSpellScribeLikeCallsMemorizeSendPacketWrapperAtRva);
    evidence += L" caller_opcode=";
    evidence += Hex32(static_cast<std::uint32_t>(caller_opcode));
    evidence += L" call_rva_resolved=";
    evidence += call_rva_resolved ? L"yes" : L"no";
    evidence += L" expected_call_target=";
    evidence += HexPtr(reinterpret_cast<std::uintptr_t>(image.base) + kMemorizeSendPacketWrapperRva);
    evidence += L" actual_call_target=";
    evidence += resolved_call_target == 0 ? L"0x0" : HexPtr(resolved_call_target);
    evidence += L" start_spell_scribe_like_calls_wrapper=";
    evidence += start_spell_scribe_like_calls_wrapper ? L"yes" : L"no";
    evidence += L" ";
    evidence += cleanroom_fingerprint.evidence;
    ApplyDecision(
        image,
        candidate,
        decision,
        evidence,
        L"validated by cleanroom RVA, exact entry bytes, and StartSpellScribe-like caller shape",
        L"MemorizeSendPacketWrapper candidate did not satisfy cleanroom RVA validation",
        &target);
    return target;
}

TargetResult DiscoverIsClassUsablePredicate(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverPinnedCleanroomTarget(
        image,
        L"EQ_Character::IsClassUsablePredicate",
        L"EQ_Character::IsClassUsablePredicate",
        kIsClassUsablePredicateRva,
        kIsClassUsablePredicateEntryBytes.data(),
        kIsClassUsablePredicateEntryBytes.size(),
        L"entry_bytes",
        cleanroom_fingerprint);
}

TargetResult DiscoverCanEquip(
    const ImageView& image,
    const CleanroomFingerprintStatus& cleanroom_fingerprint) {
    return DiscoverPinnedCleanroomTarget(
        image,
        L"EQ_Character::CanEquip",
        L"EQ_Character::CanEquip",
        kCanEquipRva,
        kCanEquipEntryBytes.data(),
        kCanEquipEntryBytes.size(),
        L"entry_bytes",
        cleanroom_fingerprint);
}

TargetResult DiscoverInvSlotMgrMoveItem(const ImageView& image) {
    return DiscoverRuntimeExportTarget(
        image,
        L"CInvSlotMgr::MoveItem",
        "?MoveItem@CInvSlotMgr@EQClasses@@QAE_NPAVCMoveItemData@2@0HHHH@Z",
        L"?MoveItem@CInvSlotMgr@EQClasses@@QAE_NPAVCMoveItemData@2@0HHHH@Z");
}

}  // namespace

void Initialize() noexcept {
    g_result = {};
    g_result.reason = L"initialized";
    g_result.handle_rbutton_up = {L"CInvSlot::HandleRButtonUp"};
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.is_class_usable_predicate = {L"EQ_Character::IsClassUsablePredicate"};
    g_result.can_equip = {L"EQ_Character::CanEquip"};
    g_result.inv_slot_mgr_move_item = {L"CInvSlotMgr::MoveItem"};
    g_result.spellbook_dispatcher = {L"SpellbookDispatcher"};
    g_result.start_spell_scribe_path = {L"StartSpellScribePath"};
    g_result.start_spell_scribe_precheck_mode_getter = {
        L"StartSpellScribePrecheckModeGetter"};
    g_result.start_spell_scribe_precheck_gate = {L"StartSpellScribePrecheckGate"};
    g_result.start_spell_scribe_precheck_lookup = {L"StartSpellScribePrecheckLookup"};
    g_result.start_spell_scribe_precheck_fast_accept = {
        L"StartSpellScribePrecheckFastAccept"};
    g_result.start_spell_scribe_precheck_class_resolver = {
        L"StartSpellScribePrecheckClassResolver"};
    g_result.start_spell_scribe_precheck_assigned_mask_getter = {
        L"StartSpellScribePrecheckAssignedMaskGetter"};
    g_result.start_spell_scribe_precheck_rule_4462c0 = {
        L"StartSpellScribePrecheckRule4462c0"};
    g_result.start_spell_scribe_precheck_rule_446190 = {
        L"StartSpellScribePrecheckRule446190"};
    g_result.start_spell_scribe_precheck_rule_446200 = {
        L"StartSpellScribePrecheckRule446200"};
    g_result.start_spell_scribe_precheck_rule_446380 = {
        L"StartSpellScribePrecheckRule446380"};
    g_result.can_start_memming = {L"CanStartMemming"};
    g_result.spellbook_memorize_send_path = {L"SpellbookMemorizeSendPath"};
    g_result.start_spell_memorization_path = {L"StartSpellMemorizationPath"};
    g_result.memorize_send_packet_wrapper = {L"MemorizeSendPacketWrapper"};
}

Result Run(bool discovery_allowed, bool fingerprint_matched) noexcept {
    g_result = {};
    g_result.allowed = discovery_allowed;
    g_result.handle_rbutton_up = {L"CInvSlot::HandleRButtonUp"};
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.is_class_usable_predicate = {L"EQ_Character::IsClassUsablePredicate"};
    g_result.can_equip = {L"EQ_Character::CanEquip"};
    g_result.inv_slot_mgr_move_item = {L"CInvSlotMgr::MoveItem"};
    g_result.spellbook_dispatcher = {L"SpellbookDispatcher"};
    g_result.start_spell_scribe_path = {L"StartSpellScribePath"};
    g_result.start_spell_scribe_precheck_mode_getter = {
        L"StartSpellScribePrecheckModeGetter"};
    g_result.start_spell_scribe_precheck_gate = {L"StartSpellScribePrecheckGate"};
    g_result.start_spell_scribe_precheck_lookup = {L"StartSpellScribePrecheckLookup"};
    g_result.start_spell_scribe_precheck_fast_accept = {
        L"StartSpellScribePrecheckFastAccept"};
    g_result.start_spell_scribe_precheck_class_resolver = {
        L"StartSpellScribePrecheckClassResolver"};
    g_result.start_spell_scribe_precheck_assigned_mask_getter = {
        L"StartSpellScribePrecheckAssignedMaskGetter"};
    g_result.start_spell_scribe_precheck_rule_4462c0 = {
        L"StartSpellScribePrecheckRule4462c0"};
    g_result.start_spell_scribe_precheck_rule_446190 = {
        L"StartSpellScribePrecheckRule446190"};
    g_result.start_spell_scribe_precheck_rule_446200 = {
        L"StartSpellScribePrecheckRule446200"};
    g_result.start_spell_scribe_precheck_rule_446380 = {
        L"StartSpellScribePrecheckRule446380"};
    g_result.can_start_memming = {L"CanStartMemming"};
    g_result.spellbook_memorize_send_path = {L"SpellbookMemorizeSendPath"};
    g_result.start_spell_memorization_path = {L"StartSpellMemorizationPath"};
    g_result.memorize_send_packet_wrapper = {L"MemorizeSendPacketWrapper"};

    if (!discovery_allowed) {
        g_result.reason =
            L"skipped because capability manifest denied spell usability discovery";
        if (!fingerprint_matched) {
            g_result.reason = L"skipped because ROF2 fingerprint did not match";
        }
        const wchar_t* failure_reason =
            fingerprint_matched ? L"capability_denied" : L"fingerprint_mismatch";
        g_result.handle_rbutton_up = BuildCapabilityDeniedTarget(
            L"CInvSlot::HandleRButtonUp",
            failure_reason);
        g_result.get_spell_level_needed = BuildCapabilityDeniedTarget(
            L"GetSpellLevelNeeded",
            failure_reason);
        g_result.is_class_usable_predicate = BuildCapabilityDeniedTarget(
            L"EQ_Character::IsClassUsablePredicate",
            failure_reason);
        g_result.can_equip = BuildCapabilityDeniedTarget(
            L"EQ_Character::CanEquip",
            failure_reason);
        g_result.inv_slot_mgr_move_item = BuildCapabilityDeniedTarget(
            L"CInvSlotMgr::MoveItem",
            failure_reason);
        g_result.spellbook_dispatcher = BuildCapabilityDeniedTarget(
            L"SpellbookDispatcher",
            failure_reason);
        g_result.start_spell_scribe_path = BuildCapabilityDeniedTarget(
            L"StartSpellScribePath",
            failure_reason);
        g_result.start_spell_scribe_precheck_mode_getter = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckModeGetter",
            failure_reason);
        g_result.start_spell_scribe_precheck_gate = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckGate",
            failure_reason);
        g_result.start_spell_scribe_precheck_lookup = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckLookup",
            failure_reason);
        g_result.start_spell_scribe_precheck_fast_accept = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckFastAccept",
            failure_reason);
        g_result.start_spell_scribe_precheck_class_resolver = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckClassResolver",
            failure_reason);
        g_result.start_spell_scribe_precheck_assigned_mask_getter =
            BuildCapabilityDeniedTarget(
                L"StartSpellScribePrecheckAssignedMaskGetter",
                failure_reason);
        g_result.start_spell_scribe_precheck_rule_4462c0 = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckRule4462c0",
            failure_reason);
        g_result.start_spell_scribe_precheck_rule_446190 = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckRule446190",
            failure_reason);
        g_result.start_spell_scribe_precheck_rule_446200 = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckRule446200",
            failure_reason);
        g_result.start_spell_scribe_precheck_rule_446380 = BuildCapabilityDeniedTarget(
            L"StartSpellScribePrecheckRule446380",
            failure_reason);
        g_result.can_start_memming = BuildCapabilityDeniedTarget(
            L"CanStartMemming",
            failure_reason);
        g_result.spellbook_memorize_send_path = BuildCapabilityDeniedTarget(
            L"SpellbookMemorizeSendPath",
            failure_reason);
        g_result.start_spell_memorization_path = BuildCapabilityDeniedTarget(
            L"StartSpellMemorizationPath",
            failure_reason);
        g_result.memorize_send_packet_wrapper = BuildCapabilityDeniedTarget(
            L"MemorizeSendPacketWrapper",
            failure_reason);
        return g_result;
    }

    ImageView image = {};
    if (!BuildImageView(&image)) {
        g_result.reason = L"failed because host PE image unavailable";
        g_result.handle_rbutton_up = BuildImageUnavailableTarget(L"CInvSlot::HandleRButtonUp");
        g_result.get_spell_level_needed = BuildImageUnavailableTarget(L"GetSpellLevelNeeded");
        g_result.is_class_usable_predicate =
            BuildImageUnavailableTarget(L"EQ_Character::IsClassUsablePredicate");
        g_result.can_equip = BuildImageUnavailableTarget(L"EQ_Character::CanEquip");
        g_result.inv_slot_mgr_move_item =
            BuildImageUnavailableTarget(L"CInvSlotMgr::MoveItem");
        g_result.spellbook_dispatcher =
            BuildImageUnavailableTarget(L"SpellbookDispatcher");
        g_result.start_spell_scribe_path =
            BuildImageUnavailableTarget(L"StartSpellScribePath");
        g_result.start_spell_scribe_precheck_mode_getter =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckModeGetter");
        g_result.start_spell_scribe_precheck_gate =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckGate");
        g_result.start_spell_scribe_precheck_lookup =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckLookup");
        g_result.start_spell_scribe_precheck_fast_accept =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckFastAccept");
        g_result.start_spell_scribe_precheck_class_resolver =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckClassResolver");
        g_result.start_spell_scribe_precheck_assigned_mask_getter =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckAssignedMaskGetter");
        g_result.start_spell_scribe_precheck_rule_4462c0 =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckRule4462c0");
        g_result.start_spell_scribe_precheck_rule_446190 =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckRule446190");
        g_result.start_spell_scribe_precheck_rule_446200 =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckRule446200");
        g_result.start_spell_scribe_precheck_rule_446380 =
            BuildImageUnavailableTarget(L"StartSpellScribePrecheckRule446380");
        g_result.can_start_memming = BuildImageUnavailableTarget(L"CanStartMemming");
        g_result.spellbook_memorize_send_path =
            BuildImageUnavailableTarget(L"SpellbookMemorizeSendPath");
        g_result.start_spell_memorization_path =
            BuildImageUnavailableTarget(L"StartSpellMemorizationPath");
        g_result.memorize_send_packet_wrapper =
            BuildImageUnavailableTarget(L"MemorizeSendPacketWrapper");
        return g_result;
    }

    const CleanroomFingerprintStatus cleanroom_fingerprint =
        EvaluateCleanroomFingerprint();
    g_result.reason = L"resolver=";
    g_result.reason += kResolverVersion;
    g_result.reason += L" packet_id=";
    g_result.reason += kPacketId;
    g_result.reason += L" known_non_export_targets=cleanroom_or_fail_closed";
    g_result.reason += L" runtime_export_targets=CInvSlotMgr::MoveItem";
    g_result.handle_rbutton_up = DiscoverHandleRButtonUp(image, cleanroom_fingerprint);
    g_result.get_spell_level_needed = DiscoverGetSpellLevelNeeded(image);
    g_result.is_class_usable_predicate =
        DiscoverIsClassUsablePredicate(image, cleanroom_fingerprint);
    g_result.can_equip = DiscoverCanEquip(image, cleanroom_fingerprint);
    g_result.inv_slot_mgr_move_item = DiscoverInvSlotMgrMoveItem(image);
    g_result.spellbook_dispatcher =
        DiscoverSpellbookDispatcher(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_path =
        DiscoverStartSpellScribePath(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_mode_getter =
        DiscoverStartSpellScribePrecheckModeGetter(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_gate =
        DiscoverStartSpellScribePrecheckGate(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_lookup =
        DiscoverStartSpellScribePrecheckLookup(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_fast_accept =
        DiscoverStartSpellScribePrecheckFastAccept(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_class_resolver =
        DiscoverStartSpellScribePrecheckClassResolver(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_assigned_mask_getter =
        DiscoverStartSpellScribePrecheckAssignedMaskGetter(
            image,
            cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_rule_4462c0 =
        DiscoverStartSpellScribePrecheckRule4462c0(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_rule_446190 =
        DiscoverStartSpellScribePrecheckRule446190(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_rule_446200 =
        DiscoverStartSpellScribePrecheckRule446200(image, cleanroom_fingerprint);
    g_result.start_spell_scribe_precheck_rule_446380 =
        DiscoverStartSpellScribePrecheckRule446380(image, cleanroom_fingerprint);
    g_result.can_start_memming = DiscoverCanStartMemming(image);
    g_result.spellbook_memorize_send_path =
        DiscoverSpellbookMemorizeSendPath(image, cleanroom_fingerprint);
    g_result.start_spell_memorization_path =
        DiscoverStartSpellMemorizationPath(image, cleanroom_fingerprint);
    g_result.memorize_send_packet_wrapper =
        DiscoverMemorizeSendPacketWrapper(image, cleanroom_fingerprint);
    return g_result;
}

void Shutdown() noexcept {
    g_result = {};
    g_result.reason = L"shutdown";
    g_result.handle_rbutton_up = {L"CInvSlot::HandleRButtonUp"};
    g_result.get_spell_level_needed = {L"GetSpellLevelNeeded"};
    g_result.is_class_usable_predicate = {L"EQ_Character::IsClassUsablePredicate"};
    g_result.can_equip = {L"EQ_Character::CanEquip"};
    g_result.inv_slot_mgr_move_item = {L"CInvSlotMgr::MoveItem"};
    g_result.spellbook_dispatcher = {L"SpellbookDispatcher"};
    g_result.start_spell_scribe_path = {L"StartSpellScribePath"};
    g_result.start_spell_scribe_precheck_mode_getter = {
        L"StartSpellScribePrecheckModeGetter"};
    g_result.start_spell_scribe_precheck_gate = {L"StartSpellScribePrecheckGate"};
    g_result.start_spell_scribe_precheck_lookup = {L"StartSpellScribePrecheckLookup"};
    g_result.start_spell_scribe_precheck_fast_accept = {
        L"StartSpellScribePrecheckFastAccept"};
    g_result.start_spell_scribe_precheck_class_resolver = {
        L"StartSpellScribePrecheckClassResolver"};
    g_result.start_spell_scribe_precheck_assigned_mask_getter = {
        L"StartSpellScribePrecheckAssignedMaskGetter"};
    g_result.start_spell_scribe_precheck_rule_4462c0 = {
        L"StartSpellScribePrecheckRule4462c0"};
    g_result.start_spell_scribe_precheck_rule_446190 = {
        L"StartSpellScribePrecheckRule446190"};
    g_result.start_spell_scribe_precheck_rule_446200 = {
        L"StartSpellScribePrecheckRule446200"};
    g_result.start_spell_scribe_precheck_rule_446380 = {
        L"StartSpellScribePrecheckRule446380"};
    g_result.can_start_memming = {L"CanStartMemming"};
    g_result.spellbook_memorize_send_path = {L"SpellbookMemorizeSendPath"};
    g_result.start_spell_memorization_path = {L"StartSpellMemorizationPath"};
    g_result.memorize_send_packet_wrapper = {L"MemorizeSendPacketWrapper"};
}

Result GetResult() noexcept {
    return g_result;
}

void LogResult(const Result& result) noexcept {
    const TargetResult* targets[] = {
        &result.handle_rbutton_up,
        &result.get_spell_level_needed,
        &result.is_class_usable_predicate,
        &result.can_equip,
        &result.inv_slot_mgr_move_item,
        &result.spellbook_dispatcher,
        &result.start_spell_scribe_path,
        &result.start_spell_scribe_precheck_mode_getter,
        &result.start_spell_scribe_precheck_gate,
        &result.start_spell_scribe_precheck_lookup,
        &result.start_spell_scribe_precheck_fast_accept,
        &result.start_spell_scribe_precheck_class_resolver,
        &result.start_spell_scribe_precheck_assigned_mask_getter,
        &result.start_spell_scribe_precheck_rule_4462c0,
        &result.start_spell_scribe_precheck_rule_446190,
        &result.start_spell_scribe_precheck_rule_446200,
        &result.start_spell_scribe_precheck_rule_446380,
        &result.can_start_memming,
        &result.spellbook_memorize_send_path,
        &result.start_spell_memorization_path,
        &result.memorize_send_packet_wrapper,
    };

    for (const TargetResult* target : targets) {
        if (target == nullptr) {
            continue;
        }

        std::wstring message = L"SpellUsabilityDiscovery target=";
        message += target->target;
        message += L" enabled=";
        message += target->state == TargetState::kValidated ? L"true" : L"false";
        message += L" state=";
        message += TargetStateName(target->state);
        message += L" evidence_source=";
        message += target->evidence_source;
        message += L" validation=";
        message += target->validation;
        message += L" failure_reason=";
        message += target->failure_reason;
        message += L" resolver=";
        message += kResolverVersion;
        message += L" packet_id=";
        message += kPacketId;
        if (target->module_base != 0) {
            message += L" module_base=";
            message += HexPtr(target->module_base);
        }
        if (target->candidate_rva != 0) {
            message += L" rva=";
            message += Hex32(target->candidate_rva);
        }
        if (target->candidate_address != 0) {
            message += L" address=";
            message += HexPtr(target->candidate_address);
        }
        message += L" trace_safe=";
        message += target->trace_safe ? L"true" : L"false";
        message += L" method=";
        message += target->discovery_method.empty() ? L"unknown" : target->discovery_method;
        if (!target->resolved_symbol.empty()) {
            message += L" symbol=\"";
            message += target->resolved_symbol;
            message += L"\"";
        }
        message += L" reason=\"";
        message += target->reason.empty() ? L"unknown" : target->reason;
        message += L"\"";
        message += L" evidence=\"";
        message += target->validation_evidence.empty() ? L"unknown" : target->validation_evidence;
        message += L"\"";
        monomyth::logger::Log(message);
    }
}

}  // namespace monomyth::spell_usability_discovery
