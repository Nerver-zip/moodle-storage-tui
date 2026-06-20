#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "core/updater.hpp"
#include "mock_http_client.hpp"
#include <filesystem>
#include <fstream>

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;

namespace mstorage::core::testing {

TEST(UpdaterTest, ParseVersion) {
    auto v1 = parse_version("v1.0.4");
    EXPECT_EQ(v1.major, 1);
    EXPECT_EQ(v1.minor, 0);
    EXPECT_EQ(v1.patch, 4);

    auto v2 = parse_version("v2.12.34");
    EXPECT_EQ(v2.major, 2);
    EXPECT_EQ(v2.minor, 12);
    EXPECT_EQ(v2.patch, 34);

    auto v3 = parse_version("10.5.6");
    EXPECT_EQ(v3.major, 10);
    EXPECT_EQ(v3.minor, 5);
    EXPECT_EQ(v3.patch, 6);

    auto v4 = parse_version("v1");
    EXPECT_EQ(v4.major, 1);
    EXPECT_EQ(v4.minor, 0);
    EXPECT_EQ(v4.patch, 0);
}

TEST(UpdaterTest, CompareVersion) {
    auto v1 = parse_version("v1.0.4");
    auto v2 = parse_version("v1.0.5");
    EXPECT_TRUE(v1 < v2);
    EXPECT_TRUE(v2 > v1);
    EXPECT_TRUE(v1 <= v2);

    auto v3 = parse_version("v1.10.0");
    auto v4 = parse_version("v1.2.0");
    EXPECT_TRUE(v3 > v4);
}

TEST(UpdaterTest, PerformUpdateAlreadyUpToDate) {
    NiceMock<network::MockHttpClient> mock_http;
    
    std::string mock_release_json = R"({
        "tag_name": "v1.0.5",
        "body": "* Fixed bugs"
    })";

    EXPECT_CALL(mock_http, get("https://api.github.com/repos/Nerver-zip/moodle-storage-tui/releases/latest", _))
        .WillOnce(Return(mock_release_json));

    // Executa o update informando versão compilada igual (v1.0.5)
    auto res = Updater::perform_update(mock_http, "v1.0.5");
    EXPECT_TRUE(res.has_value());
}

} // namespace mstorage::core::testing
