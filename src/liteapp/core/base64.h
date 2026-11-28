#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace liteapp {

enum class Base64 { standard, url };

// Encodes bytes as base64. `standard` uses "+/" with "=" padding; `url` uses "-_" without padding.
[[nodiscard]] std::string base64_encode(std::span<const std::byte> bytes, Base64 alphabet = Base64::standard);
[[nodiscard]] std::string base64_encode(std::string_view bytes, Base64 alphabet = Base64::standard);
// Decodes either alphabet, with or without padding; nullopt for any other character.
[[nodiscard]] std::optional<std::string> base64_decode(std::string_view text);

}  // namespace liteapp
