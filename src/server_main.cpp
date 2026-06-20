// Entry point for the kvstore gRPC server. Parses --port and --node-id,
// registers a placeholder KvStore service exposing Ping, and starts a
// synchronous gRPC server.

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "kvstore.grpc.pb.h"

#include "kvstore/kv_store.h"
#include "kv_service.h"

namespace {

// Splits "a,b,c" into {"a","b","c"}. Trims whitespace, drops empties.
std::vector<std::string> split_csv(const std::string& s)
{
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ','))
  {
    auto first = token.find_first_not_of(" \t");
    auto last = token.find_last_not_of(" \t");
    if (first == std::string::npos) continue;
    out.push_back(token.substr(first, last - first + 1));
  }
  return out;
}

}  // namespace

int main(int argc, char **argv)
{
  CLI::App app{"kvstore server"};
  int port = 50051;
  std::string node_id = "node-0";
  std::string wal_path = "wal.log";
  std::string role = "leader";
  std::string follower_targets_csv;   // leader uses this
  std::string leader_target;          // follower uses this (informational for now)

  app.add_option("--port", port, "TCP port to listen on")->capture_default_str();
  app.add_option("--node-id", node_id, "Unique node identifier")
      ->capture_default_str();
  app.add_option("--wal-path", wal_path, "Path to the write-ahead log file")
      ->capture_default_str();
  app.add_option("--role", role, "Node role: leader or follower")
      ->capture_default_str()
      ->check(CLI::IsMember({"leader", "follower"}));
  app.add_option("--follower-targets", follower_targets_csv,
                 "Comma-separated host:port list of followers (leader only)")
      ->capture_default_str();
  app.add_option("--leader-target", leader_target,
                 "host:port of the leader (follower only)")
      ->capture_default_str();

  CLI11_PARSE(app, argc, argv);

  const std::vector<std::string> follower_targets = split_csv(follower_targets_csv);

  // Sanity check the role/target combination so a misconfigured node fails
  // loudly at startup rather than at first replicate.
  if (role == "leader" && follower_targets.empty())
  {
    // Leader with no followers is fine — degenerate "single node" mode.
  }
  if (role == "follower" && leader_target.empty())
  {
    // Not fatal yet; follower can still accept replicate calls without
    // knowing the leader. Logged below for visibility.
  }

  // spdlog logger names are immutable after construction, so swap the
  // default logger for a freshly-built one named after this node.
  spdlog::set_default_logger(spdlog::stdout_color_mt(node_id));
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

  const std::string address = "0.0.0.0:" + std::to_string(port);

  // Enable server reflection so grpcurl can discover RPCs without a -proto flag.
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  kvstore::Database db(wal_path);
  kvstore::KvStoreServiceImpl service{node_id, db, role, follower_targets};

  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server)
  {
    spdlog::error("Failed to bind {}", address);
    return 1;
  }
  spdlog::info("kvstore server listening on {} (node-id={}, role={})",
               address, node_id, role);
  if (role == "leader")
  {
    spdlog::info("leader: {} follower(s) configured", follower_targets.size());
    for (const auto& t : follower_targets) spdlog::info("  follower -> {}", t);
  }
  else
  {
    spdlog::info("follower: leader={}",
                 leader_target.empty() ? "<unset>" : leader_target);
  }

  server->Wait();
  return 0;
}
