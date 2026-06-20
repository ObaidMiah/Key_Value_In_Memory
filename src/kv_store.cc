// In-memory key-value storage implementation. Interface declared in
// include/kvstore/kv_store.h.

#include "kvstore/kv_store.h"
#include "kvstore.pb.h"

#include <fcntl.h>  // open, O_* flags
#include <unistd.h> // close, write, fsync, read
#include <zlib.h>
#include <system_error>
#include <cerrno>
#include <vector>

// constexpr uint32_t maxPayloadSize = maxKeySize + sizeof(uint32_t);

namespace kvstore
{
    constexpr uint32_t maxPayloadSize = 1 * 1024 * 1024; // 1 MB

    Database::Database(std::string wal_path) : wal_log_path_(std::move(wal_path))
    {
        fd_ = open(wal_log_path_.c_str(),
                   O_RDWR | O_CREAT | O_APPEND,
                   0644);

        if (fd_ == -1)
        {
            throw std::system_error(errno, std::generic_category(), "failed to open " + wal_log_path_);
        }

        // replay WAL log

        // find file start
        ::lseek(fd_, 0, SEEK_SET);

        // while not end of file
        while (true)
        {
            off_t current_offset = lseek(fd_, 0, SEEK_CUR);

            // read length
            uint32_t len;
            ssize_t n = ::read(fd_, &len, sizeof(len));

            if (n == 0)
                break; // EOF
            if (n == -1)
            {
                throw std::system_error(errno, std::generic_category(), "replay: invalid payload size");
            }

            if (n != static_cast<ssize_t>(sizeof(len)) || len > maxPayloadSize)
            {
                throw std::runtime_error("replay: invalid payload len");
            }

            // read payload
            std::vector<char> buf;
            buf.resize(len);

            n = ::read(fd_, buf.data(), len);

            if (n != static_cast<ssize_t>(len))
            {
                ftruncate(fd_, current_offset);    
                break;
            }

            // read crc
            uint32_t crc;
            n = ::read(fd_, &crc, sizeof(crc));

            if (n != static_cast<ssize_t>(sizeof(crc)))
            {
                ftruncate(fd_, current_offset);
                break;
            }

            // compute crc and compare
            uint32_t crc_calc = crc32(0L, reinterpret_cast<const Bytef *>(buf.data()), buf.size());

            if (crc != crc_calc)
            {
                //TODO: Bad CRC mid-file should throw
                ftruncate(fd_, current_offset);
                break;
            }

            kvstore::WalRecord record;
            if (!record.ParseFromArray(buf.data(), buf.size()))
            {
                // parse failed 
                throw std::runtime_error("replay: malformed WalRecord");
            }

            // populate db 
            if (record.operation() == kvstore::WalRecord::OP_PUT)
            {
                database_[record.key()] = record.value();
            }
            else if (record.operation() == kvstore::WalRecord::OP_DELETE)
            {
                database_.erase(record.key());
            }
        }
    }

    Database::~Database() noexcept
    {
        close(fd_);
    }

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

        // construct WAL record
        kvstore::WalRecord record;
        record.set_operation(kvstore::WalRecord::OP_DELETE);
        record.set_key(key);

        std::string payload = record.SerializeAsString();

        // write len
        uint32_t len = payload.size();
        ssize_t n = ::write(fd_, &len, sizeof(len));

        if (n != static_cast<ssize_t>(sizeof(len)))
        {
            throw std::system_error(errno, std::generic_category(), "len write failed");
        }

        // write record
        n = ::write(fd_, payload.data(), payload.size());

        if (n != static_cast<ssize_t>(payload.size()))
        {
            throw std::system_error(errno, std::generic_category(), "record write failed");
        }

        uint32_t crc = crc32(0L, reinterpret_cast<const Bytef *>(payload.data()), payload.size());

        // write CRC
        n = ::write(fd_, &crc, sizeof(crc));

        if (n != static_cast<ssize_t>(sizeof(crc)))
        {
            throw std::system_error(errno, std::generic_category(), "crc write failed");
        }

        ::fsync(fd_);

        auto it = database_.find(key);
        if (it != database_.end())
        {
            database_.erase(it);
            return true;
        }

        return false; // is this correct if it doesn't exist?
    }

    void Database::putValue(const std::string &key, int32_t val)
    {
        std::lock_guard<std::mutex> lock(mu_);

        // construct WAL record
        kvstore::WalRecord record;
        record.set_operation(kvstore::WalRecord::OP_PUT);
        record.set_key(key);
        record.set_value(val);

        std::string payload = record.SerializeAsString();

        // write len
        uint32_t len = payload.size();
        ssize_t n = ::write(fd_, &len, sizeof(len));

        if (n != static_cast<ssize_t>(sizeof(len)))
        {
            throw std::system_error(errno, std::generic_category(), "len write failed");
        }

        // write record
        n = ::write(fd_, payload.data(), payload.size());

        if (n != static_cast<ssize_t>(payload.size()))
        {
            throw std::system_error(errno, std::generic_category(), "record write failed");
        }

        uint32_t crc = crc32(0L, reinterpret_cast<const Bytef *>(payload.data()), payload.size());

        // write CRC
        n = ::write(fd_, &crc, sizeof(crc));

        if (n != static_cast<ssize_t>(sizeof(crc)))
        {
            throw std::system_error(errno, std::generic_category(), "crc write failed");
        }

        // write to disk
        ::fsync(fd_);

        database_[key] = val;
    }

    size_t Database::getSize() const
    {
        std::lock_guard<std::mutex> lock(mu_);

        return database_.size();
    }
} // namespace kvstore
