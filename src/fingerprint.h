#pragma once

#include <string>
#include <string_view>

namespace monomyth::fingerprint {

enum class Method {
    kUnavailable = 0,
    kVersionResource,
    kByteScan,
};

struct MarkerPresence {
    bool date_found = false;
    bool time_found = false;
};

struct Result {
    bool hooks_allowed = false;
    bool process_name_match = false;
    bool matched = false;
    bool version_strings_match = false;
    bool version_strings_checked = false;
    bool byte_scan_match = false;
    bool byte_scan_checked = false;
    bool file_hash_placeholder = false;
    bool text_hash_placeholder = false;
    bool prepatch_placeholder = false;
    Method method = Method::kUnavailable;
    std::wstring reason;
};

MarkerPresence FindKnownRof2Markers(std::string_view bytes) noexcept;
const wchar_t* MethodName(Method method) noexcept;

Result Evaluate() noexcept;

}  // namespace monomyth::fingerprint
