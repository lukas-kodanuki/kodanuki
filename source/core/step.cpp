#include "core/step.h"

#include "core/frame.h"
#include "core/plugin.h"
#include "core/render.h"

#include <chrono>

namespace kodanuki {

Observing noObserving() {
	return Observing {
		.frames  = {},
		.workers = {},
	};
}

StepSeconds noSeconds() {
	StepSeconds none = {
		.seconds = {},
	};

	none.seconds.fill(0);
	return none;
}

void addedStep(StepSeconds& seconds, const StepEvent& event) {
	if (event.done && event.worker < 0) {
		seconds.seconds[std::size_t(event.step)] += event.seconds;
	}
}

double secondsOf(const StepSeconds& seconds, LoopStep step) {
	return seconds.seconds[std::size_t(step)];
}

Kernel kernelOf(const Plugin& plugin, LoopStep step) {
	switch (step) {
		case LoopStep::ExtendRays: return plugin.hooks.extendRays;
		case LoopStep::ArriveAtHits: return plugin.hooks.arriveAtHits;
		case LoopStep::ConnectLights: return plugin.hooks.connectLights;
		case LoopStep::ScatterWalks: return plugin.hooks.scatterWalks;
		default: return nullptr;
	}
}

void ranKernel(const Renderer& renderer, Worker& worker, const Plugin& plugin, LoopStep step) {
	const Hooks& backend = renderer.backends[worker.backend].hooks;
	if (backend.ranStep == nullptr || !backend.ranStep(renderer, worker, plugin, step)) {
		renderer.fallback.hooks.ranStep(renderer, worker, plugin, step);
	}
}

double clockSeconds() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void told(const std::vector<Watcher>& watchers, const StepEvent& event) {
	for (const Watcher& watcher : watchers) {
		watcher.observed(watcher.state, event);
	}
}

StepEvent stepEvent(LoopStep step, std::uint32_t plugin, int worker, std::uint32_t items) {
	return StepEvent {
		.step    = step,
		.plugin  = plugin,
		.worker  = worker,
		.items   = items,
		.seconds = 0,
		.done    = false,
	};
}

double frameStepBegun(const Observing& observing, LoopStep step, std::uint32_t plugin, std::uint32_t items) {
	if (observing.frames.empty()) {
		return 0;
	}
	told(observing.frames, stepEvent(step, plugin, -1, items));
	return clockSeconds();
}

void frameStepEnded(const Observing& observing, LoopStep step, std::uint32_t plugin, std::uint32_t items, double begun) {
	if (observing.frames.empty()) {
		return;
	}
	StepEvent event = stepEvent(step, plugin, -1, items);
	event.seconds   = clockSeconds() - begun;
	event.done      = true;
	told(observing.frames, event);
}

void workerStepEnded(const Renderer& renderer, const Worker& worker, StepEvent event, double begun) {
	event.worker  = worker.index;
	event.seconds = clockSeconds() - begun;
	event.done    = true;
	told(renderer.observing.workers, event);
}

std::uint32_t tilePixels(const Worker& worker) {
	return std::uint32_t(worker.tile.width * worker.tile.height);
}

void openTile(const Renderer& renderer, const Pass& pass, Worker& worker) {
	if (renderer.observing.workers.empty()) {
		pass.opened(renderer, worker);
		return;
	}
	const double begun = clockSeconds();
	pass.opened(renderer, worker);
	workerStepEnded(renderer, worker, stepEvent(LoopStep::OpenTile, corePlugin, 0, tilePixels(worker)), begun);
}

bool launchWalks(const Renderer& renderer, const Pass& pass, Worker& worker) {
	if (renderer.observing.workers.empty()) {
		return pass.launch(renderer, worker);
	}
	const double begun = clockSeconds();
	const bool alive   = pass.launch(renderer, worker);
	workerStepEnded(renderer, worker, stepEvent(LoopStep::LaunchWalks, corePlugin, 0, 0), begun);
	return alive;
}

std::uint32_t answeredSlots(const Pool& pool) {
	std::uint32_t answered = 0;
	for (const std::uint8_t flag : pool.answered) {
		answered += flag;
	}
	return answered;
}

void extendRays(const Renderer& renderer, Worker& worker) {
	if (renderer.observing.workers.empty()) {
		ranKernel(renderer, worker, renderer.execution, LoopStep::ExtendRays);
		return;
	}
	const double begun = clockSeconds();
	ranKernel(renderer, worker, renderer.execution, LoopStep::ExtendRays);
	workerStepEnded(renderer, worker, stepEvent(LoopStep::ExtendRays, worker.executor, 0, answeredSlots(worker.pool)), begun);
}

bool arrives(const Pool& pool, std::uint32_t slot) {
	return pool.answered[slot] != 0 && (pool.phases[slot] == WalkPhase::Flying || pool.phases[slot] == WalkPhase::Leading);
}

bool connects(const Pool& pool, std::uint32_t slot) {
	return pool.answered[slot] != 0 && pool.phases[slot] == WalkPhase::Lighting;
}

bool scatters(const Pool& pool, std::uint32_t slot) {
	return pool.phases[slot] == WalkPhase::Scattering;
}

std::uint32_t slotsThat(const Pool& pool, bool (*takes)(const Pool& pool, std::uint32_t slot)) {
	std::uint32_t slots = 0;
	for (std::uint32_t slot = 0; slot < std::uint32_t(pool.capacity); ++slot) {
		slots += takes(pool, slot) ? 1 : 0;
	}
	return slots;
}

void arriveAtHits(const Renderer& renderer, Worker& worker) {
	if (renderer.observing.workers.empty()) {
		ranKernel(renderer, worker, renderer.transport, LoopStep::ArriveAtHits);
		return;
	}
	const std::uint32_t walks = slotsThat(worker.pool, arrives);
	const double begun        = clockSeconds();
	ranKernel(renderer, worker, renderer.transport, LoopStep::ArriveAtHits);
	workerStepEnded(renderer, worker, stepEvent(LoopStep::ArriveAtHits, worker.transporter, 0, walks), begun);
}

void connectLights(const Renderer& renderer, Worker& worker) {
	if (renderer.observing.workers.empty()) {
		ranKernel(renderer, worker, renderer.transport, LoopStep::ConnectLights);
		return;
	}
	const std::uint32_t walks = slotsThat(worker.pool, connects);
	const double begun        = clockSeconds();
	ranKernel(renderer, worker, renderer.transport, LoopStep::ConnectLights);
	workerStepEnded(renderer, worker, stepEvent(LoopStep::ConnectLights, worker.transporter, 0, walks), begun);
}

void scatterWalks(const Renderer& renderer, Worker& worker) {
	if (renderer.observing.workers.empty()) {
		ranKernel(renderer, worker, renderer.transport, LoopStep::ScatterWalks);
		return;
	}
	const std::uint32_t walks = slotsThat(worker.pool, scatters);
	const double begun        = clockSeconds();
	ranKernel(renderer, worker, renderer.transport, LoopStep::ScatterWalks);
	workerStepEnded(renderer, worker, stepEvent(LoopStep::ScatterWalks, worker.transporter, 0, walks), begun);
}

void foldWalks(const Renderer& renderer, const Pass& pass, Worker& worker) {
	if (renderer.observing.workers.empty()) {
		pass.fold(renderer, worker);
		return;
	}
	const double begun = clockSeconds();
	pass.fold(renderer, worker);
	workerStepEnded(renderer, worker, stepEvent(LoopStep::FoldWalks, corePlugin, 0, 0), begun);
}

void placeTile(Renderer& renderer, const Pass& pass, const Worker& worker) {
	const Observing& observing = renderer.observing;
	if (observing.frames.empty() && observing.workers.empty()) {
		pass.placed(renderer, worker);
		return;
	}
	const double begun = clockSeconds();
	pass.placed(renderer, worker);
	StepEvent event = stepEvent(LoopStep::PlaceTile, corePlugin, worker.index, tilePixels(worker));
	event.seconds   = clockSeconds() - begun;
	event.done      = true;
	told(observing.frames, event);
	told(observing.workers, event);
}

} // namespace kodanuki
