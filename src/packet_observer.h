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
void Shutdown() noexcept;
State GetState() noexcept;

}  // namespace monomyth::packet_observer
