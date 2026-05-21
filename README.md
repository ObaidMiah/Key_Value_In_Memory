# kvstore

A distributed key-value store in C++, built as a pedagogical project. The
distributed-systems logic — consistent hashing, leader-follower replication,
quorum reads, WAL durability — is the whole point and is written by hand,
not generated. This repo's job is to stay out of the way of that.

What exists today is scaffolding: a gRPC server/client with a placeholder
`Ping` RPC, a CMake + vcpkg build, sanitizer-enabled Debug builds,
GoogleTest wiring, a 3-node Docker compose, and a GitHub Actions workflow
that verifies it all on Linux.

## Layout

```
proto/                .proto files and the CMake rules that drive protoc
include/kvstore/      public headers
src/                  server + client binaries (and future KV/WAL code)
tests/                GoogleTest unit tests
.github/workflows/    CI: Debug + Release builds, ctest, Ping, 3-node Docker
Dockerfile            multi-stage Ubuntu 24.04 build
docker-compose.yml    3-node local cluster
vcpkg.json            dependency manifest (used by CI + Docker)
CLAUDE.md             instructions for Claude — what it should and shouldn't write
```

## Two build paths

The same `CMakeLists.txt` supports both.

### Local dev (macOS, recommended)

Uses Homebrew bottles — prebuilt binaries, no source compilation of the
heavy deps (gRPC, protobuf, abseil). The only thing that compiles locally
is your own code (~10 seconds).

```sh
brew install grpc protobuf googletest spdlog cli11 nlohmann-json

env -u VCPKG_ROOT cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/release --parallel

# Debug too, for ctest + sanitizers
env -u VCPKG_ROOT cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/debug --parallel
```

`env -u VCPKG_ROOT` suppresses the vcpkg toolchain auto-detect for just
this command, so CMake falls back to the Homebrew prefix.

### Linux / CI / Docker (vcpkg)

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg

cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
```

First configure compiles grpc + protobuf from source via vcpkg — 10–30 min
on a cold cache, much faster after.

## Run server + client

```sh
# terminal 1
./build/release/src/kvstore_server --port 50051 --node-id node-0

# terminal 2
./build/release/src/kvstore_client --target localhost:50051
# expect: Ping ok: node_id=node-0, server_time_ms=...
```

Run integration smoke tests against the **Release** binary on purpose —
ASan trips inside gRPC's internal `alloca` use on the Debug binary. ctest
still runs against Debug to catch real bugs in your own code.

## Tests

```sh
ctest --test-dir build/debug --output-on-failure
```

## Docker / 3-node cluster

```sh
docker compose up --build
```

Host ports `50051`, `50052`, `50053` route to nodes `node1`, `node2`,
`node3`. Inside the compose network they reach each other as
`node1:50051`, etc.

## CI

`.github/workflows/ci.yml` runs on every push and PR. Two jobs in parallel:

- **build_and_test** — vcpkg install → Debug build → ctest → Release build → server+client Ping
- **docker_smoke** — `docker compose build` → bring up 3 nodes → client pings each node

Green CI is the source of truth for "does this work on the Linux deploy
target." Local builds are for fast iteration.

## Known quirks

- **Apple Silicon Docker** defaults to `linux/arm64`. The Dockerfile lets
  vcpkg auto-detect the triplet — don't force `--triplet x64-linux`.
- **ASan + gRPC** crash at channel creation due to gRPC's `alloca` usage.
  Workaround: integration tests run against Release; ctest keeps ASan.
- **spdlog logger names are immutable.** To rename, build a new logger via
  `spdlog::stdout_color_mt(name)` and `set_default_logger(...)`.
- **vcpkg cli11 port** needs `pkg-config` on the host (`brew install pkg-config`).

## Notes

- vcpkg is in manifest mode; deps pinned in `vcpkg.json`. To lock the
  registry baseline for fully reproducible builds, run
  `$VCPKG_ROOT/vcpkg x-update-baseline --add-initial-baseline` once.
- Sanitizers are wired globally for Debug. Use `RelWithDebInfo` if you
  need to attach a debugger that conflicts with ASan.
- Generated proto output lives in `build/<config>/proto/`. The
  `kvstore_proto` target exposes it as a `PUBLIC` include dir, so
  consumers just `#include "kvstore.grpc.pb.h"`.
