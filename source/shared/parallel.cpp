#include "shared/parallel.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace kodanuki {

void inParallel(int threads, std::size_t count, const std::function<void(std::size_t)>& work) {
	std::atomic<std::size_t> next = 0;
	const auto run                = [&]() {
		for (std::size_t i = next++; i < count; i = next++) {
			work(i);
		}
	};
	std::vector<std::thread> helpers;
	for (int k = 1; k < threads; ++k) {
		helpers.emplace_back(run);
	}
	run();
	for (std::thread& helper : helpers) {
		helper.join();
	}
}

}
