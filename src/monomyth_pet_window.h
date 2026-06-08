#pragma once

#include <cstdint>

namespace monomyth::multipet_window {

bool Initialize(std::uintptr_t module_base) noexcept;
void Shutdown() noexcept;
void TryAttachWindow(std::uintptr_t module_base) noexcept;

}  // namespace monomyth::multipet_window
