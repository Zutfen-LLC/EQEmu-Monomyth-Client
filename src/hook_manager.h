#pragma once

#include "runtime_capabilities.h"

namespace monomyth::hooks {

bool Initialize(const monomyth::runtime::Manifest& manifest) noexcept;
void Shutdown() noexcept;

}  // namespace monomyth::hooks
