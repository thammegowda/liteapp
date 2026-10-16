#include <stdexcept>

#include <gtest/gtest.h>

#include "liteapp/core/generator.h"

namespace {

liteapp::Generator<int> values(bool& started) {
    started = true;
    co_yield 1;
    co_yield 2;
}

liteapp::Generator<int> failure() {
    co_yield 1;
    throw std::runtime_error{"generator failure"};
}

}  // namespace

TEST(Generator, IsLazyAndSinglePass) {
    bool started = false;
    auto generated = values(started);
    EXPECT_FALSE(started);
    int total = 0;
    for (const auto value : generated) total += value;
    EXPECT_TRUE(started);
    EXPECT_EQ(total, 3);
}

TEST(Generator, PropagatesCoroutineExceptions) {
    auto generated = failure();
    auto iterator = generated.begin();
    EXPECT_EQ(*iterator, 1);
    EXPECT_THROW(++iterator, std::runtime_error);
}

TEST(Generator, PreservesProgressWhenMoved) {
    bool started = false;
    auto source = values(started);
    auto iterator = source.begin();
    EXPECT_EQ(*iterator, 1);
    auto moved = std::move(source);
    EXPECT_EQ(*moved.begin(), 1);
}