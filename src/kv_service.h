// gRPC service implementation for the kvstore. Inherits from the
// proto-generated kvstore::KvStore::Service and dispatches RPC calls
// into the storage backend declared in include/kvstore/kv_store.h.

#pragma once

#include "kvstore.grpc.pb.h"
#include "kvstore/kv_store.h"
#include <string>
#include <memory> 
#include <vector> 
#include <atomic> 
#include <condition_variable> 
#include <mutex> 
#include <queue> 
#include <thread> 

namespace kvstore
{
    class KvStoreServiceImpl final : public KvStore::Service
    {
    public:        
        KvStoreServiceImpl(std::string node_id, Database& db, std::string role, std::vector<std::string> followers);

        ~KvStoreServiceImpl(); 

        grpc::Status Ping(grpc::ServerContext * /*context*/,
                          const kvstore::PingRequest *request,
                          kvstore::PingResponse *response) override;

        grpc::Status Get(grpc::ServerContext * /*context*/,
                         const kvstore::GetRequest *request,
                         kvstore::GetResponse *response) override;

        grpc::Status Put(grpc::ServerContext * /*context*/,
                         const kvstore::PutRequest *request,
                         kvstore::PutResponse *response) override;

        grpc::Status Delete(grpc::ServerContext * /*context*/,
                            const kvstore::DeleteRequest *request,
                            kvstore::DeleteResponse *response) override;

        grpc::Status Replicate(grpc::ServerContext * /*context*/,
                            const kvstore::ReplicateRequest *request,
                            kvstore::ReplicateResponse *response) override;

    private:
        void replicationLoop(); 
        void enqueueReplication(kvstore::WalRecord record); 

        std::string node_id_;
        Database& db_; 
        std::string role_; 
        std::vector<std::unique_ptr<kvstore::KvStore::Stub>> follower_stubs_;
        std::queue<kvstore::WalRecord> replication_queue_; 
        std::mutex queue_mu_; 
        std::condition_variable queue_cv_; 
        std::thread replication_thread_; 
        std::atomic<bool> shutdown_{false}; 
    };

} // namespace kvstore
