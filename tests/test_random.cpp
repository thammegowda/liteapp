#include <gtest/gtest.h>

#include "liteapp/core/random.h"
#include "liteapp/platform/openers.h"

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