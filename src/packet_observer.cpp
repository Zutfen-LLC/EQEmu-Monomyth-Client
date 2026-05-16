#include "packet_observer.h"

#include <string>

#include "logger.h"

// PacketObserver scaffold — inert, no hooks installed, no packet data accessed.
// TODO(future-slice): Identify safe ROF2 receive-packet hook candidates.
// TODO(future-slice): Implement receive-only observation behind packet_hooks_allowed.

namespace monomyth::packet_observer {
namespace {

State g_state = State::kUnavailable;

}  // namespace

State Initialize(const monomyth::runtime::Manifest& manifest) noexcept {
    if (g_state == State::kInitialized) {
        return g_state;
    }

    if (!manifest.packet_hooks_allowed) {
        g_state = State::kDisabledByCapability;
        std::wstring message =
            L"PacketObserver scaffold state=disabled_by_capability packet_hooks_allowed=false receive_dispatch_discovery=";
        message += monomyth::receive_dispatch_discovery::StateName(
            manifest.receive_dispatch_discovery_state);
        monomyth::logger::Log(message);
        return g_state;
    }

    // packet_hooks_allowed is currently always false; this path is unreachable
    // in this slice and is intentionally left as a no-op placeholder.
    g_state = State::kInitialized;
    monomyth::logger::Log(L"PacketObserver scaffold state=initialized (no hooks installed)");
    return g_state;
}

void Shutdown() noexcept {
    if (g_state == State::kUnavailable || g_state == State::kShutdown) {
        return;
    }

    g_state = State::kShutdown;
    monomyth::logger::Log(L"PacketObserver scaffold state=shutdown");
}

State GetState() noexcept {
    return g_state;
}

}  // namespace monomyth::packet_observer
