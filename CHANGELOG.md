# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project currently tracks changes under an `Unreleased` section until versioned releases begin.

## [Unreleased]

### Changed

- Win32 MSVC builds now statically link the C/C++ runtime for `dinput8.dll` (`/MT` in `Release`, `/MTd` in `Debug`) so the client DLL no longer depends on `MSVCP140.dll` or `VCRUNTIME140.dll` at loader startup.
- Receive dispatcher discovery now resolves the validated ROF2 dispatcher candidate as runtime `module_base + 0x000c3250`, logs the runtime module base and resolved candidate address, and still fails closed if the resolved address falls outside the loaded image or structural validation does not match.
- Receive dispatcher discovery now validates the large ROF2 dispatcher with layered named checks: mandatory range, entry/compare-tree, unknown-message path, and feeder-call evidence remain fail-closed, while `ret 0x10` epilogue evidence is logged as bounded advisory diagnostics near the known epilogue RVA instead of being required in a small entry-adjacent scan window.

### Added

- Fail-closed ROF2 fingerprint byte-scan fallback for `eqgame.exe` that runs once at startup when version resources are unavailable or inconclusive and matches only when both known markers `May 10 2013` and `23:30:08` are present.
- Capability manifest fingerprint method reporting (`version_resource`, `byte_scan`, or `unavailable`) to make startup guard results grep-friendly.
- Fail-closed receive dispatcher discovery scaffold for the validated ROF2 candidate at VA `0x004c3250` / RVA `0x000c3250`, with layered static structural checks and one startup log line.
- Runtime capability manifest fields for receive dispatcher discovery state, validated candidate RVA/address, and PacketObserver state reporting while keeping `packet_hooks_allowed=false`.
- Dev-only receive dispatcher hook gated by `MONOMYTH_ENABLE_PACKET_HOOKS=1`, ROF2 fingerprint validation, and receive dispatcher discovery validation.
- Metadata-only `PacketObserverRecv` logging for opcode/message id, payload length, and source/context pointer, capped to the first 50 packets and every 500th packet after that.
- Dev-only bounded receive payload-prefix introspection gated separately by `MONOMYTH_ENABLE_RECV_INTROSPECTION=1`, layered on top of `MONOMYTH_ENABLE_PACKET_HOOKS=1`, limited to an allowlisted opcode set, capped at a 16-byte hex prefix, and rate-limited independently from metadata logging.
- GitHub Actions CI workflow for 32-bit Windows builds on pushes to `main` and pull requests.
- Build artifacts upload for `Debug` and `Release` `dinput8.dll` outputs.
- Rolling GitHub prerelease publishing on `main` so the newest `dinput8.dll` and `dinput8-Debug.dll` are always downloadable from Releases.
- Repository changelog for tracking notable changes over time.

### Documentation

- README documentation for the ROF2 fingerprint byte-scan fallback, required dual-marker match, and `fingerprint_method` capability logging.
- README documentation for receive dispatcher discovery, layered fail-closed validation, advisory epilogue handling for the large dispatcher, and the no-hooks/no-packet-data safety boundary.
- README documentation for the unsafe local packet-hook opt-in, metadata-only receive observation, no payload access, no send interception, and rate-limited logging.
- README documentation for the separate receive-introspection opt-in, bounded 16-byte prefix logging, default opcode allowlist, and fail-closed payload safety checks.
- README note describing the Windows CI workflow, artifact output, and rolling prerelease downloads.
- README build and troubleshooting documentation for static MSVC runtime linkage, `0xc0000142` startup failures, and `dumpbin` dependency verification.

### Fixed

- CI lint and AI review checks now exclude generated Graphify output artifacts.
