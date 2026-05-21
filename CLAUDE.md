# CLAUDE.md

Instructions for Claude when working in this repo.

## What this project is

A distributed key-value store in C++, built as a pedagogical project. The user writes all distributed-systems logic themselves — that is the entire point. Claude is here for scaffolding and tooling only.

## Hard boundary: do not write these for the user

- Storage internals (hashmap, indexes, get/put/delete semantics, eviction)
- Write-ahead log (record format, fsync, recovery, segment rotation)
- Replication (leader-follower, log shipping, snapshots, consensus)
- Sharding / consistent hashing
- Quorum reads, conflict resolution, read-repair
- Tests *for* the above (the user decides what to test and how)

If asked to "implement X" where X is any of the above, point at the right file location and reflect the question back. Do not draft logic, even as a starting point.

## Do help with

- Build system: CMake, vcpkg, Homebrew, sanitizer flags, ASan/gRPC interactions
- gRPC/protobuf plumbing: adding RPCs to the `.proto`, regenerating stubs, wiring service registration
- File organization: where new files live, how to add them to `src/CMakeLists.txt` and `tests/CMakeLists.txt`
- Compile and link errors
- Dockerfile, docker-compose, CI workflow
- Diagnosing local vs CI build divergence

## Stack

- C++20, CMake ≥ 3.20, Ninja
- gRPC + protobuf (vcpkg in CI/Docker, Homebrew locally on macOS)
- spdlog (logging), CLI11 (flags), GoogleTest (tests), nlohmann-json (config)
- macOS Apple Silicon for dev, Ubuntu 24.04 for deploy target

## Build paths

**Local (macOS, fast, no fans):**
```sh
env -u VCPKG_ROOT cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/release --parallel
```

**Local Debug (ASan + UBSan, for ctest):**
```sh
env -u VCPKG_ROOT cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
```

**CI (Linux, vcpkg):** GitHub Actions runs `.github/workflows/ci.yml` on every push — verifies Debug build, ctest, Release build, server+client Ping, plus 3-node Docker compose. Treat green CI as authoritative for "does this work on the deploy target."

## Known gotchas

- `spdlog::logger` has no `set_name`. Construct a new logger with `spdlog::stdout_color_mt(name)` and `set_default_logger(...)` instead.
- ASan crashes inside gRPC's channel-creation (alloca redzone). Run integration smoke tests against Release; keep ASan on Debug for ctest only.
- Apple Silicon Docker defaults to `linux/arm64`. Don't force `--triplet x64-linux` in the Dockerfile — let vcpkg auto-detect.
- The user's laptop overheats on heavy compiles. Prefer Homebrew bottles or CI over local vcpkg builds. Never run parallel heavy compiles (e.g., local vcpkg + docker build simultaneously).

## When the user asks…

- "Where do I put X?" → answer with a file path and one-line rationale, nothing else.
- "What goes in this file?" → reflect the question; offer to set up the file/CMake plumbing, but do not draft the logic.
- "Add an RPC for X" → edit `proto/kvstore.proto` with the requested message/method signatures, regenerate, wire the empty handler stub returning `UNIMPLEMENTED`. Stop there.
- "My build is broken" → fix the build.
- "Help me think about X (replication, WAL, etc.)" → ask whether they want a Socratic conversation or a direct answer. Default to questions, not answers.
