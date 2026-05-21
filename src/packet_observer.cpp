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
#include "opcode_reference.h"
#include "server_auth_stats_observer.h"

// PacketObserver remains non-mutating. The only opcode-specific payload decode
// is the read-only OP_ServerAuthStats class-bitmask capture; optional generic
// introspection still requires both dev opt-ins and reads only a small prefix.

namespace monomyth::packet_observer {
namespace {

constexpr std::uint64_t kFirstPacketLogLimit = 50;
constexpr std::uint64_t kPacketLogSampleInterval = 500;
constexpr std::uint64_t kAuthStatsBootstrapPacketLogLimit = 250;
constexpr std::uint64_t kAuthStatsMissingWarningSequence = 250;
constexpr std::uint64_t kFirstIntrospectionLogLimit = 10;
constexpr std::uint64_t kIntrospectionLogSampleInterval = 1000;
constexpr std::uint64_t kWhoAllResponseLogLimit = 8;
constexpr std::uint32_t kWhoAllClassDisplayCorrelationBudget = 48;
constexpr std::uint32_t kWhoAllResponseOpcode = 0x578c;
constexpr std::uint32_t kWhoAllResponseHeaderSize = 64;
constexpr std::uint32_t kWhoAllResponseEntryLogLimit = 6;
constexpr std::uint32_t kWhoAllResponseFixedBlockBytes = 28;
constexpr std::uint32_t kPayloadSafetyCeiling = 4096;
constexpr std::uint32_t kAuthStatsCandidatePayloadCeiling = 512;
constexpr std::uint64_t kAuthStatsCandidateLogLimit = 16;
constexpr std::size_t kPrefixByteCap = 16;
constexpr std::uint32_t kServerAuthStatsOpcode = 0x1338;
constexpr std::uint32_t kMoveItemOpcode = 0x32ee;
constexpr std::uint32_t kMoveItemReceiveFocusBudget = 12;
constexpr std::array<std::uint32_t, 1> kDefaultAllowlist = {
    0x7dfc,
};

std::atomic<State> g_state = State::kUnavailable;
std::atomic<std::uint64_t> g_observed_count = 0;
std::atomic<std::uint64_t> g_observed_send_count = 0;
std::atomic<bool> g_full_packet_trace_enabled = false;
std::atomic<bool> g_introspection_enabled = false;
std::atomic<std::uint64_t> g_introspection_match_count = 0;
std::atomic<std::uint64_t> g_introspection_skip_count = 0;
std::atomic<std::uint32_t> g_move_item_receive_focus_remaining = 0;
std::atomic<std::uint64_t> g_move_item_receive_focus_activation = 0;
std::atomic<std::uint64_t> g_server_auth_stats_exact_match_count = 0;
std::atomic<std::uint64_t> g_server_auth_stats_candidate_count = 0;
std::atomic<bool> g_server_auth_stats_missing_warning_logged = false;
std::atomic<std::uint64_t> g_who_all_response_count = 0;
std::atomic<std::uint32_t> g_who_all_class_display_correlation_remaining = 0;
std::atomic<std::uint64_t> g_who_all_class_display_correlation_activation = 0;
std::atomic<std::uint64_t> g_who_all_class_display_correlation_receive_sequence = 0;
std::atomic<std::uint64_t> g_who_all_class_display_correlation_response_index = 0;
std::vector<std::uint32_t> g_introspection_allowlist;

struct IntrospectionAllowlistConfig {
    std::vector<std::uint32_t> opcodes;
    std::vector<std::wstring> invalid_tokens;
    bool uses_default = true;
};

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
    if (g_full_packet_trace_enabled.load()) {
        return true;
    }
    return sequence <= kFirstPacketLogLimit ||
        (sequence % kPacketLogSampleInterval) == 0;
}

bool ShouldLogIntrospection(std::uint64_t sequence) noexcept {
    return sequence <= kFirstIntrospectionLogLimit ||
        (sequence % kIntrospectionLogSampleInterval) == 0;
}

bool TryConsumeMoveItemReceiveFocus(
    std::uint32_t* remaining_before,
    std::uint32_t* remaining_after,
    std::uint64_t* activation) noexcept {
    if (remaining_before == nullptr || remaining_after == nullptr || activation == nullptr) {
        return false;
    }

    std::uint32_t current = g_move_item_receive_focus_remaining.load();
    while (current != 0) {
        if (g_move_item_receive_focus_remaining.compare_exchange_weak(current, current - 1)) {
            *remaining_before = current;
            *remaining_after = current - 1;
            *activation = g_move_item_receive_focus_activation.load();
            return true;
        }
    }

    return false;
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
    if (end != nullptr && *end == L'\0' &&
        parsed <= std::numeric_limits<std::uint32_t>::max()) {
        *opcode = static_cast<std::uint32_t>(parsed);
        return true;
    }

    return monomyth::opcode_reference::TryLookupRof2OpcodeValue(trimmed, opcode);
}

IntrospectionAllowlistConfig LoadIntrospectionAllowlist() {
    IntrospectionAllowlistConfig config;
    wchar_t value[256] = {};
    constexpr DWORD kValueCapacity = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
    const DWORD length = GetEnvironmentVariableW(
        L"MONOMYTH_RECV_INTROSPECT_OPCODES",
        value,
        kValueCapacity);
    if (length == 0 || length >= kValueCapacity) {
        config.opcodes.assign(kDefaultAllowlist.begin(), kDefaultAllowlist.end());
        return config;
    }

    config.uses_default = false;
    std::wstring_view remaining(value, length);
    while (!remaining.empty()) {
        const std::size_t separator = remaining.find(L',');
        const std::wstring_view token = remaining.substr(0, separator);
        std::uint32_t opcode = 0;
        if (ParseOpcodeToken(token, &opcode)) {
            config.opcodes.push_back(opcode);
        } else {
            const std::wstring trimmed = TrimWhitespace(token);
            if (!trimmed.empty()) {
                config.invalid_tokens.push_back(trimmed);
            }
        }

        if (separator == std::wstring_view::npos) {
            break;
        }
        remaining.remove_prefix(separator + 1);
    }

    std::sort(config.opcodes.begin(), config.opcodes.end());
    config.opcodes.erase(
        std::unique(config.opcodes.begin(), config.opcodes.end()),
        config.opcodes.end());
    return config;
}

bool IsIntrospectionAllowlisted(std::uint32_t opcode) noexcept {
    for (const std::uint32_t candidate : g_introspection_allowlist) {
        if (candidate == opcode) {
            return true;
        }
    }
    return false;
}

bool IsServerAuthStatsOpcode(std::uint32_t opcode) noexcept {
    std::uint32_t resolved = 0;
    return opcode == kServerAuthStatsOpcode &&
        monomyth::opcode_reference::TryLookupRof2OpcodeValue(L"OP_ServerAuthStats", &resolved) &&
        resolved == kServerAuthStatsOpcode;
}

bool IsWhoAllResponseOpcode(std::uint32_t opcode) noexcept {
    std::uint32_t resolved = 0;
    return opcode == kWhoAllResponseOpcode &&
        monomyth::opcode_reference::TryLookupRof2OpcodeValue(L"OP_WhoAllResponse", &resolved) &&
        resolved == kWhoAllResponseOpcode;
}

std::wstring BuildAllowlistSummary() {
    std::wstringstream stream;
    for (std::size_t i = 0; i < g_introspection_allowlist.size(); ++i) {
        if (i != 0) {
            stream << L",";
        }
        const std::uint32_t opcode = g_introspection_allowlist[i];
        stream << Hex32(opcode);
        const std::wstring_view opcode_name = monomyth::opcode_reference::LookupRof2OpcodeName(opcode);
        if (opcode_name != L"unknown") {
            stream << L"(" << opcode_name << L")";
        }
    }
    return stream.str();
}

std::wstring JoinTokens(const std::vector<std::wstring>& tokens) {
    std::wstringstream stream;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
            stream << L",";
        }
        stream << tokens[i];
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
    const std::wstring_view opcode_name = monomyth::opcode_reference::LookupRof2OpcodeName(opcode);
    message
        << L"PacketObserverRecvIntrospectionSkip"
        << L" seq=" << sequence
        << L" opcode=" << opcode
        << L" opcode_hex=" << Hex32(opcode)
        << L" opcode_name=" << opcode_name
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

void MaybeLogServerAuthStatsCandidate(
    std::uint64_t sequence,
    std::uint32_t opcode,
    std::uint32_t payload_length,
    const void* payload) {
    if (opcode == kServerAuthStatsOpcode || payload == nullptr || payload_length < 16 ||
        payload_length > kAuthStatsCandidatePayloadCeiling) {
        return;
    }

    const monomyth::server_auth_stats::ParseResult candidate =
        monomyth::server_auth_stats::ParsePayload(payload, payload_length);
    if (!candidate.valid ||
        (!candidate.has_classes_bitmask && !candidate.invalid_classes_bitmask)) {
        return;
    }

    const std::uint64_t candidate_index = g_server_auth_stats_candidate_count.fetch_add(1) + 1;
    if (candidate_index > kAuthStatsCandidateLogLimit) {
        return;
    }

    std::wstringstream message;
    const std::wstring_view opcode_name = monomyth::opcode_reference::LookupRof2OpcodeName(opcode);
    message
        << L"PacketObserverServerAuthStatsCandidate"
        << L" seq=" << sequence
        << L" candidate_index=" << candidate_index
        << L" opcode=" << opcode
        << L" opcode_hex=" << Hex32(opcode)
        << L" opcode_name=" << opcode_name
        << L" payload_length=" << payload_length
        << L" count=" << candidate.count
        << L" has_statClassesBitmask=" << (candidate.has_classes_bitmask ? L"true" : L"false");
    if (candidate.has_classes_bitmask) {
        message << L" statClassesBitmask=" << Hex32(candidate.classes_bitmask);
    }
    if (candidate.invalid_classes_bitmask) {
        message << L" invalid_statClassesBitmask=true";
    }
    message << L" reason=\"payload_matches_server_auth_stats_shape_but_opcode_differs\"";
    monomyth::logger::Log(message.str());
}

void MaybeLogMissingServerAuthStatsWarning(std::uint64_t sequence) {
    if (sequence < kAuthStatsMissingWarningSequence ||
        g_server_auth_stats_missing_warning_logged.exchange(true)) {
        return;
    }

    const monomyth::server_auth_stats::Snapshot snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    if (snapshot.has_classes_bitmask ||
        g_server_auth_stats_exact_match_count.load() != 0) {
        return;
    }

    std::wstringstream message;
    message
        << L"PacketObserverServerAuthStatsMissing"
        << L" seq=" << sequence
        << L" expected_opcode=" << kServerAuthStatsOpcode
        << L" expected_opcode_hex=" << Hex32(kServerAuthStatsOpcode)
        << L" exact_match_count=" << g_server_auth_stats_exact_match_count.load()
        << L" candidate_count=" << g_server_auth_stats_candidate_count.load()
        << L" reason=\"no_authoritative_server_auth_stats_observed_yet; ui_and_multiclass_fallbacks_will_remain_single_class\"";
    monomyth::logger::Log(message.str());
}

void ArmWhoAllClassDisplayCorrelation(
    std::uint64_t receive_sequence,
    std::uint64_t response_index) noexcept {
    const std::uint64_t activation =
        g_who_all_class_display_correlation_activation.fetch_add(1) + 1;
    g_who_all_class_display_correlation_receive_sequence.store(receive_sequence);
    g_who_all_class_display_correlation_response_index.store(response_index);
    g_who_all_class_display_correlation_remaining.store(
        kWhoAllClassDisplayCorrelationBudget);

    std::wstringstream message;
    message
        << L"PacketObserverWhoAllCorrelationWindow"
        << L" activation=" << activation
        << L" receive_sequence=" << receive_sequence
        << L" response_index=" << response_index
        << L" budget=" << kWhoAllClassDisplayCorrelationBudget;
    monomyth::logger::Log(message.str());
}

bool TryReadU32(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::uint32_t* value_out) noexcept {
    if (payload == nullptr || value_out == nullptr ||
        offset > payload_length || (payload_length - offset) < sizeof(std::uint32_t)) {
        return false;
    }

    std::uint32_t value = 0;
    std::memcpy(&value, payload + offset, sizeof(value));
    *value_out = value;
    return true;
}

bool TryReadCString(
    const std::uint8_t* payload,
    std::uint32_t payload_length,
    std::uint32_t offset,
    std::string* value_out,
    std::uint32_t* next_offset_out) noexcept {
    if (payload == nullptr || value_out == nullptr || next_offset_out == nullptr ||
        offset >= payload_length) {
        return false;
    }

    std::uint32_t cursor = offset;
    while (cursor < payload_length && payload[cursor] != 0) {
        ++cursor;
    }
    if (cursor >= payload_length) {
        return false;
    }

    value_out->assign(
        reinterpret_cast<const char*>(payload + offset),
        reinterpret_cast<const char*>(payload + cursor));
    *next_offset_out = cursor + 1;
    return true;
}

std::wstring WidenAsciiLossy(std::string_view text) {
    std::wstring widened;
    widened.reserve(text.size());
    for (const unsigned char ch : text) {
        widened.push_back(static_cast<wchar_t>(ch));
    }
    return widened;
}

void MaybeLogWhoAllResponse(
    std::uint64_t sequence,
    std::uint32_t opcode,
    std::uint32_t payload_length,
    const void* payload) {
    if (!IsWhoAllResponseOpcode(opcode) || payload == nullptr ||
        payload_length < kWhoAllResponseHeaderSize ||
        payload_length > kPayloadSafetyCeiling) {
        return;
    }

    const std::uint64_t response_index = g_who_all_response_count.fetch_add(1) + 1;
    ArmWhoAllClassDisplayCorrelation(sequence, response_index);
    if (response_index > kWhoAllResponseLogLimit) {
        return;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    std::uint32_t player_count = 0;
    std::uint32_t players_in_zone_string = 0;
    if (!TryReadU32(bytes, payload_length, 60, &player_count) ||
        !TryReadU32(bytes, payload_length, 40, &players_in_zone_string)) {
        monomyth::logger::Log(
            L"PacketObserverWhoAllResponse parse_status=header_read_failed");
        return;
    }

    std::wstringstream header_message;
    header_message
        << L"PacketObserverWhoAllResponse"
        << L" seq=" << sequence
        << L" response_index=" << response_index
        << L" payload_length=" << payload_length
        << L" player_count=" << player_count
        << L" players_in_zone_string=" << players_in_zone_string;
    monomyth::logger::Log(header_message.str());

    std::uint32_t cursor = kWhoAllResponseHeaderSize;
    const std::uint32_t entry_log_limit =
        (player_count < kWhoAllResponseEntryLogLimit) ? player_count : kWhoAllResponseEntryLogLimit;
    for (std::uint32_t entry_index = 0;
         entry_index < player_count && entry_index < entry_log_limit;
         ++entry_index) {
        std::uint32_t format_string = 0;
        std::uint32_t pid_string = 0;
        std::uint32_t rank_string = 0;
        std::uint32_t zone_string = 0;
        std::uint32_t zone = 0;
        std::uint32_t class_id = 0;
        std::uint32_t level = 0;
        std::uint32_t race = 0;
        std::uint32_t unknown100 = 0;
        std::string name;
        std::string guild;
        std::string account;

        if (!TryReadU32(bytes, payload_length, cursor, &format_string) ||
            !TryReadU32(bytes, payload_length, cursor + 4, &pid_string)) {
            monomyth::logger::Log(
                L"PacketObserverWhoAllResponseEntry parse_status=entry_prefix_read_failed");
            break;
        }
        cursor += 12;

        if (!TryReadCString(bytes, payload_length, cursor, &name, &cursor) ||
            !TryReadU32(bytes, payload_length, cursor, &rank_string)) {
            monomyth::logger::Log(
                L"PacketObserverWhoAllResponseEntry parse_status=name_or_rank_read_failed");
            break;
        }
        cursor += 4;

        if (!TryReadCString(bytes, payload_length, cursor, &guild, &cursor)) {
            monomyth::logger::Log(
                L"PacketObserverWhoAllResponseEntry parse_status=guild_read_failed");
            break;
        }

        if (cursor > payload_length || (payload_length - cursor) < kWhoAllResponseFixedBlockBytes) {
            monomyth::logger::Log(
                L"PacketObserverWhoAllResponseEntry parse_status=fixed_block_truncated");
            break;
        }

        if (!TryReadU32(bytes, payload_length, cursor + 8, &zone_string) ||
            !TryReadU32(bytes, payload_length, cursor + 12, &zone) ||
            !TryReadU32(bytes, payload_length, cursor + 16, &class_id) ||
            !TryReadU32(bytes, payload_length, cursor + 20, &level) ||
            !TryReadU32(bytes, payload_length, cursor + 24, &race)) {
            monomyth::logger::Log(
                L"PacketObserverWhoAllResponseEntry parse_status=fixed_fields_read_failed");
            break;
        }
        cursor += kWhoAllResponseFixedBlockBytes;

        if (!TryReadCString(bytes, payload_length, cursor, &account, &cursor) ||
            !TryReadU32(bytes, payload_length, cursor, &unknown100)) {
            monomyth::logger::Log(
                L"PacketObserverWhoAllResponseEntry parse_status=account_or_tail_read_failed");
            break;
        }
        cursor += 4;

        std::wstringstream entry_message;
        entry_message
            << L"PacketObserverWhoAllResponseEntry"
            << L" seq=" << sequence
            << L" response_index=" << response_index
            << L" entry_index=" << entry_index
            << L" format_string=" << format_string
            << L" pid_string=" << pid_string
            << L" rank_string=" << rank_string
            << L" zone_string=" << zone_string
            << L" zone=" << zone
            << L" class_id=" << class_id
            << L" level=" << level
            << L" race=" << race
            << L" unknown100=" << unknown100
            << L" name=\"" << WidenAsciiLossy(name) << L"\""
            << L" guild=\"" << WidenAsciiLossy(guild) << L"\""
            << L" account=\"" << WidenAsciiLossy(account) << L"\"";
        monomyth::logger::Log(entry_message.str());
    }
}

}  // namespace

State Initialize(const monomyth::runtime::Manifest& manifest) noexcept {
    const State current = g_state.load();
    if (current == State::kInitialized) {
        return current;
    }

    if (!manifest.packet_hooks_allowed && !manifest.memorize_send_trace_allowed) {
        g_state.store(State::kDisabledByCapability);
        std::wstring message =
            L"PacketObserver state=disabled_by_capability receive_packet_hooks_allowed=false memorize_send_trace_allowed=false";
        if (!manifest.packet_hooks_reason.empty()) {
            message += L" receive_reason=\"";
            message += manifest.packet_hooks_reason;
            message += L"\"";
        }
        if (!manifest.memorize_send_trace_reason.empty()) {
            message += L" send_reason=\"";
            message += manifest.memorize_send_trace_reason;
            message += L"\"";
        }
        monomyth::logger::Log(message);
        return g_state.load();
    }

    g_observed_count.store(0);
    g_observed_send_count.store(0);
    g_full_packet_trace_enabled.store(manifest.full_packet_trace_allowed);
    g_introspection_match_count.store(0);
    g_introspection_skip_count.store(0);
    g_introspection_enabled.store(manifest.receive_introspection_allowed);
    g_move_item_receive_focus_remaining.store(0);
    g_move_item_receive_focus_activation.store(0);
    g_server_auth_stats_exact_match_count.store(0);
    g_server_auth_stats_candidate_count.store(0);
    g_server_auth_stats_missing_warning_logged.store(false);
    g_who_all_response_count.store(0);
    g_who_all_class_display_correlation_remaining.store(0);
    g_who_all_class_display_correlation_activation.store(0);
    g_who_all_class_display_correlation_receive_sequence.store(0);
    g_who_all_class_display_correlation_response_index.store(0);
    const IntrospectionAllowlistConfig allowlist_config = LoadIntrospectionAllowlist();
    g_introspection_allowlist = allowlist_config.opcodes;
    g_state.store(State::kInitialized);

    if (!allowlist_config.invalid_tokens.empty()) {
        std::wstring invalid_message = L"PacketObserver recv_introspection_invalid_tokens=\"";
        invalid_message += JoinTokens(allowlist_config.invalid_tokens);
        invalid_message += L"\"";
        monomyth::logger::Log(invalid_message);
    }
    if (!allowlist_config.uses_default && g_introspection_allowlist.empty()) {
        monomyth::logger::Log(
            L"PacketObserver recv_introspection_config_status=no_valid_opcodes");
    }

    std::wstring message = L"PacketObserver state=initialized recv_log_policy=";
    message += manifest.full_packet_trace_allowed
        ? L"all_packets"
        : L"first_50_then_every_500";
    message += L" send_log_policy=";
    message += manifest.full_packet_trace_allowed
        ? L"all_packets"
        : L"first_50_then_every_500";
    message += L" recv_metadata=";
    message += manifest.packet_hooks_allowed ? L"true" : L"false";
    message += L" memorize_send_trace=";
    message += manifest.memorize_send_trace_allowed ? L"true" : L"false";
    message += L" full_packet_trace=";
    message += manifest.full_packet_trace_allowed ? L"true" : L"false";
    message += L" move_item_recv_focus_budget=";
    message += std::to_wstring(kMoveItemReceiveFocusBudget);
    if (manifest.receive_introspection_allowed) {
        message += L" recv_introspection=true recv_introspection_prefix_cap=16";
        message += L" recv_introspection_safety_ceiling=";
        message += std::to_wstring(kPayloadSafetyCeiling);
        message += L" recv_introspection_log_policy=first_10_then_every_1000";
        message += L" recv_introspection_scope=allowlisted_only";
        message += L" recv_introspection_allowlist_source=";
        message += allowlist_config.uses_default ? L"default" : L"configured";
        message += L" recv_introspection_allowlist=";
        if (g_introspection_allowlist.empty()) {
            message += L"empty";
        } else {
            message += BuildAllowlistSummary();
        }
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
    std::uint32_t move_item_focus_remaining_before = 0;
    std::uint32_t move_item_focus_remaining_after = 0;
    std::uint64_t move_item_focus_activation = 0;
    const bool move_item_focus = TryConsumeMoveItemReceiveFocus(
        &move_item_focus_remaining_before,
        &move_item_focus_remaining_after,
        &move_item_focus_activation);
    const monomyth::server_auth_stats::Snapshot server_auth_snapshot =
        monomyth::server_auth_stats::GetSnapshot();
    const bool auth_stats_bootstrap_logging =
        !server_auth_snapshot.has_classes_bitmask &&
        sequence <= kAuthStatsBootstrapPacketLogLimit;
    if (move_item_focus || auth_stats_bootstrap_logging || ShouldLogPacket(sequence)) {
        std::wstringstream message;
        const std::wstring_view opcode_name = monomyth::opcode_reference::LookupRof2OpcodeName(opcode);
        message
            << L"PacketObserverRecv"
            << L" seq=" << sequence
            << L" opcode=" << opcode
            << L" opcode_hex=" << Hex32(opcode)
            << L" opcode_name=" << opcode_name
            << L" payload_length=" << payload_length
            << L" source_context=" << HexPtr(source_context);
        if (move_item_focus) {
            message
                << L" move_item_focus=true"
                << L" move_item_focus_activation=" << move_item_focus_activation
                << L" move_item_focus_remaining_before=" << move_item_focus_remaining_before
                << L" move_item_focus_remaining_after=" << move_item_focus_remaining_after;
        }
        monomyth::logger::Log(message.str());
    }

    if (IsServerAuthStatsOpcode(opcode)) {
        g_server_auth_stats_exact_match_count.fetch_add(1);
        monomyth::server_auth_stats::ObserveReceivePayload(payload, payload_length);
    } else {
        MaybeLogServerAuthStatsCandidate(sequence, opcode, payload_length, payload);
    }
    MaybeLogWhoAllResponse(sequence, opcode, payload_length, payload);

    MaybeLogMissingServerAuthStatsWarning(sequence);

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
    const std::wstring_view opcode_name = monomyth::opcode_reference::LookupRof2OpcodeName(opcode);
    introspection_message
        << L"PacketObserverRecvIntrospection"
        << L" seq=" << introspection_sequence
        << L" opcode=" << opcode
        << L" opcode_hex=" << Hex32(opcode)
        << L" opcode_name=" << opcode_name
        << L" payload_length=" << payload_length
        << L" prefix_len=" << prefix_length
        << L" prefix_hex=\"" << prefix_hex << L"\"";
    monomyth::logger::Log(introspection_message.str());
}

void ObserveSendMetadata(
    std::uintptr_t wrapper_address,
    std::uintptr_t source_context,
    std::uintptr_t packet_pointer,
    std::uint32_t total_length,
    bool opcode_decoded,
    std::uint32_t opcode,
    std::uint32_t payload_length,
    const wchar_t* decode_status,
    const wchar_t* not_decoded_reason,
    bool original_result,
    bool original_result_available,
    std::uint32_t correlation_id) noexcept {
    if (g_state.load() != State::kInitialized) {
        return;
    }

    const std::uint64_t sequence = g_observed_send_count.fetch_add(1) + 1;
    bool move_item_focus_armed = false;
    std::uint64_t move_item_focus_activation = 0;
    if (opcode_decoded && opcode == kMoveItemOpcode &&
        original_result_available && original_result) {
        move_item_focus_activation = g_move_item_receive_focus_activation.fetch_add(1) + 1;
        g_move_item_receive_focus_remaining.store(kMoveItemReceiveFocusBudget);
        move_item_focus_armed = true;
    }
    if (!move_item_focus_armed && !ShouldLogPacket(sequence)) {
        return;
    }

    std::wstringstream message;
    const std::wstring_view opcode_name = opcode_decoded
        ? monomyth::opcode_reference::LookupRof2OpcodeName(opcode)
        : std::wstring_view(L"not_decoded");
    message
        << L"PacketObserverSend"
        << L" seq=" << sequence
        << L" target=MemorizeSendPacketWrapper"
        << L" wrapper_address=" << HexPtr(wrapper_address)
        << L" source_context=" << HexPtr(source_context)
        << L" packet_pointer=" << HexPtr(packet_pointer)
        << L" total_length=" << total_length
        << L" decode_status="
        << ((decode_status == nullptr || decode_status[0] == L'\0') ? L"unknown" : decode_status);
    if (opcode_decoded) {
        message << L" opcode=" << opcode
                << L" opcode_hex=" << Hex32(opcode);
    } else {
        message << L" opcode=unknown opcode_hex=not_decoded";
    }
    message
        << L" opcode_name=" << opcode_name
        << L" payload_length=" << payload_length;
    if (!opcode_decoded) {
        message << L" not_decoded_reason="
                << ((not_decoded_reason == nullptr || not_decoded_reason[0] == L'\0')
                        ? L"unknown"
                        : not_decoded_reason);
    } else if (opcode_name == L"OP_MemorizeSpell") {
        message << L" memorize_opcode_match=true";
    }
    if (original_result_available) {
        message << L" original_result=" << (original_result ? L"true" : L"false");
    }
    if (correlation_id != 0) {
        message << L" memorize_send_correlation=" << correlation_id;
    }
    if (move_item_focus_armed) {
        message << L" move_item_focus_armed=true"
                << L" move_item_focus_activation=" << move_item_focus_activation
                << L" move_item_focus_budget=" << kMoveItemReceiveFocusBudget;
    }
    monomyth::logger::Log(message.str());
}

bool TryConsumeWhoAllClassDisplayCorrelation(
    WhoAllClassDisplayCorrelationWindow* window) noexcept {
    if (window == nullptr) {
        return false;
    }

    *window = {};

    std::uint32_t current = g_who_all_class_display_correlation_remaining.load();
    while (current != 0) {
        if (g_who_all_class_display_correlation_remaining.compare_exchange_weak(
                current,
                current - 1)) {
            window->active = true;
            window->activation = g_who_all_class_display_correlation_activation.load();
            window->receive_sequence = g_who_all_class_display_correlation_receive_sequence.load();
            window->response_index = g_who_all_class_display_correlation_response_index.load();
            window->remaining_before = current;
            window->remaining_after = current - 1;
            return true;
        }
    }

    return false;
}

void Shutdown() noexcept {
    const State previous = g_state.exchange(State::kShutdown);
    if (previous == State::kUnavailable || previous == State::kShutdown) {
        return;
    }

    std::wstringstream message;
    message << L"PacketObserver state=shutdown observed_receive_count="
            << g_observed_count.load()
            << L" observed_send_count=" << g_observed_send_count.load()
            << L" introspection_match_count=" << g_introspection_match_count.load()
            << L" introspection_skip_count=" << g_introspection_skip_count.load()
            << L" server_auth_stats_exact_match_count=" << g_server_auth_stats_exact_match_count.load()
            << L" server_auth_stats_candidate_count=" << g_server_auth_stats_candidate_count.load()
            << L" who_all_response_count=" << g_who_all_response_count.load()
            << L" who_all_class_display_correlation_remaining="
            << g_who_all_class_display_correlation_remaining.load()
            << L" who_all_class_display_correlation_activation="
            << g_who_all_class_display_correlation_activation.load()
            << L" move_item_focus_remaining=" << g_move_item_receive_focus_remaining.load()
            << L" move_item_focus_activation=" << g_move_item_receive_focus_activation.load();
    g_full_packet_trace_enabled.store(false);
    g_move_item_receive_focus_remaining.store(0);
    g_who_all_class_display_correlation_remaining.store(0);
    monomyth::logger::Log(message.str());
}

State GetState() noexcept {
    return g_state.load();
}

}  // namespace monomyth::packet_observer
