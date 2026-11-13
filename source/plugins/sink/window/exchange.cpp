#include "plugins/sink/window/exchange.h"

#include "core/render.h"
#include "shared/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace kodanuki::viewer {

Published emptyPublished() {
	const Camera none = {
		.eye      = { 0, 0, 0 },
		.target   = { 0, 0, 1 },
		.up       = { 0, 0, 1 },
		.fov      = 1,
		.aperture = 0,
		.focus    = 0,
	};

	return Published {
		.number  = 0,
		.width   = 0,
		.height  = 0,
		.rgb     = {},
		.samples = 0,
		.seconds = 0,
		.steps   = noSeconds(),
		.grading = 0,
		.error   = {},
		.camera  = none,
		.builds  = 0,
		.times   = 0,
	};
}

Exchange freshExchange(const Camera& camera) {
	return Exchange {
		.mutex     = {},
		.wake      = {},
		.camera    = camera,
		.moved     = false,
		.flown     = false,
		.played    = 0,
		.advanced  = false,
		.rate      = 24,
		.playing   = false,
		.reload    = false,
		.quit      = false,
		.frame     = emptyPublished(),
		.failed    = {},
		.took      = noSeconds(),
		.builds    = 0,
		.times     = 0,
		.shownTime = 0,
	};
}

void wantedCamera(Exchange& exchange, const Camera& camera) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	exchange.camera = camera;
	exchange.moved  = true;
	exchange.flown  = true;
	exchange.wake.notify_one();
}

void playedTo(Exchange& exchange, double played) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	const bool advanced = std::floor(played * exchange.rate + 1e-6) != std::floor(exchange.played * exchange.rate + 1e-6);
	exchange.played     = played;
	if (advanced) {
		exchange.advanced = true;
		exchange.wake.notify_one();
	}
}

double playedTime(Exchange& exchange) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	return exchange.played;
}

void requestedReload(Exchange& exchange) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	exchange.reload = true;
	exchange.wake.notify_one();
}

Published newestFrame(Exchange& exchange, std::uint64_t seen) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	if (exchange.frame.number <= seen) {
		return emptyPublished();
	}
	return exchange.frame;
}

void publishedFrame(Exchange& exchange, const Published& frame) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	const std::uint64_t number = exchange.frame.number + 1;
	exchange.frame             = frame;
	exchange.frame.number      = number;
}

Order takenOrder(Exchange& exchange, bool resting) {
	std::unique_lock<std::mutex> lock(exchange.mutex);
	const auto wished = [&]() {
		return !resting || exchange.moved || exchange.advanced || exchange.reload || exchange.quit;
	};

	exchange.wake.wait(lock, wished);

	const Order order = {
		.camera = exchange.camera,
		.flown  = exchange.flown,
		.reload = exchange.reload,
		.quit   = exchange.quit,
	};

	exchange.moved    = false;
	exchange.advanced = false;
	exchange.reload   = false;
	return order;
}

void gradedPart(const GradedPart& part, std::size_t first, std::size_t last) {
	for (std::size_t i = first; i < last; ++i) {
		part.bytes[i] = encodedByte(part.image.rgb[i], part.grade);
	}
}

std::vector<unsigned char> gradedBytes(const Image& image, const Grade& grade, int threads) {
	std::vector<unsigned char> bytes(image.rgb.size());
	const std::size_t count = bytes.size();
	const std::size_t parts = std::size_t(std::max(1, threads));

	const GradedPart part = {
		.image = image,
		.grade = grade,
		.bytes = bytes,
	};

	inParallel(threads, parts, [&](std::size_t k) {
		gradedPart(part, count * k / parts, count * (k + 1) / parts);
	});
	return bytes;
}

void quitDrawing(Exchange& exchange) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	exchange.quit = true;
	exchange.wake.notify_one();
}

void stoppedDrawing(Exchange& exchange, std::thread& thread) {
	quitDrawing(exchange);
	thread.join();
}

Exchange& stateExchange(void* state) {
	return *static_cast<Exchange*>(state);
}

Exchange& windowExchange(const Renderer& renderer) {
	return stateExchange(stateOf(renderer, "sink/window"));
}

}
