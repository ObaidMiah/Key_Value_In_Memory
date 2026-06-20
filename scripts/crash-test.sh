#!/usr/bin/env bash
set -e

WAL=/tmp/wal-test.log
PORT=50051
BIN=./build/release/src/kvstore_server

rm -f "$WAL"

echo "==> start server"
"$BIN" --port $PORT --node-id n1 --wal-path "$WAL" &
SERVER_PID=$!
sleep 1

echo "==> Put foo=42, bar=99, baz=7"
grpcurl -plaintext -d '{"key":"foo","value":42}' localhost:$PORT kvstore.KvStore/Put
grpcurl -plaintext -d '{"key":"bar","value":99}' localhost:$PORT kvstore.KvStore/Put
grpcurl -plaintext -d '{"key":"baz","value":7}'  localhost:$PORT kvstore.KvStore/Put

echo "==> Get before crash"
grpcurl -plaintext -d '{"key":"foo"}' localhost:$PORT kvstore.KvStore/Get
grpcurl -plaintext -d '{"key":"bar"}' localhost:$PORT kvstore.KvStore/Get
grpcurl -plaintext -d '{"key":"baz"}' localhost:$PORT kvstore.KvStore/Get

echo "==> kill -9 (simulated crash)"
kill -9 $SERVER_PID
wait $SERVER_PID 2>/dev/null || true
sleep 1

echo "==> restart against same WAL"
"$BIN" --port $PORT --node-id n1 --wal-path "$WAL" &
SERVER_PID=$!
sleep 1

echo "==> Get after restart - expect 42, 99, 7"
grpcurl -plaintext -d '{"key":"foo"}' localhost:$PORT kvstore.KvStore/Get
grpcurl -plaintext -d '{"key":"bar"}' localhost:$PORT kvstore.KvStore/Get
grpcurl -plaintext -d '{"key":"baz"}' localhost:$PORT kvstore.KvStore/Get

echo "==> cleanup"
kill $SERVER_PID 2>/dev/null || true
rm -f "$WAL"
