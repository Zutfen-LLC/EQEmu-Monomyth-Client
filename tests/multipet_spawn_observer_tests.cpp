#include "multipet_spawn_observer.h"
#include "server_auth_stats_observer.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

namespace monomyth::logger {

void Log(std::wstring_view) noexcept {}
void Log(const wchar_t*) noexcept {}
void Flush() noexcept {}

}  // namespace monomyth::logger

namespace {

void AppendU8(std::vector<std::uint8_t>* bytes, std::uint8_t value) {
    bytes->push_back(value);
}

void AppendU32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
    bytes->push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes->push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    bytes->push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    bytes->push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

void AppendF32(std::vector<std::uint8_t>* bytes, float value) {
    std::uint32_t raw = 0;
    static_assert(sizeof(raw) == sizeof(value));
    std::memcpy(&raw, &value, sizeof(raw));
    AppendU32(bytes, raw);
}

void AppendCString(std::vector<std::uint8_t>* bytes, std::string_view value) {
    bytes->insert(bytes->end(), value.begin(), value.end());
    bytes->push_back(0);
}

std::vector<std::uint8_t> BuildSpawnPayload(
    std::string_view name,
    std::uint32_t spawn_id,
    std::uint32_t owner_id) {
    std::vector<std::uint8_t> bytes;
    AppendCString(&bytes, name);
    AppendU32(&bytes, spawn_id);
    AppendU8(&bytes, 33);
    AppendF32(&bytes, 5.3f);
    AppendU8(&bytes, 1);
    AppendU32(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendF32(&bytes, -1.0f);
    AppendF32(&bytes, 0.0f);
    AppendU8(&bytes, 1);
    AppendU32(&bytes, 11);
    for (int i = 0; i < 7; ++i) {
        AppendU8(&bytes, 0);
    }
    AppendU32(&bytes, 0);
    AppendU32(&bytes, 0);
    AppendU32(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendF32(&bytes, 6.0f);
    AppendU8(&bytes, 0);
    AppendF32(&bytes, 0.7f);
    AppendF32(&bytes, 1.4f);
    AppendU32(&bytes, 127);
    AppendU8(&bytes, 0);
    AppendU32(&bytes, 0);
    AppendU32(&bytes, 0xffffffff);
    AppendU32(&bytes, 0);
    AppendU8(&bytes, 1);
    AppendU8(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendCString(&bytes, "");
    AppendU32(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendU8(&bytes, 0);
    AppendU32(&bytes, owner_id);
    return bytes;
}

std::vector<std::uint8_t> BuildDeleteSpawnPayload(std::uint32_t spawn_id) {
    std::vector<std::uint8_t> bytes;
    AppendU32(&bytes, spawn_id);
    AppendU8(&bytes, 0);
    return bytes;
}

std::vector<std::uint8_t> BuildServerAuthStatsPayload(std::uint32_t focused_pet_id) {
    std::vector<std::uint8_t> bytes;
    AppendU32(&bytes, 1);
    AppendU32(&bytes, 102);
    bytes.push_back(static_cast<std::uint8_t>(focused_pet_id & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((focused_pet_id >> 8) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((focused_pet_id >> 16) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((focused_pet_id >> 24) & 0xff));
    AppendU32(&bytes, 0);
    return bytes;
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

    const auto valid_payload = BuildSpawnPayload("Splashboi000", 101, 42);
    monomyth::multipet_spawn_observer::ParsedSpawnEntry parsed = {};
    passed &= Expect(
        monomyth::multipet_spawn_observer::ParseSingleSpawnPayload(
            valid_payload.data(),
            static_cast<std::uint32_t>(valid_payload.size()),
            &parsed),
        "parse returns true");
    passed &= Expect(parsed.valid, "parsed entry valid");
    passed &= Expect(parsed.spawn_id == 101, "parsed spawn id");
    passed &= Expect(parsed.owner_id == 42, "parsed owner id");
    passed &= Expect(parsed.name == "Splashboi000", "parsed raw name");

    monomyth::multipet_spawn_observer::Clear();
    const auto splashboi = BuildSpawnPayload("Splashboi000", 101, 42);
    const auto emberpaw = BuildSpawnPayload("Emberpaw000", 102, 42);
    const auto fang = BuildSpawnPayload("Fang000", 103, 42);

    monomyth::multipet_spawn_observer::ObserveSpawnPayload(
        splashboi.data(),
        static_cast<std::uint32_t>(splashboi.size()));
    monomyth::multipet_spawn_observer::ObserveSpawnPayload(
        emberpaw.data(),
        static_cast<std::uint32_t>(emberpaw.size()));
    monomyth::multipet_spawn_observer::ObserveSpawnPayload(
        fang.data(),
        static_cast<std::uint32_t>(fang.size()));

    const auto focus_splashboi = BuildServerAuthStatsPayload(101);
    monomyth::server_auth_stats::ObserveReceivePayload(
        focus_splashboi.data(),
        static_cast<std::uint32_t>(focus_splashboi.size()));

    auto snapshot = monomyth::multipet_spawn_observer::GetSnapshot();
    passed &= Expect(snapshot.focused_pet_id == 101, "focused pet id");
    passed &= Expect(snapshot.has_other_pet_name[0], "slot0 has name");
    passed &= Expect(snapshot.other_pet_name[0] == "Emberpaw", "slot0 order");
    passed &= Expect(snapshot.has_other_pet_spawn_id[0], "slot0 has spawn id");
    passed &= Expect(snapshot.other_pet_spawn_id[0] == 102, "slot0 spawn id");
    passed &= Expect(snapshot.has_other_pet_name[1], "slot1 has name");
    passed &= Expect(snapshot.other_pet_name[1] == "Fang", "slot1 order");
    passed &= Expect(snapshot.has_other_pet_spawn_id[1], "slot1 has spawn id");
    passed &= Expect(snapshot.other_pet_spawn_id[1] == 103, "slot1 spawn id");

    const auto delete_emberpaw = BuildDeleteSpawnPayload(102);
    monomyth::multipet_spawn_observer::ObserveDeleteSpawnPayload(
        delete_emberpaw.data(),
        static_cast<std::uint32_t>(delete_emberpaw.size()));
    snapshot = monomyth::multipet_spawn_observer::GetSnapshot();
    passed &= Expect(snapshot.has_other_pet_name[0], "slot0 survives delete");
    passed &= Expect(snapshot.other_pet_name[0] == "Fang", "slot0 after delete");
    passed &= Expect(snapshot.has_other_pet_spawn_id[0], "slot0 spawn id survives delete");
    passed &= Expect(snapshot.other_pet_spawn_id[0] == 103, "slot0 spawn id after delete");
    passed &= Expect(!snapshot.has_other_pet_name[1], "slot1 cleared after delete");
    passed &= Expect(!snapshot.has_other_pet_spawn_id[1], "slot1 spawn id cleared after delete");

    monomyth::multipet_spawn_observer::ObserveSpawnPayload(
        emberpaw.data(),
        static_cast<std::uint32_t>(emberpaw.size()));
    const auto focus_fang = BuildServerAuthStatsPayload(103);
    monomyth::server_auth_stats::ObserveReceivePayload(
        focus_fang.data(),
        static_cast<std::uint32_t>(focus_fang.size()));
    snapshot = monomyth::multipet_spawn_observer::GetSnapshot();
    passed &= Expect(snapshot.focused_pet_id == 103, "focused pet changes");
    passed &= Expect(snapshot.has_other_pet_name[0], "slot0 after refocus");
    passed &= Expect(snapshot.other_pet_name[0] == "Splashboi", "slot0 stable order");
    passed &= Expect(snapshot.has_other_pet_spawn_id[0], "slot0 spawn id after refocus");
    passed &= Expect(snapshot.other_pet_spawn_id[0] == 101, "slot0 spawn id stable order");
    passed &= Expect(snapshot.has_other_pet_name[1], "slot1 after refocus");
    passed &= Expect(snapshot.other_pet_name[1] == "Emberpaw", "slot1 stable order");
    passed &= Expect(snapshot.has_other_pet_spawn_id[1], "slot1 spawn id after refocus");
    passed &= Expect(snapshot.other_pet_spawn_id[1] == 102, "slot1 spawn id stable order");

    return passed ? 0 : 1;
}
