#include "liteapp/core/base64.h"

#include <cstdint>

namespace liteapp {
namespace {

constexpr std::string_view standard_digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view url_digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::optional<std::uint32_t> digit_value(char character) {
    if (character >= 'A' && character <= 'Z') return static_cast<std::uint32_t>(character - 'A');
    if (character >= 'a' && character <= 'z') return static_cast<std::uint32_t>(character - 'a' + 26);
    if (character >= '0' && character <= '9') return static_cast<std::uint32_t>(character - '0' + 52);
    if (character == '+' || character == '-') return 62U;
    if (character == '/' || character == '_') return 63U;
    return std::nullopt;
}

}  // namespace

std::string base64_encode(std::span<const std::byte> bytes, Base64 alphabet) {
    const auto digits = alphabet == Base64::url ? url_digits : standard_digits;
    const bool padded = alphabet == Base64::standard;
    std::string output;
    output.reserve((bytes.size() + 2U) / 3U * 4U);
    for (std::size_t index = 0; index < bytes.size(); index += 3U) {
        const auto remaining = bytes.size() - index;
        const auto byte = [&](std::size_t offset) {
            return offset < remaining ? static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index + offset]))
                                      : 0U;
        };
        const auto value = (byte(0) << 16U) | (byte(1) << 8U) | byte(2);
        output.push_back(digits[(value >> 18U) & 0x3FU]);
        output.push_back(digits[(value >> 12U) & 0x3FU]);
        if (remaining > 1U) output.push_back(digits[(value >> 6U) & 0x3FU]);
        else if (padded) output.push_back('=');
        if (remaining > 2U) output.push_back(digits[value & 0x3FU]);
        else if (padded) output.push_back('=');
    }
    return output;
}

std::string base64_encode(std::string_view bytes, Base64 alphabet) {
    return base64_encode(std::as_bytes(std::span(bytes.data(), bytes.size())), alphabet);
}

std::optional<std::string> base64_decode(std::string_view text) {
    while (!text.empty() && text.back() == '=') text.remove_suffix(1);
    std::string bytes;
    bytes.reserve(text.size() / 4U * 3U + 2U);
    std::uint32_t buffer = 0;
    unsigned bits = 0;
    for (const char character : text) {
        const auto value = digit_value(character);
        if (!value) return std::nullopt;
        buffer = (buffer << 6U) | *value;
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            bytes.push_back(static_cast<char>((buffer >> bits) & 0xFFU));
        }
    }
    return bytes;
}

}  // namespace liteapp
