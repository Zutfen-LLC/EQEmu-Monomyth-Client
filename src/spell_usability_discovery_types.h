#pragma once

namespace monomyth::spell_usability_discovery {

enum class TargetState {
    kNotAttempted,
    kFoundUnvalidated,
    kValidated,
    kFailed,
};

enum class EvidenceSource {
    kNotAttempted,
    kRuntimeExport,
    kCleanroomRva,
    kWrapperValidation,
    kBytePattern,
    kUnavailable,
};

const wchar_t* TargetStateName(TargetState state) noexcept;
const wchar_t* EvidenceSourceName(EvidenceSource source) noexcept;

}  // namespace monomyth::spell_usability_discovery
