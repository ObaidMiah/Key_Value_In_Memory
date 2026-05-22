# Convenience commands for local macOS development. CI uses vcpkg directly,
# not this Makefile. Assumes Homebrew prerequisites are installed:
#   brew install grpc protobuf googletest spdlog cli11 nlohmann-json

PORT    ?= 50051
NODE_ID ?= node-0
TARGET  ?= localhost:$(PORT)

CMAKE_PREFIX ?= /opt/homebrew

.DEFAULT_GOAL := help

.PHONY: help build build-debug run client test dev configure configure-debug clean

help:
	@echo "Available targets:"
	@echo "  build            Build Release (~10s incremental)"
	@echo "  build-debug      Build Debug with ASan + UBSan"
	@echo "  run              Start the server  [PORT=$(PORT) NODE_ID=$(NODE_ID)]"
	@echo "  client           Ping the server   [TARGET=$(TARGET)]"
	@echo "  test             Run ctest on Debug build"
	@echo "  dev              Build Release then run server"
	@echo "  configure        Re-run cmake configure (Release)"
	@echo "  configure-debug  Re-run cmake configure (Debug)"
	@echo "  clean            Remove build/"
	@echo ""
	@echo "Examples:"
	@echo "  make run PORT=50052 NODE_ID=node-1"
	@echo "  make client TARGET=localhost:50052"

build: build/release/build.ninja
	cmake --build build/release --parallel

build-debug: build/debug/build.ninja
	cmake --build build/debug --parallel

run: build
	./build/release/src/kvstore_server --port $(PORT) --node-id $(NODE_ID)

client: build
	./build/release/src/kvstore_client --target $(TARGET)

test: build-debug
	ctest --test-dir build/debug --output-on-failure

dev: build run

configure:
	env -u VCPKG_ROOT cmake -S . -B build/release -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(CMAKE_PREFIX)

configure-debug:
	env -u VCPKG_ROOT cmake -S . -B build/debug -G Ninja \
	  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$(CMAKE_PREFIX)

# Auto-configure if the build dir doesn't exist yet, so first-time
# `make build` works without a separate configure step.
build/release/build.ninja:
	@$(MAKE) configure

build/debug/build.ninja:
	@$(MAKE) configure-debug

clean:
	rm -rf build
