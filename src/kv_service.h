// gRPC service implementation for the kvstore. Inherits from the
// proto-generated kvstore::KvStore::Service and dispatches RPC calls
// into the storage backend declared in include/kvstore/kv_store.h.

#pragma once

#include "kvstore.grpc.pb.h"
#include "kvstore/kv_store.h"
#include "string"

namespace kvstore
{
    class KvStoreServiceImpl final : public KvStore::Service
    {
    public:
        KvStoreServiceImpl(std::string node_id, Database& db)
            : node_id_(std::move(node_id)), db_(db) {}

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

    private:
        std::string node_id_;
        Database& db_; 
    };

} // namespace kvstore
