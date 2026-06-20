#pragma once
#include "network/http_client.hpp"
#include <expected>
#include <system_error>
#include <string_view>
#include <string>

namespace mstorage::core {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    auto operator<=>(const Version&) const = default;
};

Version parse_version(std::string_view version_str);

class Updater {
public:
    static std::expected<void, std::error_code> perform_update(network::HttpClient& http_client, std::string_view current_version);
};

} // namespace mstorage::core
