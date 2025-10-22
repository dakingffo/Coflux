#include <coflux/task.hpp>
#include <coflux/combiner.hpp>
#include <string>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <random>

using work_executor = coflux::thread_pool_executor<>;
using scheduler = coflux::scheduler<work_executor, coflux::timer_executor>;
using std::chrono::milliseconds;
coflux::fork<std::pair<int, int>, work_executor> horse(auto&&, int id, std::mt19937& mt) {
	int time = mt() % 2000 + 500;
	co_await std::chrono::milliseconds(time);
	std::cout << "horse" << id << " has reached the finish line!\n";
	co_return std::pair{ time, id };
}

void horse_race() {
	std::cout << "==========" << "horse_race" << "==========\n";
	auto env = coflux::make_environment(scheduler{});
	auto task = [](auto&&)->coflux::task<void, work_executor, scheduler> {
		std::random_device rd;
		std::mt19937 mt(rd());
		std::pair<int, int> score[4]{};
		std::tie(score[0], score[1], score[2], score[3]) = co_await coflux::when_all(
			horse(co_await coflux::context(), 1, mt),
			horse(co_await coflux::context(), 2, mt),
			horse(co_await coflux::context(), 3, mt),
			horse(co_await coflux::context(), 4, mt)
		);
		std::sort(score, score + 4);
		std::endl(std::cout);
		for (auto& s : score) {
			std::cout << "horse" << s.second << " : " << s.first << '\n';
		}
		}(env);
}