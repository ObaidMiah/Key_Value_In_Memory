Start: single node, in-memory hashmap + WAL, gRPC API for get/put/delete.

# Progress
- string -> int32 KV store via gRPC: Ping, Get, Put, Delete
- All mutations under a single std::mutex, lock_guard released at scope

# Decisions: kv_store (Database)
- std::unordered_map<std::string, int32_t> as the backing store
- single std::mutex mu_, mutable so const getters can lock
- getValue(key, out value) -> bool (hit/miss)
- putValue -> void (no failure mode in memory)
- deleteValue -> bool (true if key existed)
- find() over contains()+[] to avoid double lookups

# Decisions: kv_service
- KvStoreServiceImpl in kv_service.h/.cc, separate from server_main
- inherits kvstore::KvStore::Service, final, override on all handlers
- holds Database& (non-owning); server owns the Database
- ctor: KvStoreServiceImpl(node_id, Database&)
- Get / Delete -> NOT_FOUND on missing key
- Put -> always OK
- server reflection enabled so grpcurl can discover RPCs

# WAL

## Contract
OK to a Put means the record is fsynced to disk. Cost: ~1ms per write on SSD,
throughput ceiling per client ~= 1 / fsync_latency.

## Record format
[length: 4 bytes][payload bytes][crc32: 4 bytes]

Payload is a WalRecord protobuf message (op, key, value). Length prefix solves
framing; CRC catches torn writes and bit-rot.

## Mutation order (Put / Delete)
Under mu_:
  1. Build WalRecord, append [length][payload][crc] to wal.log
  2. fsync(wal.log)
  3. Update in-memory map
  4. Return OK

fsync precedes the map update because a concurrent Get could otherwise read a
value that is not yet durable. Single mutex around all four steps guarantees
WAL byte order matches map mutation order.

## Recovery
Database's constructor opens (or creates) wal.log and streams records:
- valid CRC -> apply op to map
- bad CRC at EOF -> truncate to that offset, stop (last write torn by crash)
- bad CRC mid-file -> refuse to start (disk corruption)

Get never touches the WAL.

## Placement
WAL lives inside Database. Service handlers do not change. Any future mutation
path (replication apply, bulk import, etc.) goes through the same gate.

## Deferred
- Segment rotation, compaction
- Group commit (batch fsync)
- std::shared_mutex for concurrent reads

# Next phases
- 3-node leader-follower replication, configurable read consistency
- 2-shard partitioning via consistent hashing
- README + p50/p99 benchmarks
