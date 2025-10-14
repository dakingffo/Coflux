#include <iostream>
#include <string>
#include <ranges>
#include <vector>
#include <coflux/generator.hpp>

using coflux::generator;

generator<char> generate_line(char start, int reapt_time) {
    std::string line;
    for (int i = 0; i < reapt_time; i++) {
        co_yield start;
    }
}

generator<char> generate_pyramid(char current = 'A', int level = 1, int max_deep = 5) {
    if (level == max_deep) {
        co_yield generate_line(current, level);
        co_return;
    }

    co_yield generate_line(current, level);
    co_yield '\n';
    co_yield generate_pyramid(current + 1, level + 1, max_deep);
    co_yield '\n';
    co_yield generate_line(current, level);
}

generator<std::string> pattern_a(int depth);
generator<std::string> pattern_b(int depth);

generator<std::string> pattern_a(int depth) {
    if (depth <= 0) co_return;
    co_yield "A" + std::to_string(depth);
    co_yield pattern_b(depth - 1);
}

generator<std::string> pattern_b(int depth) {
    if (depth <= 0) co_return;
    co_yield "B" + std::to_string(depth);
    co_yield pattern_a(depth - 1);
}



void character_genrator() {
    std::cout << "==========" << "character_genrator" << "==========\n";

    std::cout << "--- recursion pyramid ---\n";
    for (auto&& layer : generate_pyramid()) {
        std::cout << layer;
    }

    std::cout << "\n--- mutual calling ---\n";
    for (auto&& item : pattern_a(3) | std::views::take(6)) {
        std::cout << item << "\n";
    }
}