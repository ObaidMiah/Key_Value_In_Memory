// Minimal synchronous gRPC client. Sends one Ping to the server identified
// by --target and prints what comes back. Useful as a smoke test against
// any single node.

#include <memory>
#include <string>

#include <CLI/CLI.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "kvstore.grpc.pb.h"

int main(int argc, char** argv) {
  CLI::App app{"kvstore client"};
  std::string target = "localhost:50051";
  std::string from = "client-cli";
  app.add_option("--target", target, "host:port of the server")
      ->capture_default_str();
  app.add_option("--from", from, "Identifier sent in the Ping request")
      ->capture_default_str();
  CLI11_PARSE(app, argc, argv);

  auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  auto stub = kvstore::KvStore::NewStub(channel);

  kvstore::PingRequest request;
  request.set_from(from);
  kvstore::PingResponse response;
  grpc::ClientContext context;

  grpc::Status status = stub->Ping(&context, request, &response);
  if (!status.ok()) {
    spdlog::error("Ping failed: {} (code={})",
                  status.error_message(),
                  static_cast<int>(status.error_code()));
    return 1;
  }
  spdlog::info("Ping ok: node_id={}, server_time_ms={}",
               response.node_id(), response.server_time_ms());
  return 0;
}
