#include <coflux/task.hpp>
#include <coflux/combiner.hpp>
#include <string>
#include <chrono>
#include <iostream>
#include <random>

using scheduler = coflux::scheduler<coflux::noop_executor>;

coflux::fork<void, coflux::noop_executor> player(auto&&, std::string name, coflux::channel<int[]>& ball, std::mt19937& mt) {
	bool winner = true;
	if (name == "Alice")
		co_await (ball << (mt() % 1000));
	while (ball.active()) {
		int get;
		co_await (ball >> get);
		int num = mt() % 1000;
		if (num > get / 10) {
			std::cout << (num % 2 ? "ping!\n" : "pong!\n");
			co_await (ball << (mt() % 1000));
		}
		else {
			winner = false;
			std::cout << "miss!" << name << " lose!\n";
			ball.close();
		}
	}
	if (winner) {
		std::cout << name << " Win!";
	}
}

void ping_pong_game() {
	std::cout << "==========" << "ping_pong_game" << "==========\n";
	auto env = coflux::make_environment(scheduler{});
	auto task = [](auto&&)->coflux::task<void, coflux::noop_executor, scheduler> {
		std::random_device rd;
		std::mt19937 mt(rd());
		coflux::channel<int[]> ball;
		player(co_await coflux::this_task::environment(), "Alice", ball, mt);
		player(co_await coflux::this_task::environment(), "Bob", ball, mt);
		}(env);
}