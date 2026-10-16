#include "liteapp/core/random.h"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/SecRandom.h>
#else
#include <sys/random.h>
#endif

namespace liteapp {
namespace {

constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const std::vector<std::byte>& input) {
    std::string output;
    output.reserve((input.size() + 2U) / 3U * 4U);
    std::size_t index = 0;
    while (index + 3U <= input.size()) {
        const auto first = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index]));
        const auto second = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index + 1U]));
        const auto third = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index + 2U]));
        const auto value = (first << 16U) | (second << 8U) | third;
        output.push_back(alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(alphabet[(value >> 12U) & 0x3fU]);
        output.push_back(alphabet[(value >> 6U) & 0x3fU]);
        output.push_back(alphabet[value & 0x3fU]);
        index += 3U;
    }
    if (input.size() - index == 1U) {
        const auto value = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index])) << 16U;
        output.push_back(alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(alphabet[(value >> 12U) & 0x3fU]);
    } else if (input.size() - index == 2U) {
        const auto first = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index]));
        const auto second = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index + 1U]));
        const auto value = (first << 16U) | (second << 8U);
        output.push_back(alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(alphabet[(value >> 12U) & 0x3fU]);
        output.push_back(alphabet[(value >> 6U) & 0x3fU]);
    }
    return output;
}

}  // namespace

Result<std::vector<std::byte>> secure_random_bytes(std::size_t size) {
    std::vector<std::byte> output(size);
    if (output.empty()) {
        return output;
    }

#if defined(_WIN32)
    if (size > std::numeric_limits<ULONG>::max()) {
        return std::unexpected(Error{"invalid_size", "secure random byte request is too large"});
    }
    const auto status = BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(output.data()),
        static_cast<ULONG>(output.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        return std::unexpected(Error{"random_failed", "Windows secure random generation failed"});
    }
#elif defined(__APPLE__)
    if (SecRandomCopyBytes(kSecRandomDefault, output.size(), reinterpret_cast<std::uint8_t*>(output.data())) !=
        errSecSuccess) {
        return std::unexpected(Error{"random_failed", "macOS secure random generation failed"});
    }
#else
    std::size_t offset = 0;
    while (offset < output.size()) {
        const auto count = getrandom(output.data() + offset, output.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(
                Error{"random_failed", "system secure random generation failed: " + std::to_string(errno)});
        }
        offset += static_cast<std::size_t>(count);
    }
#endif
    return output;
}

Result<std::string> secure_random_token(std::size_t byte_count) {
    auto bytes = secure_random_bytes(byte_count);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return base64url_encode(*bytes);
}

}  // namespace liteapp