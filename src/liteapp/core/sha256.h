#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace liteapp {

// SHA-256 (FIPS 180-4) of `bytes`, for protocol needs such as OAuth PKCE challenges; not a password hash.
[[nodiscard]] std::array<std::byte, 32> sha256(std::string_view bytes);

}  // namespace liteapp
