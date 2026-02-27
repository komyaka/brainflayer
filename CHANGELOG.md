# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] — 2026-02-26

### Changed
- README.md fully rewritten in Russian (`ru`): comprehensive project description, secp256k1 pipeline diagram, dependency table, build instructions with submodule init, per-tool documentation (`brainflayer`, `hexln`, `hex2blf`, `blfchk`, `ecmtabgen`), bloom filter preparation guide, detailed usage examples for every supported input type (sha256, sha3, keccak, camp2, warp, bwio, bv2, rush, priv), parallel workload splitting (`-n K/N`), hex-encoded input (`-x`), precomputed EC table (`ecmtabgen` + `-m`), exact verification (`-f`), testing/benchmarking targets, performance tuning notes, complete flag reference, and licence notice

## [Previous Unreleased]

### Fixed
- Fixed newline handling (CRLF/LF/CR) across all tools: brainflayer, hexln, hex2blf, blfchk
- Fixed memory leaks in dictionary ingestion, bloom mmap, secp256k1 batch/precomp allocations
- Fixed hexln truncating last byte on inputs without trailing newline
- Fixed `normalize_line()` NULL pointer guard to prevent segfault when `line` is NULL

### Changed
- Default batch size reduced from 4096 to 1024 for better cache locality
- Help text now shows both default and max batch size
- OpenSSL 3.0 deprecation warnings suppressed with `-Wno-deprecated-declarations`

### Added
- `normalize_line()` shared helper in `hex.h` for consistent newline stripping
- `make test` target with normalization, hexln, brainflayer E2E, and bloom round-trip tests
- `make memcheck` target (requires valgrind)
- `make sanitize` target (AddressSanitizer build)
- `make bench` target with generated dictionary
- secp256k1 batch/precomp cleanup functions
- README: build instructions, usage examples, test/bench commands, benchmark results, performance comparison
- GitHub Actions CI workflow (`.github/workflows/ci.yml`)
