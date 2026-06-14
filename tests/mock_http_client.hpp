#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "network/http_client.hpp"

namespace mstorage::network {

class MockHttpClient : public HttpClient {
public:
    MOCK_METHOD((std::expected<std::string, std::error_code>), get, (const std::string& url, const cpr::Cookies& cookies), (override));
    MOCK_METHOD((std::expected<std::string, std::error_code>), post, (const std::string& url, const cpr::Payload& payload, const cpr::Cookies& cookies), (override));
    MOCK_METHOD((std::expected<std::string, std::error_code>), post_raw, (const std::string& url, const std::string& body, const std::string& content_type, const cpr::Cookies& cookies), (override));
    MOCK_METHOD((std::expected<std::string, std::error_code>), post_multipart, (const std::string& url, const cpr::Multipart& multipart, const cpr::Cookies& cookies), (override));
};

} // namespace mstorage::network
