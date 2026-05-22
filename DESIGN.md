Start: single node, in-memory hashmap + WAL, gRPC API for get/put/delete.
Add: 3-node leader-follower replication with configurable read consistency.
Add: 2-shard partitioning via consistent hashing.
Stop there. Strong README explaining tradeoffs. Benchmarks with p50/p99.