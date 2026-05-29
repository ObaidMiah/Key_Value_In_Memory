// Entry point for the kvstore gRPC server. Parses --port and --node-id,
// registers a placeholder KvStore service exposing Ping, and starts a
// synchronous gRPC server. Real KV logic will live in separate translation
// units once it exists.

#include <chrono>
#include <memory>
#include <string>

#include <CLI/CLI.hpp>
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "kvstore.grpc.pb.h"

#include "kvstore/kv_store.h"
#include "kv_service.h"

int main(int argc, char** argv) {
  CLI::App app{"kvstore server"};
  int port = 50051;
  std::string node_id = "node-0";
  app.add_option("--port", port, "TCP port to listen on")->capture_default_str();
  app.add_option("--node-id", node_id, "Unique node identifier")
      ->capture_default_str();
  CLI11_PARSE(app, argc, argv);

  // spdlog logger names are immutable after construction, so swap the
  // default logger for a freshly-built one named after this node.
  spdlog::set_default_logger(spdlog::stdout_color_mt(node_id));
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

  const std::string address = "0.0.0.0:" + std::to_string(port);

  // Enable server reflection so grpcurl can discover RPCs without a -proto flag.
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  kvstore::Database db; 
  kvstore::KvStoreServiceImpl service{node_id, db};


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
