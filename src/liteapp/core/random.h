#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "liteapp/core/result.h"

namespace liteapp {

[[nodiscard]] Result<std::vector<std::byte>> secure_random_bytes(std::size_t size);
[[nodiscard]] Result<std::string> secure_random_token(std::size_t byte_count = 32);

}  // namespace liteapp