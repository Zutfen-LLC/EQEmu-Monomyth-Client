#include "packet_observer.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

#include "logger.h"

// PacketObserver is metadata-only. It never reads, copies, decodes, logs, or
// mutates receive payload bytes.

namespace monomyth::packet_observer {
namespace {

constexpr std::uint64_t kFirstPacketLogLimit = 50;
constexpr std::uint64_t kPacketLogSampleInterval = 500;

std::atomic<State> g_state = State::kUnavailable;
std::atomic<std::uint64_t> g_observed_count = 0;

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
    g_state.store(State::kInitialized);
    monomyth::logger::Log(
        L"PacketObserver state=initialized mode=metadata_only log_policy=first_50_then_every_500");
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
    std::uintptr_t source_context) noexcept {
    if (g_state.load() != State::kInitialized) {
        return;
    }

    const std::uint64_t sequence = g_observed_count.fetch_add(1) + 1;
    if (!ShouldLogPacket(sequence)) {
        return;
    }

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

void Shutdown() noexcept {
    const State previous = g_state.exchange(State::kShutdown);
    if (previous == State::kUnavailable || previous == State::kShutdown) {
        return;
    }

    std::wstringstream message;
    message << L"PacketObserver state=shutdown observed_receive_count="
            << g_observed_count.load();
    monomyth::logger::Log(message.str());
}

State GetState() noexcept {
    return g_state.load();
}

}  // namespace monomyth::packet_observer
