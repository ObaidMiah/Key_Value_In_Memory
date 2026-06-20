# kvstore

A durable, replicated, in-memory key-value store written in C++. Built as a learning project to get hands-on with distributed-systems primitives: write-ahead logging, crash recovery, gRPC, concurrent access, async replication, and benchmarking.

## What's built

- String-to-int32 key-value store accessible over gRPC (Get, Put, Delete, Ping).
- Write-ahead log on every Put/Delete, with fsync before the server acknowledges the client. Data survives crashes.
- Replay on startup, with detection and recovery from writes that were torn in half by a crash.
- Thread-safe access via std::shared_mutex (reads run concurrently, writes are exclusive).
- Async leader/follower replication: leader pushes WAL records to followers in a background thread; reads can be served from either node.
- Unit tests + a manual crash-recovery script + a CI pipeline (Debug build, ctest, Release smoke test, Docker compose 2-node replication test).
- A concurrent benchmark harness that measures throughput and latency percentiles.

## How it works (single node)

The server is a normal gRPC server with one stateful component: a `Database` class.

When a client sends a Put:
1. The server takes an exclusive lock on the Database.
2. It serializes the operation into a small WAL record: `[length: 4 bytes][protobuf payload][checksum: 4 bytes]`.
3. It writes that record to a file (`wal.log`) and calls `fsync` to force it to physical disk.
4. Only then does it update the in-memory hash map and reply OK to the client.

"OK" means "this write is on disk and survives a power loss." Same contract Postgres gives you with synchronous commit. The cost is one disk flush per write (around 1ms on SSD).

When the server starts up, the `Database` constructor opens `wal.log` and walks it from the beginning. For each record, it verifies the checksum and replays the operation into the in-memory map. If the last record on disk has a bad checksum, that's expected after a crash mid-write, so the server truncates it and continues normally.

Reads (Get) skip the WAL entirely and just read from the in-memory map. With `shared_mutex`, many readers can be inside the map at the same time.

## How it works (replication)

The cluster is 1 leader + N followers, all statically configured at startup. The leader handles all writes; reads can hit either.

When the leader gets a Put or Delete:
1. Runs the normal single-node path (WAL + fsync + map update + return OK to the client).
2. Pushes a `WalRecord` describing the operation onto a thread-safe queue.
3. A background thread drains the queue and sends each record to every follower via a `Replicate` gRPC call.

Each follower has a `Replicate` handler that just applies the record to its own Database — same `putValue`/`deleteValue` paths, so the follower ends up with its own WAL on disk too.

**This is fire-and-forget.** The leader doesn't wait for followers to ack before replying to the client. The follower may be a few ms behind. If the leader crashes after returning OK but before the entry reaches the follower, that entry is lost from the follower's perspective (the leader still has it on disk).

**No automatic failover.** Whoever is configured as leader stays leader. If the leader dies, the cluster is read-only until someone restarts it. Raft-style election was designed (see DESIGN.md) but scoped out to keep the project shippable.

## Benchmarks

I wrote a little load generator (`kvstore_bench`) that fires Put/Get calls from N concurrent client threads against a running server, then reports throughput and latency percentiles.

The interesting experiment was about the lock. I expected `std::shared_mutex` (which lets reads run in parallel) to beat `std::mutex` (one-at-a-time) on read-heavy workloads. So I built both, swapped between them, and ran the same benchmark on each.

**Result: basically no difference.**

| | mutex | shared_mutex |
|---|---|---|
| Read throughput (16 threads) | 48,859 ops/sec | 49,226 ops/sec |
| Read p50 latency | 317 µs | 316 µs |
| Read p99 latency | 470 µs | 456 µs |
| Write throughput (8 threads) | 22,258 ops/sec | 23,075 ops/sec |
| Mixed 90/10 throughput | 46,988 ops/sec | 46,550 ops/sec |

The reason the lock change didn't matter: each request takes about 315 microseconds end-to-end, but the lock is only held for 1-2 microseconds of that. The other ~313 microseconds is gRPC serialization, thread dispatch, and the network round trip. Removing lock contention doesn't help if the lock wasn't the bottleneck.

To confirm, I ran the same benchmark with 1 thread vs 32 threads. Single thread: 12k ops/sec at 75 µs per request. 32 threads: 50k ops/sec at 612 µs per request. So adding 32x more clients only got 4x more throughput, and per-request latency went up 8x. The real bottleneck is the gRPC sync server's internal thread pool, not the lock.

I kept `shared_mutex` anyway because it's the semantically right primitive (reads really don't conflict with each other, so the type should say so). The lesson was the more useful part: measure before assuming an optimization helps.

## Crash recovery test

There's a manual end-to-end test for crash recovery in `scripts/crash-test.sh`. It starts the server, writes a few keys, kills the server with `SIGKILL`, restarts it, and verifies the writes are still there. This is the test that catches anything CI can't (CI doesn't crash mid-run).

```sh
./scripts/crash-test.sh
```

## Layout

```
proto/                .proto files and the CMake rules that drive protoc
include/kvstore/      public headers (the Database interface)
src/                  server, client, and bench binaries; Database + service implementation
tests/                GoogleTest unit tests (basic ops + WAL replay scenarios)
scripts/              crash-test.sh and other dev scripts
.github/workflows/    CI: Debug build, ctest, Release smoke, Docker 2-node replication
Dockerfile            multi-stage Ubuntu 24.04 build
docker-compose.yml    local 2-node leader/follower cluster
vcpkg.json            dependency manifest for CI + Docker
DESIGN.md             design decisions and tradeoffs
```

## Build paths

The same `CMakeLists.txt` supports both.

### Local dev (macOS, recommended)

Uses Homebrew bottles, so the heavy deps (gRPC, protobuf, abseil) don't compile from source. Only your code compiles locally (~10 seconds).

```sh
brew install grpc protobuf googletest spdlog cli11 nlohmann-json grpcurl

env -u VCPKG_ROOT cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/release --parallel

# Debug too, for ctest + sanitizers
env -u VCPKG_ROOT cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/debug --parallel
```

`env -u VCPKG_ROOT` suppresses the vcpkg auto-detect so CMake falls back to the Homebrew prefix.

### Linux / CI / Docker (vcpkg)

```sh
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg

cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
```

First configure compiles gRPC + protobuf from source — 10-30 minutes on a cold cache, much faster after.

## Run

### Single node

```sh
# terminal 1
./build/release/src/kvstore_server --port 50051 --node-id node-0 --wal-path /tmp/wal.log

# terminal 2 — talk to it with grpcurl
grpcurl -plaintext -d '{"key":"foo","value":42}' localhost:50051 kvstore.KvStore/Put
grpcurl -plaintext -d '{"key":"foo"}'             localhost:50051 kvstore.KvStore/Get
grpcurl -plaintext -d '{"key":"foo"}'             localhost:50051 kvstore.KvStore/Delete
```

### Two nodes (leader + follower)

Start the follower first (so the leader's channel can connect immediately), then the leader pointing at it.

```sh
# terminal 1 — follower
./build/release/src/kvstore_server \
  --port 50052 --node-id follower-1 \
  --wal-path /tmp/wal-follower.log \
  --role follower --leader-target localhost:50051

# terminal 2 — leader
./build/release/src/kvstore_server \
  --port 50051 --node-id leader-1 \
  --wal-path /tmp/wal-leader.log \
  --role leader --follower-targets localhost:50052

# terminal 3 — Put on leader, read from either
grpcurl -plaintext -d '{"key":"foo","value":42}' localhost:50051 kvstore.KvStore/Put
sleep 0.1   # give the background thread a moment to ship

grpcurl -plaintext -d '{"key":"foo"}' localhost:50051 kvstore.KvStore/Get   # leader, value=42
grpcurl -plaintext -d '{"key":"foo"}' localhost:50052 kvstore.KvStore/Get   # follower, value=42
```

`--follower-targets` accepts a comma-separated list, so a leader with two followers would use `--follower-targets host1:port1,host2:port2`.

## Tests

```sh
ctest --test-dir build/debug --output-on-failure
```

ctest runs against Debug because sanitizers (ASan + UBSan) catch real memory bugs there. Integration smoke tests run against Release because gRPC's internal `alloca` use trips ASan at channel creation.

## Benchmarks

```sh
# start server first, then in another terminal:
./build/release/src/kvstore_bench --threads 16 --ops 50000 --read-pct 100
```

Flags:

- `--target` host:port (default `localhost:50051`)
- `--threads` concurrent client threads
- `--ops` operations per thread
- `--key-space` number of unique keys (cycles through these)
- `--read-pct` percent of ops that are Gets (0 = pure writes, 100 = pure reads)
- `--no-prefill` skip filling the key space before measuring

## Docker / 2-node cluster

```sh
docker compose up --build
```

Host port `50051` maps to the leader, `50052` to the follower. Inside the compose network they address each other as `leader:50051` and `follower:50051`. Same Put-on-leader, read-from-either-node flow as the local 2-node setup above.

## CI

`.github/workflows/ci.yml` runs on every push and PR. Two jobs in parallel:

- **build_and_test** — vcpkg install, Debug build, ctest, Release build, Ping smoke test, Put/Get/Delete via grpcurl
- **docker_smoke** — `docker compose up`, Put on the leader, verify the value replicated to the follower, then verify Delete propagated too

Green CI is the source of truth for "does this work on Linux." Local builds are for fast iteration.

## Known quirks

- **Apple Silicon Docker** defaults to `linux/arm64`. The Dockerfile lets vcpkg auto-detect the triplet — don't force `--triplet x64-linux`.
- **ASan + gRPC** crash at channel creation because of gRPC's internal `alloca`. Workaround: integration tests run against Release; ctest keeps ASan on Debug.
- **spdlog logger names are immutable.** To rename, build a new logger via `spdlog::stdout_color_mt(name)` and `set_default_logger(...)`.
- **vcpkg's cli11 port** needs `pkg-config` on the host (`brew install pkg-config`).
- **Start the follower before the leader.** gRPC channels are lazy, so the leader can technically start first, but any Put that fires before the follower is reachable will fail to replicate and that key will be permanently missing from the follower (no retry logic in v1).

## What's not built

- No automatic failover. Static leader config; if the leader dies, manual restart needed.
- No consensus protocol (Raft was designed but scoped out — see DESIGN.md).
- No sharding or partitioning.
- No retry on failed replication. If a Replicate RPC fails (follower down, network blip), the entry is lost from the follower's view.
- Values are `int32` only.

The design conversation behind those scoping decisions is in [DESIGN.md](DESIGN.md).
