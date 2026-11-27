#include <tuple>

#include <gtest/gtest.h>

#include "liteapp/core/activity_gate.h"
#include "liteapp/core/base64.h"
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
