#pragma once
#include <cpr/cpr.h>
#include <string>
#include <expected>
#include <system_error>

namespace mstorage::network {

class HttpClient {
public:
    virtual ~HttpClient() = default;

    virtual std::expected<std::string, std::error_code> get(const std::string& url, const cpr::Cookies& cookies = {}) = 0;
    virtual std::expected<std::string, std::error_code> post(const std::string& url, const cpr::Payload& payload, const cpr::Cookies& cookies = {}) = 0;
    virtual std::expected<std::string, std::error_code> post_raw(const std::string& url, const std::string& body, const std::string& content_type, const cpr::Cookies& cookies = {}) = 0;
    virtual std::expected<std::string, std::error_code> post_multipart(const std::string& url, const cpr::Multipart& multipart, const cpr::Cookies& cookies = {}) = 0;
};

class CprClient : public HttpClient {
public:
    std::expected<std::string, std::error_code> get(const std::string& url, const cpr::Cookies& cookies = {}) override {
        auto r = cpr::Get(cpr::Url{url}, cookies);
        if (r.error) return std::unexpected(std::make_error_code(std::errc::network_unreachable));
        return r.text;
    }

    std::expected<std::string, std::error_code> post(const std::string& url, const cpr::Payload& payload, const cpr::Cookies& cookies = {}) override {
        auto r = cpr::Post(cpr::Url{url}, payload, cookies);
        if (r.error) return std::unexpected(std::make_error_code(std::errc::network_unreachable));
        return r.text;
    }

    std::expected<std::string, std::error_code> post_raw(const std::string& url, const std::string& body, const std::string& content_type, const cpr::Cookies& cookies = {}) override {
        auto r = cpr::Post(cpr::Url{url}, cpr::Body{body}, cpr::Header{{"Content-Type", content_type}}, cookies);
        if (r.error) return std::unexpected(std::make_error_code(std::errc::network_unreachable));
        return r.text;
    }

    std::expected<std::string, std::error_code> post_multipart(const std::string& url, const cpr::Multipart& multipart, const cpr::Cookies& cookies = {}) override {
        auto r = cpr::Post(cpr::Url{url}, multipart, cookies);
        if (r.error) return std::unexpected(std::make_error_code(std::errc::network_unreachable));
        return r.text;
    }
};

} // namespace mstorage::network
