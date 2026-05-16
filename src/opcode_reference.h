#pragma once

#include <cstdint>
#include <string_view>

namespace monomyth::opcode_reference {

std::wstring_view LookupRof2OpcodeName(std::uint32_t opcode) noexcept;

}  // namespace monomyth::opcode_reference
