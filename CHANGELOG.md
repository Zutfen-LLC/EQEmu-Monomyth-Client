# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project currently tracks changes under an `Unreleased` section until versioned releases begin.

## [Unreleased]

### Added

- Fail-closed receive dispatcher discovery scaffold for the validated ROF2 candidate at VA `0x004c3250` / RVA `0x000c3250`, with layered static structural checks and one startup log line.
- Runtime capability manifest fields for receive dispatcher discovery state, validated candidate RVA/address, and PacketObserver state reporting while keeping `packet_hooks_allowed=false`.
- GitHub Actions CI workflow for 32-bit Windows builds on pushes to `main` and pull requests.
- Build artifacts upload for `Debug` and `Release` `dinput8.dll` outputs.
- Repository changelog for tracking notable changes over time.

### Documentation

- README documentation for receive dispatcher discovery, fail-closed behavior, and the no-hooks/no-packet-data safety boundary.
- README note describing the Windows CI workflow and artifact output.

### Fixed

- CI lint and AI review checks now exclude generated Graphify output artifacts.
