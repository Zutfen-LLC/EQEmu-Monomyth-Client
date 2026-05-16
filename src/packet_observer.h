#pragma once

#include "runtime_capabilities.h"

namespace monomyth::packet_observer {

enum class State {
    kUnavailable,
    kDisabledByCapability,
    kInitialized,
    kShutdown,
};

State Initialize(const monomyth::runtime::Manifest& manifest) noexcept;
void Shutdown() noexcept;
State GetState() noexcept;

}  // namespace monomyth::packet_observer
