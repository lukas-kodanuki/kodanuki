#include "core/worker.h"

#include "core/frame.h"
#include "core/plugin.h"
#include "core/render.h"
#include "core/step.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace kodanuki {

std::vector<Tile> tilesOf(int width, int height, int size) {
	std::vector<Tile> tiles;
	for (int y = 0; y < height; y += size) {
		for (int x = 0; x < width; x += size) {
			const Tile tile = {
				.x      = x,
				.y      = y,
				.width  = std::min(size, width - x),
				.height = std::min(size, height - y),
			};

			tiles.push_back(tile);
		}
	}
	return tiles;
}

void tracedTile(const Renderer& renderer, const Pass& pass, Worker& worker) {
	while (launchWalks(renderer, pass, worker)) {
		extendRays(renderer, worker);
		arriveAtHits(renderer, worker);
		connectLights(renderer, worker);
		scatterWalks(renderer, worker);
		foldWalks(renderer, pass, worker);
	}
}

PixelWork idlePixels(int capacity) {
	const std::size_t walks = std::size_t(capacity);
	return PixelWork {
		.jobs      = {},
		.slots     = std::vector<PixelSlot>(walks / std::size_t(slotSamples), idleSlot()),
		.colours   = std::vector<Colour>(walks),
		.shared    = std::vector<Colour>(walks),
		.estimates = {},
		.firsts    = {},
		.next      = 0,
	};
}

/**
 * A worker's place before it opens: its backend and its pool size.
 */
struct Seat {
	std::uint32_t backend;
	int capacity;
};

Worker freshWorker(const Renderer& renderer, int index, const Seat& seat, std::uint32_t executor, std::uint32_t transporter) {
	const Tile none = {
		.x      = 0,
		.y      = 0,
		.width  = 0,
		.height = 0,
	};

	return Worker {
		.index       = index,
		.backend     = seat.backend,
		.executor    = executor,
		.transporter = transporter,
		.tile        = none,
		.taken       = 0,
		.pool        = emptyPool(seat.capacity),
		.walks       = renderer.transport.hooks.openWorker(),
		.registers   = std::vector<double>(longestProgram(renderer.file.scene), 0.0),
		.pixels      = idlePixels(seat.capacity),
		.held        = nullptr,
	};
}

std::vector<Seat> openedSeats(const Renderer& renderer) {
	std::vector<Seat> seats;
	for (std::uint32_t backend = 0; backend < renderer.backends.size(); ++backend) {
		const Crew crew = renderer.backends[backend].hooks.openWorkers(renderer);
		seats.insert(seats.end(), std::size_t(crew.workers), Seat { .backend = backend, .capacity = crew.capacity });
	}
	return seats;
}

void closedWorkers(const Renderer& renderer) {
	for (const Plugin& backend : renderer.backends) {
		if (backend.hooks.closeWorkers) {
			backend.hooks.closeWorkers(renderer);
		}
	}
}

void onWorkers(Renderer& renderer, const Pass& pass) {
	const Renderer& reading         = renderer;
	const std::vector<Tile>& tiles  = renderer.drawing.tiles;
	const std::uint32_t executor    = pluginIndex(renderer.execution.name);
	const std::uint32_t transporter = pluginIndex(renderer.transport.name);
	const double begun              = frameStepBegun(renderer.observing, LoopStep::Pass, corePlugin, std::uint32_t(tiles.size()));
	const std::vector<Seat> seats   = openedSeats(renderer);
	std::atomic<std::size_t> next   = 0;
	std::mutex placing;
	const auto run = [&](int index) {
		Worker worker = freshWorker(reading, index, seats[std::size_t(index)], executor, transporter);
		for (std::size_t k = next++; k < tiles.size(); k = next++) {
			worker.tile  = tiles[k];
			worker.taken = k;
			openTile(reading, pass, worker);
			tracedTile(reading, pass, worker);
			placing.lock();
			placeTile(renderer, pass, worker);
			placing.unlock();
		}
	};

	renderer.fallback.hooks.hostWorkers(reading, int(seats.size()), run);
	closedWorkers(renderer);
	frameStepEnded(renderer.observing, LoopStep::Pass, corePlugin, std::uint32_t(tiles.size()), begun);
}

void blockChanged(const Renderer& renderer, const Block& block) {
	for (const Plugin& backend : renderer.backends) {
		if (backend.hooks.uploadBlock) {
			backend.hooks.uploadBlock(renderer, block);
		}
	}
}

} // namespace kodanuki
