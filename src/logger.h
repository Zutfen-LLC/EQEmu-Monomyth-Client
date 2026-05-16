#pragma once

#include <string_view>

namespace monomyth::logger {

void Log(std::wstring_view message) noexcept;
void Log(const wchar_t* message) noexcept;
void Flush() noexcept;

}  // namespace monomyth::logger
