#pragma once

#include <expected>
#include <string>

namespace liteapp {

struct Error {
    std::string code;
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

}  // namespace liteapp