#include <tuple>

#include <gtest/gtest.h>

#include "liteapp/core/activity_gate.h"
#include "liteapp/core/base64.h"
#include "liteapp/core/sha256.h"
#include "liteapp/core/random.h"
#include "liteapp/platform/openers.h"

TEST(FrameworkCore, TracksIndependentActivityKinds) {
    liteapp::ActivityGate gate;
    EXPECT_FALSE(gate.any());
    EXPECT_TRUE(gate.acquire("chat"));
    EXPECT_FALSE(gate.acquire("chat"));
    EXPECT_TRUE(gate.acquire("speech"));
    EXPECT_TRUE(gate.any());
    gate.release("chat");
    EXPECT_FALSE(gate.acquire("speech"));
    gate.release("speech");
    EXPECT_FALSE(gate.any());
}

TEST(FrameworkRandom, GeneratesIndependentUrlSafeCapabilities) {
    const auto first = liteapp::secure_random_token();
    const auto second = liteapp::secure_random_token();

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(*first, *second);
    EXPECT_EQ(first->size(), 43);
    EXPECT_EQ(
        first->find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"),
        std::string::npos);
}

TEST(FrameworkRandom, RejectsEmptyUrlBeforePlatformLaunch) {
    const auto result = liteapp::platform::open_url("");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, "invalid_url");
}

TEST(FrameworkRandom, RejectsMissingPathsBeforeFileManagerLaunch) {
    const auto missing = std::filesystem::temp_directory_path() / "liteapp-missing-file-manager-item";
    EXPECT_FALSE(liteapp::platform::open_path(missing));
    EXPECT_FALSE(liteapp::platform::reveal_in_file_manager(missing));
}
TEST(FrameworkCore, EncodesAndDecodesBase64) {
    for (const auto& [plain, standard, url] : {std::tuple{"", "", ""}, std::tuple{"f", "Zg==", "Zg"},
                                               std::tuple{"fo", "Zm8=", "Zm8"}, std::tuple{"foo", "Zm9v", "Zm9v"},
                                               std::tuple{"\xfb\xff", "+/8=", "-_8"}}) {
        EXPECT_EQ(liteapp::base64_encode(std::string_view(plain)), standard);
        EXPECT_EQ(liteapp::base64_encode(std::string_view(plain), liteapp::Base64::url), url);
        EXPECT_EQ(liteapp::base64_decode(standard), plain);
        EXPECT_EQ(liteapp::base64_decode(url), plain);
    }
    EXPECT_FALSE(liteapp::base64_decode("Zm9v!"));
}

TEST(Sha256, MatchesStandardVectors) {
    const auto hex = [](const std::array<std::byte, 32>& digest) {
        static constexpr std::string_view digits = "0123456789abcdef";
        std::string text;
        for (const auto byte : digest) {
            text += digits[std::to_integer<unsigned>(byte) >> 4U];
            text += digits[std::to_integer<unsigned>(byte) & 15U];
        }
        return text;
    };
    EXPECT_EQ(hex(liteapp::sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hex(liteapp::sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(hex(liteapp::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(hex(liteapp::sha256(std::string(1000000, 'a'))),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // RFC 7636 appendix B: the PKCE challenge for this verifier.
    const auto challenge = liteapp::sha256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
    EXPECT_EQ(liteapp::base64_encode(std::span<const std::byte>(challenge), liteapp::Base64::url),
              "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}
