#include "liteapp/core/sha256.h"

#include <bit>
#include <cstdint>

namespace liteapp {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

void compress(std::array<std::uint32_t, 8>& state, const unsigned char* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = static_cast<std::uint32_t>(block[4 * index]) << 24U |
                       static_cast<std::uint32_t>(block[4 * index + 1]) << 16U |
                       static_cast<std::uint32_t>(block[4 * index + 2]) << 8U |
                       static_cast<std::uint32_t>(block[4 * index + 3]);
    }
    for (std::size_t index = 16; index < 64; ++index) {
        const auto low = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3U);
        const auto high = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10U);
        words[index] = words[index - 16] + low + words[index - 7] + high;
    }
    auto [a, b, c, d, e, f, g, h] = state;
    for (std::size_t index = 0; index < 64; ++index) {
        const auto first = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                           round_constants[index] + words[index];
        const auto second = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    const std::array<std::uint32_t, 8> mixed{a, b, c, d, e, f, g, h};
    for (std::size_t index = 0; index < 8; ++index) state[index] += mixed[index];
}

}  // namespace

std::array<std::byte, 32> sha256(std::string_view bytes) {
    std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                       0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t offset = 0;
    for (; offset + 64 <= bytes.size(); offset += 64) compress(state, data + offset);
    // The final block(s): the remaining bytes, a one bit, zeros, and the message length in bits.
    std::array<unsigned char, 128> tail{};
    const auto remaining = bytes.size() - offset;
    for (std::size_t index = 0; index < remaining; ++index) tail[index] = data[offset + index];
    tail[remaining] = 0x80U;
    const std::size_t tail_size = remaining < 56 ? 64 : 128;
    const auto bits = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (std::size_t index = 0; index < 8; ++index)
        tail[tail_size - 1 - index] = static_cast<unsigned char>(bits >> (8U * index));
    compress(state, tail.data());
    if (tail_size == 128) compress(state, tail.data() + 64);
    std::array<std::byte, 32> digest{};
    for (std::size_t index = 0; index < 8; ++index)
        for (std::size_t part = 0; part < 4; ++part)
            digest[4 * index + part] = static_cast<std::byte>(state[index] >> (24U - 8U * part));
    return digest;
}

}  // namespace liteapp
