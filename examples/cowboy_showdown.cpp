#include <coflux/task.hpp>
#include <coflux/combiner.hpp>
#include <string>
#include <chrono>
#include <iostream>
#include <random>

using scheduler = coflux::scheduler<coflux::new_thread_executor, coflux::timer_executor>;

coflux::fork<std::string, coflux::new_thread_executor> cowboy(auto&&, std::string name, std::mt19937 mt) {
	auto stop_token = co_await coflux::this_fork::get_stop_token();
	co_await std::chrono::milliseconds(mt() % 100);
	if (stop_token.stop_requested()) {
		co_await coflux::this_fork::cancel();
	}
	co_return name + " fire!";
}

void cowboy_showdown() {
	std::cout << "==========" << "cowboy_showdown" << "==========\n";
	auto env = coflux::make_environment(scheduler{});
	auto task = [](auto&&, int times)->coflux::task<void, coflux::new_thread_executor, scheduler> {
		std::random_device rd;
		std::mt19937 mt(rd());
		for (int i = 0; i < times; i++) {
			std::cout << "In paralled world " << i << '\n';
			auto res = co_await coflux::when_any(
				cowboy(co_await coflux::this_task::environment(), "Jack", mt),
				cowboy(co_await coflux::this_task::environment(), "Peter", mt)
			);
			std::visit([](const std::string& s) {
				std::cout << s << "\n\n";
				}, res);
			co_await std::chrono::milliseconds(200);
		}
		}(env, 5);
}