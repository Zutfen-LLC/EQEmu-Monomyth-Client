#pragma once

#include <cstdint>
#include <string>

namespace monomyth::receive_dispatch_discovery {

enum class State {
    kUnavailable,
    kSkippedByCapability,
    kCandidateFound,
    kValidated,
    kFailed,
    kShutdown,
};

struct Result {
    State state = State::kUnavailable;
    bool validated = false;
    std::uintptr_t module_base = 0;
    std::uint32_t candidate_rva = 0;
    std::uintptr_t candidate_address = 0;
    std::wstring reason = L"not run";
};

void Initialize() noexcept;
Result Run(bool enhancement_discovery_allowed) noexcept;
void Shutdown() noexcept;
Result GetResult() noexcept;

const wchar_t* StateName(State state) noexcept;
void LogResult(const Result& result) noexcept;

}  // namespace monomyth::receive_dispatch_discovery
