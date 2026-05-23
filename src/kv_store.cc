// In-memory key-value storage implementation. Interface declared in
// include/kvstore/kv_store.h.

#include "kvstore/kv_store.h"

namespace kvstore
{
    bool Database::getValue(const std::string &key, int32_t &value) const
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = database_.find(key);
        if (it != database_.end())
        {
            value = it->second;
            return true;
        }

        return false;
    }

    bool Database::deleteValue(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mu_);

        auto it = database_.find(key); 
        if (it != database_.end())
        {
            database_.erase(it);
            return true;
        }

        return false; // is this correct if it doesn't exist?
    }

    bool Database::putValue(const std::string &key, int32_t val)
    {
        std::lock_guard<std::mutex> lock(mu_);

        database_[key] = val;
        return true;

        // don't know if a false is possible
    }

    size_t Database::getSize() const
    {
        std::lock_guard<std::mutex> lock(mu_);

        return database_.size();
    }
} // namespace kvstore
