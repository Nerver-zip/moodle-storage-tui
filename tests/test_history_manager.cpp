#include <gtest/gtest.h>
#include "storage/upload_history.hpp"

using namespace mstorage::storage;

class HistoryManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::filesystem::temp_directory_path() / "mstorage_hist_test";
        std::filesystem::create_directories(temp_dir);
        setenv("HOME", temp_dir.c_str(), 1);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
};

TEST_F(HistoryManagerTest, RecordAndRetrieve) {
    HistoryManager manager;
    
    auto res1 = manager.record_upload("file1.txt", "url1");
    auto res2 = manager.record_upload("file2.txt", "url2");
    
    ASSERT_TRUE(res1.has_value());
    ASSERT_TRUE(res2.has_value());

    auto history = manager.get_history();
    ASSERT_TRUE(history.has_value());
    ASSERT_EQ(history->size(), 2);
    
    // We expect the most recent first (ORDER BY timestamp DESC), 
    // but if timestamps are identical, we should at least check both exist.
    bool found1 = false, found2 = false;
    for(const auto& entry : *history) {
        if (entry.filename == "file1.txt") found1 = true;
        if (entry.filename == "file2.txt") found2 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}
