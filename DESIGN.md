Start: single node, in-memory hashmap + WAL, gRPC API for get/put/delete.

Scope: single node done. Next is a concurrency upgrade on the storage layer, then async leader/follower replication, then benchmarks and a README.

## Progress

i. string -> int32 KV store over gRPC (Ping, Get, Put, Delete).
ii. WAL wired in. Every Put/Delete writes [len][payload][crc] then fsyncs before replying OK.
iii. Replay on startup, with torn-write recovery at the tail.
iv. Unit tests, plus a manual crash-recovery script in scripts/crash-test.sh.
v. CI extended with Put/Get/Delete via grpcurl.

## kv_store (Database)

- backing store is std::unordered_map<std::string, int32_t>
- single std::mutex mu_, marked mutable so const getters can still lock
- getValue uses an out-param and returns bool for hit/miss
- putValue returns void. nothing in the in-memory path can fail meaningfully
- deleteValue returns bool, true if the key was actually there
- use find() not contains() + [] so we don't hash the key twice

## kv_service

- KvStoreServiceImpl lives in its own kv_service.h/.cc, not inline in server_main
- holds Database& (server owns the DB, service borrows it)
- ctor takes (node_id, Database&)
- Get and Delete return NOT_FOUND on missing key, Put always returns OK
- server reflection is on so grpcurl can poke at it without a -proto flag

## WAL

### what OK means
OK to a Put means the record is fsynced to disk. Same idea as Postgres with synchronous_commit=on. Costs about 1ms per write on an SSD, so per-client throughput is roughly 1/fsync_latency.

### record format on disk
[length: 4 bytes][payload bytes][crc32: 4 bytes]

payload is a WalRecord protobuf (op, key, value). length prefix gives framing without delimiters. CRC catches torn writes and bit-rot.

### what putValue/deleteValue do, inside mu_
1. build the WalRecord, append [len][payload][crc] to wal.log
2. fsync(fd)
3. update the in-memory map
4. return OK

fsync goes before the map update because a concurrent Get could otherwise see a value that isn't durable yet, and if the server crashed at that point the client would have seen data that doesn't exist after restart.

single mutex around all four steps means WAL byte order is the same as map mutation order, and no two writers can interleave on the file.

### recovery
Database's constructor opens wal.log (creates if missing) and walks records from the start:
- valid CRC -> apply the op to the map
- bad CRC at EOF -> ftruncate to the offset of that record, stop. expected after a crash mid-write
- bad CRC mid-file -> truncate and stop

Get never touches the WAL.

### where it lives
Inside Database. The service handlers don't change. "fsync before mutate" is enforced once, at the only place mutations happen.

## Concurrency upgrade (next)

Swap std::mutex for std::shared_mutex on Database. const methods (getValue, getSize) take a shared_lock so reads run concurrently with each other. Writes (putValue, deleteValue) take a unique_lock and stay serialized.

Will benchmark before and after with N concurrent reader threads plus a steady write load. Expect read throughput to scale roughly linearly with cores.

## Replication

Async leader/follower. 1 leader, 1 follower minimum. Both nodes hard-coded by address in config.

### write path
- client Puts hit the leader
- leader runs its existing WAL + fsync + map update + returns OK to client
- in the background, leader streams new WAL entries to the follower over gRPC
- follower deserializes each entry and applies it to its own in-memory map

### reads
Per-request flag. Client picks per call:
- fresh -> read from leader. always current.
- lax -> read from either node. follower may be a few ms behind.

### tradeoffs
- OK means leader durability only. follower might be a few ms behind the leader.
- follower can lag, so lax reads can return slightly stale values.
- leader is configured statically. if it dies, the cluster is read-only until someone restarts it.

## Benchmarks

What to measure:
- Put throughput, single client and N concurrent clients
- Get throughput, single client and N concurrent clients
- Put p50 / p99 latency
- Get p50 / p99 latency
- Read throughput before vs after the shared_mutex upgrade
- Read throughput single-node vs 2-node replicated (lax reads split across nodes)

Tool: small C++ load generator using the existing gRPC client. Run against Release builds. Numbers go in the README.

## README

Final piece. Covers:
- what the project does
- architecture diagram
- design decisions and tradeoffs (link to this file for full detail)
- benchmark numbers
- how to build and run locally
