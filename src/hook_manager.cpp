#include "hook_manager.h"

#include "logger.h"

namespace monomyth::hooks {
namespace {

bool g_initialized = false;

}  // namespace

bool Initialize(const monomyth::runtime::Manifest& manifest) noexcept {
    if (g_initialized) {
        return true;
    }

    g_initialized = true;
    monomyth::logger::Log(L"hook_manager: initialized (no active hooks)");
    if (manifest.heartbeat_allowed) {
        monomyth::logger::Log(L"hook_manager: heartbeat (guard passed, hook install point remains inert)");
    }
    return true;
}

void Shutdown() noexcept {
    if (!g_initialized) {
        return;
    }

    g_initialized = false;
    monomyth::logger::Log(L"hook_manager: shutdown");
}

}  // namespace monomyth::hooks
