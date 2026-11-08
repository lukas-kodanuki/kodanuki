#include "extras/strategy_correct_join/pass.h"

#include "image/image.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

namespace kodanuki {

constexpr double lightConfidence = 1.96;

struct ChunkRun {
	std::uint32_t first;
	std::uint32_t last;
};

struct FoldQueue {
	std::vector<std::vector<Splat>> waiting;
	std::vector<char> ready;
	std::uint32_t folded;
};

CarriedLight noCarriedLight() {
	return CarriedLight {
		.sums    = {},
		.squares = {},
		.paths   = 0,
	};
}

std::uint32_t chunksFor(double share, int samples, std::size_t pixels) {
	const double paths = share * double(samples) * double(pixels);
	return std::max(1u, std::uint32_t(std::ceil(paths / chunkPaths)));
}

void foldedSplats(CarriedLight& light, const std::vector<Splat>& splats) {
	const bool squared = !light.squares.empty();
	for (const Splat& splat : splats) {
		light.sums[splat.pixel] = light.sums[splat.pixel] + splat.colour;
		if (squared) {
			light.squares[splat.pixel] = light.squares[splat.pixel] + splat.colour * splat.colour;
		}
	}
	light.paths += chunkPaths;
}

void foldedInOrder(CarriedLight& light, FoldQueue& queue) {
	while (queue.folded < queue.ready.size() && queue.ready[queue.folded] != 0) {
		foldedSplats(light, queue.waiting[queue.folded]);
		queue.waiting[queue.folded] = {};
		queue.folded += 1;
	}
}

std::vector<Splat> chunkSplats(const LightPassQuery& query, std::uint32_t chunk) {
	const Execution execution = {
		.member  = query.execution,
		.tracing = query.tracing,
	};

	const LightJob job = {
		.first = chunk * chunkPaths,
		.count = chunkPaths,
	};

	return executedLightJob(execution, job);
}

void tracedChunks(const LightPassQuery& query, const ChunkRun& run, CarriedLight& light) {
	FoldQueue queue = {
		.waiting = std::vector<std::vector<Splat>>(run.last - run.first),
		.ready   = std::vector<char>(run.last - run.first, 0),
		.folded  = 0,
	};

	std::mutex fold;
	std::atomic<std::uint32_t> next = run.first;
	const auto work                 = [&]() {
		for (std::uint32_t chunk = next++; chunk < run.last; chunk = next++) {
			std::vector<Splat> splats = chunkSplats(query, chunk);
			const std::lock_guard<std::mutex> lock(fold);
			queue.waiting[chunk - run.first] = std::move(splats);
			queue.ready[chunk - run.first]   = 1;
			foldedInOrder(light, queue);
		}
	};

	std::vector<std::thread> threads;
	for (int k = 0; k < query.threads; ++k) {
		threads.emplace_back(work);
	}
	for (std::thread& thread : threads) {
		thread.join();
	}
}

bool inRounds(const Stopping& stopping) {
	return stopping.member == StoppingMember::ConfidenceInterval && stopping.tolerance > 0;
}

bool keepsSpread(const Stopping& stopping) {
	return inRounds(stopping) || trialSamples(stopping) > 0;
}

bool tracesLight(const Tracing& tracing) {
	return strategyOn(tracing.strategies, StrategyMember::Join) && tracing.strategies.joinShare > 0 && !tracing.strategies.lampShares.empty();
}

CarriedLight carriedLight(const LightPassQuery& query) {
	const Tracing& tracing = query.tracing;
	if (!tracesLight(tracing)) {
		return noCarriedLight();
	}
	const Stopping& stopping  = query.stopping;
	const std::size_t pixels  = std::size_t(tracing.lens.width) * std::size_t(tracing.lens.height);
	const std::uint32_t round = chunksFor(tracing.strategies.joinShare, stopping.batch, pixels);
	const std::uint32_t most  = std::max(round, chunksFor(tracing.strategies.joinShare, stopping.samples, pixels));
	const bool rounds         = inRounds(stopping);

	CarriedLight light = {
		.sums    = std::vector<Colour>(pixels, Colour { 0, 0, 0 }),
		.squares = keepsSpread(stopping) ? std::vector<Colour>(pixels, Colour { 0, 0, 0 }) : std::vector<Colour> {},
		.paths   = 0,
	};

	ChunkRun run = {
		.first = 0,
		.last  = rounds ? round : most,
	};

	while (true) {
		tracedChunks(query, run, light);
		if (run.last >= most || lightSettled(light, stopping)) {
			return light;
		}
		run.first = run.last;
		run.last  = std::min(run.last + round, most);
	}
}

Colour carriedMean(const CarriedLight& light, std::size_t pixel) {
	if (light.paths == 0) {
		return { 0, 0, 0 };
	}
	return light.sums[pixel] * (1.0 / double(light.paths));
}

double spreadOf(double mean, double second, double count) {
	return std::max(second - mean * mean, 0.0) / count;
}

Colour carriedSpread(const CarriedLight& light, std::size_t pixel) {
	if (light.paths == 0 || light.squares.empty()) {
		return { 0, 0, 0 };
	}
	const double count  = double(light.paths);
	const Colour mean   = carriedMean(light, pixel);
	const Colour second = light.squares[pixel] * (1.0 / count);
	return Colour {
		.red   = spreadOf(mean.red, second.red, count),
		.green = spreadOf(mean.green, second.green, count),
		.blue  = spreadOf(mean.blue, second.blue, count),
	};
}

bool channelKnown(const Stopping& stopping, double mean, double spread) {
	const double width = lightConfidence * std::sqrt(spread);
	return stepsBetween(mean - width, mean + width, stopping.grade) <= 2 * stopping.tolerance;
}

bool pixelKnown(const CarriedLight& light, const Stopping& stopping, std::size_t pixel) {
	const Colour& sum = light.sums[pixel];
	if (sum.red == 0 && sum.green == 0 && sum.blue == 0) {
		return true;
	}
	const Colour mean   = carriedMean(light, pixel);
	const Colour spread = carriedSpread(light, pixel);
	return channelKnown(stopping, mean.red, spread.red) && channelKnown(stopping, mean.green, spread.green) && channelKnown(stopping, mean.blue, spread.blue);
}

bool lightSettled(const CarriedLight& light, const Stopping& stopping) {
	if (!inRounds(stopping)) {
		return false;
	}
	for (std::size_t pixel = 0; pixel < light.sums.size(); ++pixel) {
		if (!pixelKnown(light, stopping, pixel)) {
			return false;
		}
	}
	return true;
}

}
