A durable, replicated, in-memory key-value store written in C++. Built as a learning project for distributed-systems primitives: WAL durability, crash recovery, concurrent access, async replication, and benchmarking.

## What's built

i. string -> int32 KV store over gRPC (Get, Put, Delete, Ping).
ii. Write-ahead log: every Put/Delete is fsynced to disk before the server acks the client.
iii. Crash recovery: replay on startup with torn-write detection at the tail.
iv. std::shared_mutex on the storage layer (concurrent reads, exclusive writes).
v. Async leader/follower replication: leader pushes WAL records to followers in a background thread.
vi. GoogleTest unit tests, a manual crash-recovery script, and CI on every push.

## Single node

The server is a gRPC server with one stateful piece (a Database class).

When a Put comes in:
1. Take an exclusive lock.
2. Serialize the op to a WAL record [length: 4 bytes][payload][crc32: 4 bytes].
3. Write it to wal.log and fsync.
4. Update the in-memory map.
5. Reply OK.

OK means the record is on disk. Same contract as Postgres with synchronous_commit=on.

On startup, Database walks wal.log from the beginning, validates each record's CRC, and replays it into the map. A bad CRC at the end means a crash mid-write, so truncate and continue. Reads skip the WAL.

## Replication

1 leader, 1 or more followers, statically configured.

The leader handles all writes. After fsync, it pushes the WalRecord onto a thread-safe queue. A background thread drains the queue and calls Replicate on every follower. Each follower's Replicate handler applies the record to its own Database (which has its own WAL).

Fire-and-forget. The leader doesn't wait for follower acks. Followers may lag by a few ms.

Reads can hit either node. Set require_fresh=true on a Get to force leader-only.

No automatic failover. If the leader dies the cluster is read-only until someone restarts it. Raft was designed (see DESIGN.md) but scoped out.

## Benchmarks

kvstore_bench is a load generator that hits a server with N concurrent client threads and reports throughput + p50/p99 latency.

I expected std::shared_mutex to beat std::mutex on read-heavy workloads. It didn't (within noise either way). Each request takes about 315 µs end-to-end but the lock is only held for 1-2 µs. The bottleneck is gRPC's sync server thread pool, not the lock. Kept shared_mutex anyway because it's the semantically right type.

16-thread, read-only:
- mutex: 48,859 ops/sec, p50 317 µs
- shared_mutex: 49,226 ops/sec, p50 316 µs

Raw output in benchmarks/.

## Build

Local (macOS, fast):

```sh
brew install grpc protobuf googletest spdlog cli11 nlohmann-json grpcurl
env -u VCPKG_ROOT cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/release --parallel
```

Linux / CI uses vcpkg; the CMake config supports both.

## Run

Single node:

```sh
./build/release/src/kvstore_server --port 50051 --node-id n1 --wal-path /tmp/wal.log

grpcurl -plaintext -d '{"key":"foo","value":42}' localhost:50051 kvstore.KvStore/Put
grpcurl -plaintext -d '{"key":"foo"}'             localhost:50051 kvstore.KvStore/Get
```

Two nodes (start the follower first):

```sh
./build/release/src/kvstore_server --port 50052 --node-id follower-1 \
  --wal-path /tmp/wal-f.log \
  --role follower --leader-target localhost:50051 &

./build/release/src/kvstore_server --port 50051 --node-id leader-1 \
  --wal-path /tmp/wal-l.log \
  --role leader --follower-targets localhost:50052 &

grpcurl -plaintext -d '{"key":"foo","value":42}' localhost:50051 kvstore.KvStore/Put
sleep 0.1
grpcurl -plaintext -d '{"key":"foo"}' localhost:50052 kvstore.KvStore/Get   # value=42
```

Docker compose brings up the same 2-node setup:

```sh
docker compose up --build
```

## Tests

```sh
ctest --test-dir build/debug --output-on-failure
```

ctest runs against Debug because sanitizers (ASan + UBSan) catch real memory bugs. Integration smoke tests run against Release (ASan + gRPC don't play nicely at channel creation).

scripts/crash-test.sh does an end-to-end crash recovery check (Put -> kill -9 -> restart -> Get).

## What's not built

- No automatic failover, no consensus protocol.
- No retry on failed replication.
- No sharding.
- Values are int32 only.

Full design and scoping decisions in [DESIGN.md](DESIGN.md).
