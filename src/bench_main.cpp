// Concurrent benchmark harness for the kvstore. Spawns N client threads
// that each fire a fixed number of Put/Get operations at a target server,
// measures every call's latency, and reports throughput plus p50/p99/p999.

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <CLI/CLI.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "kvstore.grpc.pb.h"

using Clock = std::chrono::steady_clock;
using Nanos = std::chrono::nanoseconds;

namespace {

struct Args {
    std::string target = "localhost:50051";
    int threads = 8;
    int ops_per_thread = 10000;
    int key_space = 1000;
    int read_pct = 90;
    bool skip_prefill = false;
};

struct ThreadResult {
    std::vector<Nanos> latencies;
    int errors = 0;
};

// Each thread keeps its own gRPC stub. Sharing a stub across threads is
// safe, but separate stubs avoid any contention inside the client library.
ThreadResult run_thread(const Args& args, int thread_id) {
    auto channel = grpc::CreateChannel(args.target, grpc::InsecureChannelCredentials());
    auto stub = kvstore::KvStore::NewStub(channel);

    std::mt19937 rng(static_cast<uint32_t>(thread_id) * 7919u + 1u);
    std::uniform_int_distribution<int> key_dist(0, args.key_space - 1);
    std::uniform_int_distribution<int> op_dist(0, 99);

    ThreadResult result;
    result.latencies.reserve(args.ops_per_thread);

    for (int i = 0; i < args.ops_per_thread; ++i) {
        const std::string key = "k-" + std::to_string(key_dist(rng));
        const bool is_read = op_dist(rng) < args.read_pct;

        const auto start = Clock::now();

        grpc::ClientContext ctx;
        grpc::Status status;
        if (is_read) {
            kvstore::GetRequest req;
            req.set_key(key);
            kvstore::GetResponse resp;
            status = stub->Get(&ctx, req, &resp);
        } else {
            kvstore::PutRequest req;
            req.set_key(key);
            req.set_value(static_cast<int32_t>(i));
            kvstore::PutResponse resp;
            status = stub->Put(&ctx, req, &resp);
        }

        const auto end = Clock::now();
        result.latencies.push_back(end - start);

        // NOT_FOUND on Get is a normal miss, not an error.
        if (!status.ok() && status.error_code() != grpc::StatusCode::NOT_FOUND) {
            result.errors++;
        }
    }

    return result;
}

// Warm the server with one Put per key so Get-heavy workloads aren't
// drowning in NOT_FOUND responses.
void prefill(const Args& args) {
    auto channel = grpc::CreateChannel(args.target, grpc::InsecureChannelCredentials());
    auto stub = kvstore::KvStore::NewStub(channel);
    for (int i = 0; i < args.key_space; ++i) {
        grpc::ClientContext ctx;
        kvstore::PutRequest req;
        req.set_key("k-" + std::to_string(i));
        req.set_value(i);
        kvstore::PutResponse resp;
        auto status = stub->Put(&ctx, req, &resp);
        if (!status.ok()) {
            spdlog::warn("prefill Put failed at i={}: {}", i, status.error_message());
        }
    }
}

double to_micros(Nanos d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

Nanos percentile(const std::vector<Nanos>& sorted, double p) {
    if (sorted.empty()) return Nanos{0};
    auto idx = static_cast<size_t>(p * (sorted.size() - 1));
    return sorted[idx];
}

void print_results(const Args& args,
                   std::vector<Nanos> all_latencies,
                   double elapsed_sec,
                   int total_errors) {
    std::sort(all_latencies.begin(), all_latencies.end());

    const size_t n = all_latencies.size();
    const auto p50 = percentile(all_latencies, 0.50);
    const auto p99 = percentile(all_latencies, 0.99);
    const auto p999 = percentile(all_latencies, 0.999);

    const double throughput = static_cast<double>(n) / elapsed_sec;

    std::cout << "\n";
    std::cout << "================ bench results ================\n";
    std::cout << "Target:         " << args.target << "\n";
    std::cout << "Threads:        " << args.threads << "\n";
    std::cout << "Ops/thread:     " << args.ops_per_thread << "\n";
    std::cout << "Total ops:      " << n << "\n";
    std::cout << "Read %:         " << args.read_pct << "\n";
    std::cout << "Key space:      " << args.key_space << "\n";
    std::cout << "Duration:       " << elapsed_sec << " s\n";
    std::cout << "Throughput:     " << static_cast<long long>(throughput) << " ops/sec\n";
    std::cout << "Latency p50:    " << to_micros(p50) << " us\n";
    std::cout << "Latency p99:    " << to_micros(p99) << " us\n";
    std::cout << "Latency p999:   " << to_micros(p999) << " us\n";
    std::cout << "Errors:         " << total_errors << "\n";
    std::cout << "===============================================\n";
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"kvstore benchmark"};
    Args args;
    app.add_option("--target", args.target, "gRPC target host:port")->capture_default_str();
    app.add_option("--threads", args.threads, "Concurrent client threads")->capture_default_str();
    app.add_option("--ops", args.ops_per_thread, "Operations per thread")->capture_default_str();
    app.add_option("--key-space", args.key_space, "Number of unique keys")->capture_default_str();
    app.add_option("--read-pct", args.read_pct, "Percent of operations that are Gets (0-100)")
        ->capture_default_str();
    app.add_flag("--no-prefill", args.skip_prefill, "Skip prefilling the key space before measuring");
    CLI11_PARSE(app, argc, argv);

    spdlog::set_default_logger(spdlog::stdout_color_mt("bench"));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

    if (!args.skip_prefill) {
        spdlog::info("prefilling {} keys at {}", args.key_space, args.target);
        prefill(args);
    }

    spdlog::info("starting bench: {} threads x {} ops, read pct {}",
                 args.threads, args.ops_per_thread, args.read_pct);

    const auto t0 = Clock::now();

    std::vector<std::thread> workers;
    std::vector<ThreadResult> results(args.threads);
    workers.reserve(args.threads);
    for (int i = 0; i < args.threads; ++i) {
        workers.emplace_back([&, i]() {
            results[i] = run_thread(args, i);
        });
    }
    for (auto& t : workers) t.join();

    const auto t1 = Clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::vector<Nanos> all_latencies;
    int total_errors = 0;
    for (auto& r : results) {
        all_latencies.insert(all_latencies.end(),
                             std::make_move_iterator(r.latencies.begin()),
                             std::make_move_iterator(r.latencies.end()));
        total_errors += r.errors;
    }

    print_results(args, std::move(all_latencies), elapsed, total_errors);
    return 0;
}
