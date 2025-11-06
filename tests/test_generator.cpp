#include <gtest/gtest.h>
#include <coflux/generator.hpp>
#include <ranges>

/*
coflux::generator<int> recursive_gen(int level) {
    if (level > 0) {
        co_yield level * 10;
        co_yield recursive_gen(level - 1);
        co_yield level * 10 + 1;
    }
    else {
        co_yield 0;
    }
}

TEST(GeneratorTest, BasicIteration) {
    auto gen = [](int n) -> coflux::generator<int> {
        for (int i = 0; i < n; ++i) co_yield i;
        }(5);

    std::vector<int> results;
    for (int val : gen) {
        results.push_back(val);
    }
    std::vector<int> expected = { 0, 1, 2, 3, 4 };
    ASSERT_EQ(results, expected);
}

TEST(GeneratorTest, RangesIntegration) {
    static_assert(std::ranges::view<coflux::generator<int>>);
    auto count_gen = [](int n) -> coflux::generator<int> {
        for (int i = 0; i < n; ++i) co_yield i;
        }(10);

    auto view = std::move(count_gen) | std::views::take(5); // mut be an rvalue as a left operand 
    std::vector<int> results;
    for (int val : view) {
        results.push_back(val);
    }
    std::vector<int> expected = { 0, 1, 2, 3, 4 };
    ASSERT_EQ(results, expected);
}

TEST(GeneratorTest, ImplicitRecursion) {
    auto gen = recursive_gen(3);
    std::vector<int> results;
    for (int val : gen) {
        results.push_back(val);
    }
    // 预期结果: 30, (20, (10, 0, 11), 21), 31 -> 展开后
    std::vector<int> expected = { 30, 20, 10, 0, 11, 21, 31 };
    ASSERT_EQ(results, expected);
}
*/