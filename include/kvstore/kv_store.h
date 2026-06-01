// Public interface for the in-memory key-value storage backend.
// Implementation lives in src/kv_store.cc.

#pragma once

#include <unordered_map>
#include <string>
#include <cstdint>

#include <mutex>

namespace kvstore
{
    class Database
    {
    private:
        std::unordered_map<std::string, int32_t> database_;
        mutable std::mutex mu_;
        std::string wal_path_;

    public:
        explicit Database(std::string wal_path);

        bool getValue(const std::string &key, int32_t &value) const;
        bool deleteValue(const std::string &key);
        void putValue(const std::string &key, int32_t val);
        size_t getSize() const;
    };
} // namespace kvstore
