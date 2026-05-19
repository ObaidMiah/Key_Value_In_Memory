// Entry point for the kvstore gRPC server. Parses --port and --node-id,
// registers a placeholder KvStore service exposing Ping, and starts a
// synchronous gRPC server. Real KV logic will live in separate translation
// units once it exists.

#include <chrono>
#include <memory>
#include <string>

#include <CLI/CLI.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "kvstore.grpc.pb.h"

namespace {

class KvStoreServiceImpl final : public kvstore::KvStore::Service {
 public:
  explicit KvStoreServiceImpl(std::string node_id)
      : node_id_(std::move(node_id)) {}

  grpc::Status Ping(grpc::ServerContext* /*context*/,
                    const kvstore::PingRequest* request,
                    kvstore::PingResponse* response) override {
    spdlog::info("Ping received from '{}'", request->from());
    response->set_node_id(node_id_);
    response->set_server_time_ms(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    return grpc::Status::OK;
  }

 private:
  std::string node_id_;
};

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"kvstore server"};
  int port = 50051;
  std::string node_id = "node-0";
  app.add_option("--port", port, "TCP port to listen on")->capture_default_str();
  app.add_option("--node-id", node_id, "Unique node identifier")
      ->capture_default_str();
  CLI11_PARSE(app, argc, argv);

  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
  spdlog::default_logger()->set_name(node_id);

  const std::string address = "0.0.0.0:" + std::to_string(port);
  KvStoreServiceImpl service{node_id};

  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    spdlog::error("Failed to bind {}", address);
    return 1;
  }
  spdlog::info("kvstore server listening on {} (node-id={})", address, node_id);
  server->Wait();
  return 0;
}
