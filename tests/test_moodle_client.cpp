#include <gtest/gtest.h>
#include "moodle/moodle_client.hpp"
#include "mock_http_client.hpp"

using namespace mstorage;
using namespace mstorage::moodle;
using namespace mstorage::network;
using ::testing::Return;
using ::testing::_;

class MoodleClientTest : public ::testing::Test {
protected:
    MockHttpClient mock_http;
    std::string moodle_url = "https://moodle.example.com";
    MoodleClient client{mock_http, moodle_url};
};

TEST_F(MoodleClientTest, GetDraftInfoSuccess) {
    std::string html_content = R"(
        <html>
            <script>
                var M = {"cfg":{"sesskey":"test_sesskey","contextid":12345}};
            </script>
            <input type="hidden" name="files_filemanager" value="987654321">
        </html>
    )";

    EXPECT_CALL(mock_http, get(moodle_url + "/user/files.php", _))
        .WillOnce(Return(html_content));

    auto result = client.get_draft_info("fake_cookie");
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sesskey, "test_sesskey");
    EXPECT_EQ(result->itemid, "987654321");
    EXPECT_EQ(result->contextid, "12345");
}

TEST_F(MoodleClientTest, UploadFileSuccess) {
    MoodleClient::DraftInfo info{"key", "item", "ctx"};
    
    EXPECT_CALL(mock_http, post_multipart(_, _, _)).WillOnce(Return(std::string("{}")));

    auto result = client.upload_file("CMakeLists.txt", "/", info, "cookie");
    EXPECT_TRUE(result.has_value());
}
