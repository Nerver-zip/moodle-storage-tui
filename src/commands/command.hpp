#pragma once
#include <expected>
#include <system_error>

namespace mstorage::commands {

class Command {
public:
    virtual ~Command() = default;
    virtual std::expected<void, std::error_code> execute() = 0;
};

} // namespace mstorage::commands
