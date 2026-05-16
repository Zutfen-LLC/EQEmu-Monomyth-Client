#include "packet_observer.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "logger.h"

// PacketObserver remains metadata-only unless both packet hook and receive
// introspection dev opt-ins are enabled. Even then, payload access is bounded
// to a small allowlisted prefix and never affects control flow.

namespace monomyth::packet_observer {
namespace {

constexpr std::uint64_t kFirstPacketLogLimit = 50;
constexpr std::uint64_t kPacketLogSampleInterval = 500;
constexpr std::uint64_t kFirstIntrospectionLogLimit = 10;
constexpr std::uint64_t kIntrospectionLogSampleInterval = 1000;
constexpr std::uint32_t kPayloadSafetyCeiling = 4096;
constexpr std::size_t kPrefixByteCap = 16;
constexpr std::array<std::uint32_t, 1> kDefaultAllowlist = {
    0x7dfc,
};

std::atomic<State> g_state = State::kUnavailable;
std::atomic<std::uint64_t> g_observed_count = 0;
std::atomic<bool> g_introspection_enabled = false;
std::atomic<std::uint64_t> g_introspection_match_count = 0;
std::atomic<std::uint64_t> g_introspection_skip_count = 0;
std::vector<std::uint32_t> g_introspection_allowlist;

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

bool ShouldLogPacket(std::uint64_t sequence) noexcept {
    return sequence <= kFirstPacketLogLimit ||
        (sequence % kPacketLogSampleInterval) == 0;
}

bool ShouldLogIntrospection(std::uint64_t sequence) noexcept {
    return sequence <= kFirstIntrospectionLogLimit ||
        (sequence % kIntrospectionLogSampleInterval) == 0;
}

std::wstring TrimWhitespace(std::wstring_view value) {
    std::size_t start = 0;
    std::size_t end = value.size();
    while (start < end && iswspace(value[start]) != 0) {
        ++start;
    }
    while (end > start && iswspace(value[end - 1]) != 0) {
        --end;
    }
    return std::wstring(value.substr(start, end - start));
}

bool ParseOpcodeToken(std::wstring_view token, std::uint32_t* opcode) noexcept {
    if (opcode == nullptr) {
        return false;
    }

    const std::wstring trimmed = TrimWhitespace(token);
    if (trimmed.empty()) {
        return false;
    }

    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(trimmed.c_str(), &end, 0);
    if (end == nullptr || *end != L'\0' ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    *opcode = static_cast<std::uint32_t>(parsed);
    return true;
}

std::vector<std::uint32_t> LoadIntrospectionAllowlist() {
    wchar_t value[256] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(
        L"MONOMYTH_RECV_INTROSPECT_OPCODES",
        value,
        kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        return std::vector<std::uint32_t>(kDefaultAllowlist.begin(), kDefaultAllowlist.end());
    }

    std::vector<std::uint32_t> opcodes;
    std::wstring_view remaining(value, length);
    while (!remaining.empty()) {
        const std::size_t separator = remaining.find(L',');
        const std::wstring_view token = remaining.substr(0, separator);
        std::uint32_t opcode = 0;
        if (ParseOpcodeToken(token, &opcode)) {
            opcodes.push_back(opcode);
        }

        if (separator == std::wstring_view::npos) {
            break;
        }
        remaining.remove_prefix(separator + 1);
    }

    if (opcodes.empty()) {
        return std::vector<std::uint32_t>(kDefaultAllowlist.begin(), kDefaultAllowlist.end());
    }

    std::sort(opcodes.begin(), opcodes.end());
    opcodes.erase(std::unique(opcodes.begin(), opcodes.end()), opcodes.end());
    return opcodes;
}

bool IsIntrospectionAllowlisted(std::uint32_t opcode) noexcept {
    for (const std::uint32_t candidate : g_introspection_allowlist) {
        if (candidate == opcode) {
            return true;
        }
    }
    return false;
}

std::wstring BuildAllowlistSummary() {
    std::wstringstream stream;
    for (std::size_t i = 0; i < g_introspection_allowlist.size(); ++i) {
        if (i != 0) {
            stream << L",";
        }
        stream << Hex32(g_introspection_allowlist[i]);
    }
    return stream.str();
}

void LogIntrospectionSkip(
    std::uint64_t sequence,
    std::uint32_t opcode,
    std::uint32_t payload_length,
    const wchar_t* reason) {
    if (!ShouldLogIntrospection(sequence)) {
        return;
    }

    std::wstringstream message;
    message
        << L"PacketObserverRecvIntrospectionSkip"
        << L" seq=" << sequence
        << L" opcode=" << opcode
        << L" opcode_hex=" << Hex32(opcode)
        << L" payload_length=" << payload_length
        << L" reason=\"" << ((reason == nullptr || reason[0] == L'\0') ? L"unknown" : reason)
        << L"\"";
    monomyth::logger::Log(message.str());
}

bool TryCopyPrefixBytes(
    const void* payload,
    std::size_t prefix_length,
    std::uint8_t* prefix_out) noexcept {
    if (payload == nullptr || prefix_out == nullptr || prefix_length == 0 ||
        prefix_length > kPrefixByteCap) {
        return false;
    }

#if defined(_MSC_VER)
    __try {
        std::memcpy(prefix_out, payload, prefix_length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(prefix_out, payload, prefix_length);
#endif
    return true;
}

std::wstring FormatPrefixHex(
    const std::uint8_t* prefix,
    std::size_t prefix_length) {
    if (prefix == nullptr || prefix_length == 0 || prefix_length > kPrefixByteCap) {
        return L"";
    }

    std::wstringstream stream;
    stream << std::hex << std::setfill(L'0');
    for (std::size_t i = 0; i < prefix_length; ++i) {
        if (i != 0) {
            stream << L' ';
        }
        stream << std::setw(2) << static_cast<unsigned int>(prefix[i]);
    }
    return stream.str();
}

}  // namespace

State Initialize(const monomyth::runtime::Manifest& manifest) noexcept {
    const State current = g_state.load();
    if (current == State::kInitialized) {
        return current;
    }

    if (!manifest.packet_hooks_allowed) {
        g_state.store(State::kDisabledByCapability);
        std::wstring message =
            L"PacketObserver state=disabled_by_capability packet_hooks_allowed=false receive_dispatch_discovery=";
        message += monomyth::receive_dispatch_discovery::StateName(
            manifest.receive_dispatch_discovery_state);
        message += L" reason=\"";
        if (manifest.packet_hooks_reason.empty()) {
            message += L"unknown";
        } else {
            message += manifest.packet_hooks_reason;
        }
        message += L"\"";
        monomyth::logger::Log(message);
        return g_state.load();
    }

    g_observed_count.store(0);
    g_introspection_match_count.store(0);
    g_introspection_skip_count.store(0);
    g_introspection_enabled.store(manifest.receive_introspection_allowed);
    g_introspection_allowlist = LoadIntrospectionAllowlist();
    g_state.store(State::kInitialized);

    std::wstring message =
        L"PacketObserver state=initialized mode=metadata_only log_policy=first_50_then_every_500";
    if (manifest.receive_introspection_allowed) {
        message += L" recv_introspection=true recv_introspection_prefix_cap=16";
        message += L" recv_introspection_safety_ceiling=";
        message += std::to_wstring(kPayloadSafetyCeiling);
        message += L" recv_introspection_log_policy=first_10_then_every_1000";
        message += L" recv_introspection_scope=allowlisted_only";
        message += L" recv_introspection_allowlist=";
        message += BuildAllowlistSummary();
    } else {
        message += L" recv_introspection=false";
    }
    monomyth::logger::Log(message);
    return g_state.load();
}

void DisableBecauseHookUnavailable(const wchar_t* reason) noexcept {
    const State previous = g_state.exchange(State::kDisabledByHookFailure);
    if (previous == State::kDisabledByHookFailure) {
        return;
    }

    std::wstring message = L"PacketObserver state=disabled_by_hook_failure reason=\"";
    message += (reason == nullptr || reason[0] == L'\0') ? L"unknown" : reason;
    message += L"\"";
    monomyth::logger::Log(message);
}

void ObserveReceiveMetadata(
    std::uint32_t opcode,
    std::uint32_t payload_length,
    const void* payload,
    std::uintptr_t source_context) noexcept {
    if (g_state.load() != State::kInitialized) {
        return;
    }

    const std::uint64_t sequence = g_observed_count.fetch_add(1) + 1;
    if (ShouldLogPacket(sequence)) {
        std::wstringstream message;
        message
            << L"PacketObserverRecv"
            << L" seq=" << sequence
            << L" opcode=" << opcode
            << L" opcode_hex=" << Hex32(opcode)
            << L" payload_length=" << payload_length
            << L" source_context=" << HexPtr(source_context);
        monomyth::logger::Log(message.str());
    }

    if (!g_introspection_enabled.load() || !IsIntrospectionAllowlisted(opcode)) {
        return;
    }

    const std::uint64_t introspection_sequence = g_introspection_match_count.fetch_add(1) + 1;
    if (payload_length == 0) {
        const std::uint64_t skip_sequence = g_introspection_skip_count.fetch_add(1) + 1;
        LogIntrospectionSkip(skip_sequence, opcode, payload_length, L"zero_length");
        return;
    }
    if (payload == nullptr) {
        const std::uint64_t skip_sequence = g_introspection_skip_count.fetch_add(1) + 1;
        LogIntrospectionSkip(skip_sequence, opcode, payload_length, L"null_payload");
        return;
    }
    if (payload_length > kPayloadSafetyCeiling) {
        const std::uint64_t skip_sequence = g_introspection_skip_count.fetch_add(1) + 1;
        LogIntrospectionSkip(
            skip_sequence,
            opcode,
            payload_length,
            L"payload_length_over_safety_ceiling");
        return;
    }
    if (!ShouldLogIntrospection(introspection_sequence)) {
        return;
    }

    const std::size_t prefix_length = static_cast<std::size_t>(
        (payload_length < kPrefixByteCap) ? payload_length : kPrefixByteCap);
    std::array<std::uint8_t, kPrefixByteCap> prefix = {};
    if (!TryCopyPrefixBytes(payload, prefix_length, prefix.data())) {
        const std::uint64_t skip_sequence = g_introspection_skip_count.fetch_add(1) + 1;
        LogIntrospectionSkip(skip_sequence, opcode, payload_length, L"payload_read_fault");
        return;
    }
    const std::wstring prefix_hex = FormatPrefixHex(prefix.data(), prefix_length);

    std::wstringstream introspection_message;
    introspection_message
        << L"PacketObserverRecvIntrospection"
        << L" seq=" << introspection_sequence
        << L" opcode=" << opcode
        << L" opcode_hex=" << Hex32(opcode)
        << L" payload_length=" << payload_length
        << L" prefix_len=" << prefix_length
        << L" prefix_hex=\"" << prefix_hex << L"\"";
    monomyth::logger::Log(introspection_message.str());
}

void Shutdown() noexcept {
    const State previous = g_state.exchange(State::kShutdown);
    if (previous == State::kUnavailable || previous == State::kShutdown) {
        return;
    }

    std::wstringstream message;
    message << L"PacketObserver state=shutdown observed_receive_count="
            << g_observed_count.load()
            << L" introspection_match_count=" << g_introspection_match_count.load()
            << L" introspection_skip_count=" << g_introspection_skip_count.load();
    monomyth::logger::Log(message.str());
}

State GetState() noexcept {
    return g_state.load();
}

}  // namespace monomyth::packet_observer
