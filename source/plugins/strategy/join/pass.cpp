#include "plugins/strategy/join/pass.h"

#include "core/frame.h"
#include "core/render.h"
#include "core/step.h"
#include "plugins/transport/paths/paths.h"
#include "shared/image.h"

#include <algorithm>
#include <cmath>
#include <memory>

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

struct LightGather {
	CarriedLight& light;
	FoldQueue queue;
};

struct ChunkWork {
	std::vector<FilmLanding> landings;
	std::vector<std::uint32_t> running;
	std::uint32_t next;
};

LightGather& lightGather(const Renderer& renderer) {
	return *static_cast<LightGather*>(renderer.drawing.gathered.get());
}

ChunkWork& chunkWork(const Worker& worker) {
	return *static_cast<ChunkWork*>(worker.held.get());
}

void openedChunk(const Renderer&, Worker& worker) {
	if (!worker.held) {
		const ChunkWork fresh = {
			.landings = std::vector<FilmLanding>(chunkPaths),
			.running  = std::vector<std::uint32_t>(std::size_t(worker.pool.capacity), 0),
			.next     = 0,
		};

		worker.held = std::make_shared<ChunkWork>(fresh);
	}
	chunkWork(worker).next = 0;
}

void takenLightPath(const Renderer& renderer, std::uint32_t slot, Worker& worker) {
	ChunkWork& chunk          = chunkWork(worker);
	const std::uint32_t first = std::uint32_t(worker.tile.x) * chunkPaths;
	Walk& walk                = walksOf(worker)[slot];
	while (chunk.next < chunkPaths) {
		const std::uint32_t path = chunk.next;
		chunk.next += 1;
		lightPathRestarted(tracingOf(renderer.drawing.transported.get()), first + path, walk, leadsOf(worker));
		if (walk.phase == WalkPhase::Done) {
			chunk.landings[path] = leadsOf(worker)[slot].landing;
			continue;
		}
		chunk.running[slot] = path;
		waitingSlot(tracingOf(renderer.drawing.transported.get()), slot, worker);
		return;
	}
}

bool launchLightPaths(const Renderer& renderer, Worker& worker) {
	const Pool& pool = worker.pool;
	bool alive       = false;
	for (std::uint32_t slot = 0; slot < std::uint32_t(pool.capacity); ++slot) {
		if (pool.phases[slot] == WalkPhase::Free) {
			takenLightPath(renderer, slot, worker);
		}
		alive = pool.phases[slot] != WalkPhase::Free || alive;
	}
	return alive;
}

void foldLightPaths(const Renderer&, Worker& worker) {
	ChunkWork& chunk = chunkWork(worker);
	for (std::uint32_t slot = 0; slot < std::uint32_t(worker.pool.capacity); ++slot) {
		if (worker.pool.phases[slot] == WalkPhase::Done) {
			chunk.landings[chunk.running[slot]] = leadsOf(worker)[slot].landing;
			worker.pool.phases[slot]            = WalkPhase::Free;
		}
	}
}

std::vector<Splat> chunkSplats(const ChunkWork& chunk) {
	std::vector<Splat> splats;
	for (const FilmLanding& landing : chunk.landings) {
		splats.insert(splats.end(), landing.splats.begin(), landing.splats.begin() + landing.count);
	}
	return splats;
}

void placedChunk(Renderer& renderer, const Worker& worker) {
	LightGather& gather                = lightGather(renderer);
	gather.queue.waiting[worker.taken] = chunkSplats(chunkWork(worker));
	gather.queue.ready[worker.taken]   = 1;
	foldedInOrder(gather.light, gather.queue);
}

Pass lightPass() {
	return Pass {
		.opened = openedChunk,
		.launch = launchLightPaths,
		.fold   = foldLightPaths,
		.placed = placedChunk,
	};
}

std::vector<Tile> chunkTiles(const ChunkRun& run) {
	std::vector<Tile> tiles;
	for (std::uint32_t chunk = run.first; chunk < run.last; ++chunk) {
		const Tile tile = {
			.x      = int(chunk),
			.y      = 0,
			.width  = int(chunkPaths),
			.height = 1,
		};

		tiles.push_back(tile);
	}
	return tiles;
}

void tracedChunks(Renderer& renderer, const ChunkRun& run, CarriedLight& light) {
	const std::vector<Tile> tiles = chunkTiles(run);

	const FoldQueue queue = {
		.waiting = std::vector<std::vector<Splat>>(tiles.size()),
		.ready   = std::vector<char>(tiles.size(), 0),
		.folded  = 0,
	};

	const LightGather gather = {
		.light = light,
		.queue = queue,
	};

	passedTiles(renderer, lightPass(), tiles, std::make_shared<LightGather>(gather));
}

bool inRounds(const Stopping& stopping) {
	return judgesNoise(stopping);
}

bool keepsSpread(const Stopping& stopping) {
	return inRounds(stopping) || trialSamples(stopping) > 0;
}

bool tracesLight(const Tracing& tracing) {
	return strategyOn(tracing.strategies, StrategyMember::Join) && tracing.strategies.joinShare > 0 && !tracing.strategies.lampShares.empty();
}

CarriedLight carriedLight(Renderer& renderer) {
	const Tracing& tracing = tracingOf(renderer.drawing.transported.get());
	if (!tracesLight(tracing)) {
		return noCarriedLight();
	}
	const Stopping& stopping  = renderer.drawing.stopping;
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
		tracedChunks(renderer, run, light);
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

std::shared_ptr<void> openedCarriedLight() {
	return std::make_shared<CarriedLight>(noCarriedLight());
}

CarriedLight& carriedLightState(States& states) {
	return *static_cast<CarriedLight*>(heldState(states, "strategy/join", openedCarriedLight));
}

} // namespace kodanuki
