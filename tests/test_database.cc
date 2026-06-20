// Unit tests for kvstore::Database.
//
// Each test uses a unique WAL path under the OS temp directory. The fixture
// scrubs the file before and after every test so cases stay independent.

#include "kvstore/kv_store.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace {

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        wal_path_ = std::filesystem::temp_directory_path() /
                    (std::string("kvstore_") + info->test_suite_name() +
                     "_" + info->name() + ".log");
        std::filesystem::remove(wal_path_);
    }

    void TearDown() override {
        std::filesystem::remove(wal_path_);
    }

    std::filesystem::path wal_path_;
};

// ----- basic operations -----

TEST_F(DatabaseTest, FreshDatabaseIsEmpty) {
    kvstore::Database db{wal_path_.string()};
    EXPECT_EQ(db.getSize(), 0u);
}

TEST_F(DatabaseTest, PutThenGetReturnsValue) {
    kvstore::Database db{wal_path_.string()};
    db.putValue("foo", 42);

    int32_t value = 0;
    EXPECT_TRUE(db.getValue("foo", value));
    EXPECT_EQ(value, 42);
}

TEST_F(DatabaseTest, GetMissingKeyReturnsFalse) {
    kvstore::Database db{wal_path_.string()};
    int32_t value = 12345;  // sentinel
    EXPECT_FALSE(db.getValue("nope", value));
    EXPECT_EQ(value, 12345);  // unchanged on miss
}

TEST_F(DatabaseTest, PutOverwritesExistingValue) {
    kvstore::Database db{wal_path_.string()};
    db.putValue("foo", 1);
    db.putValue("foo", 2);

    int32_t value = 0;
    EXPECT_TRUE(db.getValue("foo", value));
    EXPECT_EQ(value, 2);
}

TEST_F(DatabaseTest, DeleteRemovesExistingKey) {
    kvstore::Database db{wal_path_.string()};
    db.putValue("foo", 42);

    EXPECT_TRUE(db.deleteValue("foo"));

    int32_t value = 0;
    EXPECT_FALSE(db.getValue("foo", value));
}

TEST_F(DatabaseTest, DeleteMissingKeyReturnsFalse) {
    kvstore::Database db{wal_path_.string()};
    EXPECT_FALSE(db.deleteValue("nope"));
}

TEST_F(DatabaseTest, SizeReflectsMutations) {
    kvstore::Database db{wal_path_.string()};
    EXPECT_EQ(db.getSize(), 0u);

    db.putValue("a", 1);
    db.putValue("b", 2);
    db.putValue("c", 3);
    EXPECT_EQ(db.getSize(), 3u);

    db.deleteValue("b");
    EXPECT_EQ(db.getSize(), 2u);

    db.deleteValue("nope");  // no-op
    EXPECT_EQ(db.getSize(), 2u);

    db.putValue("a", 99);    // overwrite — size unchanged
    EXPECT_EQ(db.getSize(), 2u);
}

TEST_F(DatabaseTest, SupportsManyKeys) {
    kvstore::Database db{wal_path_.string()};
    constexpr int kCount = 1000;

    for (int i = 0; i < kCount; ++i) {
        db.putValue("key-" + std::to_string(i), i);
    }
    EXPECT_EQ(db.getSize(), static_cast<size_t>(kCount));

    for (int i = 0; i < kCount; ++i) {
        int32_t value = 0;
        ASSERT_TRUE(db.getValue("key-" + std::to_string(i), value));
        EXPECT_EQ(value, i);
    }
}

// ----- WAL replay after restart -----

TEST_F(DatabaseTest, ReplayRestoresPutsAfterRestart) {
    {
        kvstore::Database db{wal_path_.string()};
        db.putValue("foo", 42);
        db.putValue("bar", 99);
    }  // db destroyed, fd_ closed; WAL still on disk

    kvstore::Database db{wal_path_.string()};
    int32_t value = 0;

    EXPECT_TRUE(db.getValue("foo", value));
    EXPECT_EQ(value, 42);

    EXPECT_TRUE(db.getValue("bar", value));
    EXPECT_EQ(value, 99);

    EXPECT_EQ(db.getSize(), 2u);
}

TEST_F(DatabaseTest, ReplayHonorsDeleteAfterRestart) {
    {
        kvstore::Database db{wal_path_.string()};
        db.putValue("foo", 42);
        db.deleteValue("foo");
    }

    kvstore::Database db{wal_path_.string()};
    int32_t value = 0;
    EXPECT_FALSE(db.getValue("foo", value));
    EXPECT_EQ(db.getSize(), 0u);
}

TEST_F(DatabaseTest, ReplayHonorsOverwriteAfterRestart) {
    {
        kvstore::Database db{wal_path_.string()};
        db.putValue("foo", 1);
        db.putValue("foo", 2);
        db.putValue("foo", 99);
    }

    kvstore::Database db{wal_path_.string()};
    int32_t value = 0;
    EXPECT_TRUE(db.getValue("foo", value));
    EXPECT_EQ(value, 99);
    EXPECT_EQ(db.getSize(), 1u);
}

TEST_F(DatabaseTest, ReplayHandlesInterleavedPutDelete) {
    {
        kvstore::Database db{wal_path_.string()};
        db.putValue("a", 1);
        db.putValue("b", 2);
        db.putValue("c", 3);
        db.deleteValue("b");
        db.putValue("d", 4);
        db.deleteValue("c");
    }

    kvstore::Database db{wal_path_.string()};
    int32_t value = 0;

    EXPECT_TRUE(db.getValue("a", value));
    EXPECT_EQ(value, 1);

    EXPECT_FALSE(db.getValue("b", value));
    EXPECT_FALSE(db.getValue("c", value));

    EXPECT_TRUE(db.getValue("d", value));
    EXPECT_EQ(value, 4);

    EXPECT_EQ(db.getSize(), 2u);
}

TEST_F(DatabaseTest, ReplayWorksAcrossMultipleRestarts) {
    {
        kvstore::Database db{wal_path_.string()};
        db.putValue("foo", 1);
    }
    {
        kvstore::Database db{wal_path_.string()};
        db.putValue("bar", 2);
    }
    {
        kvstore::Database db{wal_path_.string()};
        db.deleteValue("foo");
    }

    kvstore::Database db{wal_path_.string()};
    int32_t value = 0;
    EXPECT_FALSE(db.getValue("foo", value));
    EXPECT_TRUE(db.getValue("bar", value));
    EXPECT_EQ(value, 2);
}

TEST_F(DatabaseTest, EmptyWalFileLoadsCleanly) {
    { kvstore::Database db{wal_path_.string()}; }   // creates empty file

    kvstore::Database db{wal_path_.string()};        // reopens empty file
    EXPECT_EQ(db.getSize(), 0u);
}

}  // namespace
