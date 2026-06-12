Start: single node, in-memory hashmap + WAL, gRPC API for get/put/delete.

Eventually: 3-node leader/follower replication with configurable read consistency, then 2-shard partitioning via consistent hashing. Stop there. README explaining tradeoffs, p50/p99 benchmarks.

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
OK to a Put means the record is fsynced to disk. Same idea as Postgres with synchronous_commit=on. Costs about 1ms per write on an SSD, so per-client throughput is roughly 1/fsync_latency. fine for v1.

### record format on disk
[length: 4 bytes][payload bytes][crc32: 4 bytes]

payload is a WalRecord protobuf (op, key, value). length prefix gives framing without needing delimiters or escaping. CRC catches torn writes and bit-rot.

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
- bad CRC mid-file -> currently treated as torn (truncate and stop). design said throw. will revisit, it's lossy as-is

Get never touches the WAL.

### where it lives
Inside Database. The service handlers don't change. "fsync before mutate" is enforced once, at the only place mutations happen. Any future write path (replication apply, bulk load, etc) goes through the same gate.

### not doing yet
- segment rotation and compaction
- group commit (batch records into one fsync)
- shared_mutex so reads run concurrently with each other

## Replication (Phase 2, designed not built)

3 nodes total. 1 leader, 2 followers. Going Raft-style for election and log shipping.

### what OK means now
Client Puts go to leader. Leader writes to its own WAL, sends to both followers in parallel. As soon as at least 1 follower acks (fsynced), leader replies OK to client. So every acknowledged write is on at least 2 of 3 nodes. Can lose 1 node without losing data.

### picking a leader
- leader sends heartbeats every ~50ms
- each follower has a random election timeout between 150 and 300ms
- if no heartbeat lands by the timeout, follower figures leader is dead and starts an election

### stopping two leaders at once
when you start an election you become a candidate and ask the other nodes for votes. each node only votes once per "term" (basically the election round number). need a majority to win. in a 3-node cluster that's 2 votes (your own plus 1 other). two candidates can't both get 2 because there's only 1 other vote available and it goes to one of them. at most 1 leader per term, by the math.

random timeouts mean usually only 1 follower triggers an election at a time so the racing is rare.

### demoting a stale leader
every Raft message carries the term number. if you receive a message with a higher term than your own, you step down to follower and update your term. so an old leader that gets partitioned away, then reconnects, sees the new leader's term in a heartbeat reply, learns it's been deposed, demotes itself. stops accepting writes.

### protecting committed writes across leader changes
candidates include (last_log_term, last_log_index) in their vote requests. a follower only votes yes if the candidate is at least as up-to-date as the follower. since every committed write was on at least 2 nodes (option b above), the new leader has to be one of those 2 or it can't get majority. so committed writes always survive elections.

### shipping the log
leader pushes. for each follower the leader tracks next_index, basically "where i think this follower is in the log." leader sends "here's entry 43, the previous one was entry 42 in term 5." follower checks if it has entry 42 in term 5:
- yes -> appends entry 43
- no -> rejects. leader walks next_index back, retries with an earlier entry, repeats until they find common ground. then streams forward from there.

catchup uses the exact same mechanism. nothing special.

### reads
per-request flag. client decides per call:
- fresh -> read from leader. linearizable, but everything bottlenecks on leader
- lax -> read from any node. scales, but might see a stale value

same pattern Cassandra, MongoDB, DynamoDB, etcd all use.

### what has to be on disk for Raft to be safe
three things, all fsynced before responding to any Raft RPC:
- current_term (the term number you've seen so far)
- voted_for (who, if anyone, you voted for in current_term)
- log entries (already on disk via the WAL)

if any of these get lost on a crash you could vote twice in the same term, which means two candidates could both reach majority, which is split brain. probably a small metadata file next to wal.log, or extend the WAL records to carry term/votedFor too.

### not doing in v1
- dynamic cluster membership (joining/leaving at runtime). static config of node addresses for now.
- batched shipping (multiple entries per RPC)
- snapshots for fast follower catchup when the log gets huge

## Phase 3

2-shard partitioning via consistent hashing. Each shard is its own replicated 3-node group.

## Phase 4

README explaining tradeoffs, p50/p99 benchmarks.
