// gRPC handler bodies. Declarations in kv_service.h.

#include "kv_service.h"
#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>
#include <chrono>

namespace kvstore
{
    KvStoreServiceImpl::KvStoreServiceImpl(std::string node_id, Database &db, std::string role, std::vector<std::string> followers)
        : node_id_(std::move(node_id)), db_(db), role_(role)
    {
        // for every follower, create a stub and push it into stub vector
        for (const auto &target : followers)
        {
            auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
            auto stub = kvstore::KvStore::NewStub(channel);
            follower_stubs_.push_back(std::move(stub));
        }

        if (role_ == "leader" && !follower_stubs_.empty())
        {
            replication_thread_ = std::thread(&KvStoreServiceImpl::replicationLoop, this);
        }
        spdlog::info("ctor: role={} followers={} thread_spawned={}",
                     role_, follower_stubs_.size(), replication_thread_.joinable());
    }

    KvStoreServiceImpl::~KvStoreServiceImpl()
    {
        {
            std::lock_guard lock(queue_mu_);
            shutdown_ = true;
        }

        queue_cv_.notify_all();
        if (replication_thread_.joinable())
        {
            replication_thread_.join();
        }
    }

    grpc::Status KvStoreServiceImpl::Ping(grpc::ServerContext * /*context*/,
                                          const kvstore::PingRequest *request,
                                          kvstore::PingResponse *response)
    {
        spdlog::info("Ping received from '{}'", request->from());
        response->set_node_id(node_id_);
        response->set_server_time_ms(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        return grpc::Status::OK;
    }

    grpc::Status KvStoreServiceImpl::Get(grpc::ServerContext * /*context*/,
                                         const kvstore::GetRequest *request,
                                         kvstore::GetResponse *response)
    {
        if (role_ == "follower" && request->require_fresh())
        {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Invalid request to follower might return stale data");
        }
        int32_t value;

        if (db_.getValue(request->key(), value))
        {
            response->set_value(value);
            return grpc::Status::OK;
        }

        return grpc::Status(grpc::StatusCode::NOT_FOUND, "key not found");
    }

    grpc::Status KvStoreServiceImpl::Put(grpc::ServerContext * /*context*/,
                                         const kvstore::PutRequest *request,
                                         kvstore::PutResponse *response)
    {
        spdlog::info("Put called, role={}", role_); // ADD THIS LINE

        if (role_ == "follower")
        {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Invalid request to follower");
        }
        int32_t value = request->value();

        db_.putValue(request->key(), value);

        if (role_ == "leader")
        {
            spdlog::info("put: enqueuing key={}", request->key());

            kvstore::WalRecord record;
            record.set_operation(kvstore::WalRecord::OP_PUT);
            record.set_key(request->key());
            record.set_value(request->value());
            enqueueReplication(std::move(record));
        }

        return grpc::Status::OK;
    }

    grpc::Status KvStoreServiceImpl::Delete(grpc::ServerContext * /*context*/,
                                            const kvstore::DeleteRequest *request,
                                            kvstore::DeleteResponse *response)
    {
        if (role_ == "follower")
        {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Invalid request to follower");
        }
        if (db_.deleteValue(request->key()))
        {
            if (role_ == "leader")
            {
                kvstore::WalRecord record;
                record.set_operation(kvstore::WalRecord::OP_DELETE);
                record.set_key(request->key());
                enqueueReplication(std::move(record));
            }
            return grpc::Status::OK;
        }

        return grpc::Status(grpc::StatusCode::NOT_FOUND, "key not found");
    }

    grpc::Status KvStoreServiceImpl::Replicate(grpc::ServerContext * /*context*/,
                                               const kvstore::ReplicateRequest *request,
                                               kvstore::ReplicateResponse *response)
    {
        const auto &record = request->record();

        switch (record.operation())
        {
        case kvstore::WalRecord::OP_PUT:
            db_.putValue(record.key(), record.value());
            break;
        case kvstore::WalRecord::OP_DELETE:
            db_.deleteValue(record.key());
            break;
        case kvstore::WalRecord::OP_UNSPECIFIED:
        default:
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "WalRecord operation unspecified");
        }
        return grpc::Status::OK;
    }

    void KvStoreServiceImpl::replicationLoop()
    {
        spdlog::info("replication loop started");

        while (true)
        {
            kvstore::WalRecord record;
            // grab record from queue
            {
                std::unique_lock lock(queue_mu_);
                queue_cv_.wait(lock, [this]
                               { return shutdown_ || !replication_queue_.empty(); });

                if (shutdown_ && replication_queue_.empty())
                    return;

                record = std::move(replication_queue_.front());
                replication_queue_.pop();
            }

            // push to followers
            for (auto &stub : follower_stubs_)
            {
                grpc::ClientContext ctx;

                kvstore::ReplicateRequest req;
                req.set_index(0); // sequence number
                *req.mutable_record() = record;

                kvstore::ReplicateResponse resp;
                auto status = stub->Replicate(&ctx, req, &resp);

                if (!status.ok())
                {
                    spdlog::warn("replicate failed: {}", status.error_message());
                }
                else
                {
                    spdlog::info("replicate ok");
                }
            }
        }
    }

    void KvStoreServiceImpl::enqueueReplication(kvstore::WalRecord record)
    {
        {
            std::lock_guard lock(queue_mu_);
            replication_queue_.push(std::move(record));
        }

        queue_cv_.notify_one();
    }

} // namespace kvstore
