# Contributing

This repository contains a build-tested initial library and example, not a
hardware-validated release.

## Licensing

New contributions must be offered under LGPL-3.0-or-later and must
be yours to contribute under those terms. Identify third-party sources,
licenses, and permissions in each contribution.

Upstream LGPL `node-simconnect` code may be adapted under its license. Preserve
copyright notices, identify the source revision, and document modifications.
Do not assume that an uncredited local file has no third-party obligations.
See [the provenance requirements](docs/provenance.md).

## Component structure

The repository root is an ESP-IDF component
with `CMakeLists.txt`, `idf_component.yml`, `include/`, and `src/`. Buildable
applications belong under `examples/`; packet tests belong under `tests/`.

Network initialization, Wi-Fi credentials, board pins, and aircraft-specific
logic belong in examples, not the transport component.

## Review expectations

- Use bounded allocations and explicit ownership.
- Preserve receive progress across timeout or close the session on framing loss.
- Continuously dispatch exceptions and lifecycle messages.
- Define concurrency, cancellation, callback lifetime, and reconnect behavior.
- Test protocol boundary cases with properly sourced fixtures.
- Never call a successful send “applied” without observing simulator state.
- Update support claims only with recorded build/device test results.
- Keep credentials, generated builds, and local configuration out of commits.

Run `python3 tools/check_docs.py` for documentation-only changes. Firmware changes
must additionally pass `python3 tests/test_client.py` and `bash build.sh`.
Document what was and was not verified on hardware. Never include real
`app_config.h` credentials or configured build artifacts in a commit.
