#pragma once

#include <cstdint>

#include "runtime_capabilities.h"

namespace monomyth::packet_observer {

enum class State {
    kUnavailable,
    kDisabledByCapability,
    kDisabledByHookFailure,
    kInitialized,
    kShutdown,
};

State Initialize(const monomyth::runtime::Manifest& manifest) noexcept;
void DisableBecauseHookUnavailable(const wchar_t* reason) noexcept;
void ObserveReceiveMetadata(
    std::uint32_t opcode,
    std::uint32_t payload_length,
    const void* payload,
    std::uintptr_t source_context) noexcept;
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
    std::uint32_t correlation_id) noexcept;
void Shutdown() noexcept;
State GetState() noexcept;

}  // namespace monomyth::packet_observer
