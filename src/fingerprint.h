#pragma once

#include <string>

namespace monomyth::fingerprint {

struct Result {
    bool hooks_allowed = false;
    bool process_name_match = false;
    bool version_strings_match = false;
    bool version_strings_checked = false;
    bool file_hash_placeholder = false;
    bool text_hash_placeholder = false;
    bool prepatch_placeholder = false;
    std::wstring reason;
};

Result Evaluate() noexcept;

}  // namespace monomyth::fingerprint
