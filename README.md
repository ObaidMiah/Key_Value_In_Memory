# kvstore

A distributed key-value store, written in C++ as a learning project for
distributed-systems internals (consistent hashing, leader-follower
replication, quorum reads, WAL durability).

This repository currently contains only scaffolding: a gRPC server/client
pair with a placeholder `Ping` RPC, a CMake/vcpkg build, sanitizer-enabled
Debug builds, GoogleTest wiring, and Docker plumbing for a multi-node local
cluster.

## Layout

```
proto/                .proto files and the CMake rules that drive protoc
include/kvstore/      public headers (intentionally empty)
src/                  server + client binaries
tests/                GoogleTest unit tests
Dockerfile            multi-stage Ubuntu 24.04 build
docker-compose.yml    3-node local cluster
vcpkg.json            dependency manifest
```

## Prerequisites

- Linux (Ubuntu 24.04 recommended)
- CMake ≥ 3.20, Ninja, a C++20 compiler (gcc ≥ 13 or clang ≥ 16)
- `git`, `curl`, `zip`, `unzip`, `tar`, `pkg-config`, `autoconf`, `automake`, `libtool`

## 1. Bootstrap vcpkg

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg
```

Persist `export VCPKG_ROOT=~/vcpkg` in your shell rc. The top-level
`CMakeLists.txt` picks it up automatically to locate the toolchain file.

## 2. Configure

```sh
cmake -S . -B build/debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

The first configure triggers vcpkg to fetch and build dependencies — grpc +
protobuf are big, expect 10–30 minutes on a cold cache.

## 3. Build

```sh
cmake --build build/debug   --parallel
cmake --build build/release --parallel
```

## 4. Run server + client

Terminal 1:

```sh
./build/debug/src/kvstore_server --port 50051 --node-id node-0
```

Terminal 2:

```sh
./build/debug/src/kvstore_client --target localhost:50051
```

Expected output on the client:

```
[...] [info] [client-cli] Ping ok: node_id=node-0, server_time_ms=...
```

## 5. Tests

```sh
ctest --test-dir build/debug --output-on-failure
```

## 6. Docker / 3-node cluster

```sh
docker compose up --build
```

Host ports `50051`, `50052`, `50053` route to nodes `node1`, `node2`,
`node3`. Inside the compose network they reach each other as
`node1:50051`, `node2:50051`, `node3:50051`. From the host:

```sh
./build/release/src/kvstore_client --target localhost:50052
```

## Notes

- **vcpkg manifest mode.** Dependencies are pinned in `vcpkg.json`. To lock
  the registry baseline for fully reproducible builds, run
  `$VCPKG_ROOT/vcpkg x-update-baseline --add-initial-baseline` from the
  project root once.
- **Sanitizers.** Debug enables `-fsanitize=address,undefined` globally on
  this project's targets. If you ever need to attach a debugger that
  conflicts with ASan, build with `-DCMAKE_BUILD_TYPE=RelWithDebInfo`.
- **Synchronous gRPC.** The server uses the blocking `Service` API. The
  generated stubs also support async and callback APIs, so switching later
  is local to `server_main.cpp`.
- **Generated proto output** lives in `build/<config>/proto/`. The
  `kvstore_proto` target exports it as a `PUBLIC` include dir, so consumers
  just `#include "kvstore.grpc.pb.h"`.
