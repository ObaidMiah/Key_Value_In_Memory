// gRPC handler bodies. Declarations in kv_service.h.

#include "kv_service.h"
#include <spdlog/spdlog.h>
#include <chrono>


namespace kvstore
{
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
        int32_t value; 
        
        if(db_.getValue(request->key(), value))
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

        int32_t value = request->value(); 
        
        db_.putValue(request->key(), value);
        return grpc::Status::OK; 
    }

    grpc::Status KvStoreServiceImpl::Delete(grpc::ServerContext * /*context*/,
                        const kvstore::DeleteRequest *request,
                        kvstore::DeleteResponse *response) 
    {        
        if(db_.deleteValue(request->key()))
        {
            return grpc::Status::OK; 
        }

        return grpc::Status(grpc::StatusCode::NOT_FOUND, "key not found");
    }
} // namespace kvstore
