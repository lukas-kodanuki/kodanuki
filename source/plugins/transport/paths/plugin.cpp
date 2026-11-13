#include "plugins/transport/paths/plugin.h"

#include "core/block.h"
#include "core/render.h"
#include "plugins/transport/paths/paths.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace kodanuki {

static_assert(std::is_trivially_copyable_v<Finish>, "a block's element is plain data");
static_assert(std::is_trivially_copyable_v<Lamp>, "a block's element is plain data");
static_assert(std::is_trivially_copyable_v<GradientSky>, "a block's element is plain data");

struct PathsCounts {
	std::atomic<std::uint64_t> arrivals;
	std::atomic<std::uint64_t> lines;
};

PathsCounts& pathsCounts() {
	static PathsCounts counts = {
		.arrivals = 0,
		.lines    = 0,
	};

	return counts;
}

State* pathsState(Renderer& renderer) {
	for (State& state : renderer.states.held) {
		if (state.name == "transport/paths") {
			return &state;
		}
	}
	renderer.states.held.push_back(State { .name = "transport/paths", .data = nullptr });
	return &renderer.states.held.back();
}

void pathsWorldOpened(Renderer& renderer) {
	State* state = pathsState(renderer);
	state->data  = nullptr;
	state->data  = std::make_shared<PathsWorld>(pathsWorldOf(renderer.file, renderer.options.choices));
}

Tracing frameTracing(const TransportFrame& frame, const PathsFrame& opened) {
	const PathsWorld& world = pathsWorld(frame.world);
	return Tracing {
		.geometry   = frame.geometry,
		.scene      = frame.geometry.scene,
		.looks      = frame.looks,
		.materials  = opened.materials,
		.lights     = opened.lights,
		.lens       = frame.lens,
		.sampling   = opened.sampling,
		.strategies = opened.strategies,
		.features   = world.features,
		.limits     = transportLimits(frame.shot.render),
		.seed       = std::uint64_t(frame.shot.render.seed),
		.film       = frame.film,
		.recording  = frame.recording,
	};
}

std::shared_ptr<void> pathsFrameOpened(const TransportFrame& frame) {
	const PathsWorld& world = pathsWorld(frame.world);
	const Sampling sampling = preparedSampling(world.sampling, frame.states);
	const Lights lights     = lightsOf(frame.lighting);

	const PathsFrame start = {
		.materials  = featuredMaterials(world.features, materialsOf(frame.looks, world.smoothGlass), world.smoothGlass),
		.lights     = lights,
		.sampling   = sampling,
		.strategies = preparedStrategies(world.strategies, lights, sampling),
		.tracing    = std::nullopt,
	};

	std::shared_ptr<PathsFrame> opened = std::make_shared<PathsFrame>(start);
	opened->tracing.emplace(frameTracing(frame, *opened));
	return opened;
}

std::shared_ptr<void> pathsWorkerOpened() {
	std::shared_ptr<PathsWorker> held = std::make_shared_for_overwrite<PathsWorker>();
	for (std::uint32_t slot = 0; slot < std::uint32_t(poolCapacity); ++slot) {
		guideForgotten(held->walks[slot].companions.guide);
		held->walks[slot].lead = slot;
		held->walks[slot].led  = false;
	}
	return held;
}

Answer takenAnswer(Worker& worker, std::uint32_t slot) {
	worker.pool.answered[slot] = 0;
	return Answer {
		.hit       = worker.pool.hits[slot],
		.registers = worker.registers,
		.leads     = leadsOf(worker),
	};
}

void pathsArrived(const Renderer& renderer, Worker& worker) {
	const Tracing& tracing = tracingOf(renderer.drawing.transported.get());
	Walk* walks            = walksOf(worker);
	const Pool& pool       = worker.pool;
	std::uint64_t arrived  = 0;
	for (std::uint32_t slot = 0; slot < std::uint32_t(pool.capacity); ++slot) {
		if (pool.answered[slot] != 0 && (pool.phases[slot] == WalkPhase::Flying || pool.phases[slot] == WalkPhase::Leading)) {
			walkArrived(tracing, walks[slot], takenAnswer(worker, slot));
			waitingSlot(tracing, slot, worker);
			arrived += 1;
		}
	}
	pathsCounts().arrivals.fetch_add(arrived, std::memory_order_relaxed);
}

void pathsConnected(const Renderer& renderer, Worker& worker) {
	const Tracing& tracing = tracingOf(renderer.drawing.transported.get());
	Walk* walks            = walksOf(worker);
	const Pool& pool       = worker.pool;
	std::uint64_t answered = 0;
	for (std::uint32_t slot = 0; slot < std::uint32_t(pool.capacity); ++slot) {
		if (pool.answered[slot] != 0 && pool.phases[slot] == WalkPhase::Lighting) {
			lineAnswered(tracing, walks[slot], takenAnswer(worker, slot));
			waitingSlot(tracing, slot, worker);
			answered += 1;
		}
	}
	pathsCounts().lines.fetch_add(answered, std::memory_order_relaxed);
}

void pathsScattered(const Renderer& renderer, Worker& worker) {
	const Tracing& tracing = tracingOf(renderer.drawing.transported.get());
	Walk* walks            = walksOf(worker);
	const Pool& pool       = worker.pool;
	for (std::uint32_t slot = 0; slot < std::uint32_t(pool.capacity); ++slot) {
		if (pool.phases[slot] == WalkPhase::Scattering) {
			scatteredFrom(tracing, walks[slot]);
			waitingSlot(tracing, slot, worker);
		}
	}
}

void pathsStarted(const Renderer& renderer, Worker& worker, std::uint32_t slot) {
	const Tracing& tracing = tracingOf(renderer.drawing.transported.get());
	walkRestarted(tracing, worker.pool.samples[slot], walksOf(worker)[slot], leadsOf(worker));
	waitingSlot(tracing, slot, worker);
}

Colour pathsBranched(const Renderer& renderer, Worker& worker, std::uint32_t slot) {
	const Tracing& tracing       = tracingOf(renderer.drawing.transported.get());
	Walk* walks                  = walksOf(worker);
	const Walk& stem             = walks[slot];
	const std::uint32_t branches = stem.path.numbers.branches;
	const Colour shared          = sharedColour(stem);
	for (std::uint32_t k = branches; k-- > 0;) {
		branchedFrom(tracing, stem, stem.path.numbers.index + k, walks[slot + k], leadsOf(worker));
	}
	for (std::uint32_t k = 0; k < branches; ++k) {
		waitingSlot(tracing, slot + k, worker);
	}
	return shared;
}

Colour pathsColour(const Worker& worker, std::uint32_t slot) {
	return walkColour(walksOf(worker)[slot]);
}

bool pathsWalkBranched(const Worker& worker, std::uint32_t slot) {
	return walksOf(worker)[slot].path.branched;
}

Surface pathsFirstSurface(const Worker& worker, std::uint32_t slot) {
	return walksOf(worker)[slot].first;
}

Surface pathsCameraSurface(const Renderer& renderer, Worker& worker, std::uint32_t slot) {
	const CameraHit met = {
		.ray       = worker.pool.rays[slot],
		.hit       = worker.pool.hits[slot],
		.registers = worker.registers,
	};

	return cameraSurface(tracingOf(renderer.drawing.transported.get()), met);
}

std::vector<Counter> pathsPublished(const Renderer& renderer) {
	const double walks = double(sizeof(PathsWorker)) * double(renderer.options.choices.threads);
	return {
		Counter { .name = "arrivals", .value = double(pathsCounts().arrivals.load()) },
		Counter { .name = "lines", .value = double(pathsCounts().lines.load()) },
		Counter { .name = "bytes.walks", .value = walks },
	};
}

std::vector<Block> pathsBlocks(const Renderer& renderer) {
	if (!renderer.drawing.transported) {
		return {};
	}
	const Tracing& tracing = tracingOf(renderer.drawing.transported.get());
	return {
		Block { .name = "transport/paths.finishes", .data = tracing.materials.finishes.data(), .count = tracing.materials.finishes.size(), .size = sizeof(Finish) },
		Block { .name = "transport/paths.lamps", .data = tracing.lights.lamps.data(), .count = tracing.lights.lamps.size(), .size = sizeof(Lamp) },
		Block { .name = "transport/paths.sky", .data = &tracing.lights.sky, .count = 1, .size = sizeof(GradientSky) },
	};
}

Plugin transportPathsPlugin() {
	Hooks hooks         = noHooks();
	hooks.openWorld     = pathsWorldOpened;
	hooks.openFrame     = pathsFrameOpened;
	hooks.openWorker    = pathsWorkerOpened;
	hooks.arriveAtHits  = pathsArrived;
	hooks.connectLights = pathsConnected;
	hooks.scatterWalks  = pathsScattered;
	hooks.startWalk     = pathsStarted;
	hooks.branchWalks   = pathsBranched;
	hooks.walkColour    = pathsColour;
	hooks.walkBranched  = pathsWalkBranched;
	hooks.firstSurface  = pathsFirstSurface;
	hooks.cameraSurface = pathsCameraSurface;
	hooks.published     = pathsPublished;
	hooks.blocks        = pathsBlocks;
	return Plugin {
		.name   = "transport/paths",
		.point  = Point::Transport,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
