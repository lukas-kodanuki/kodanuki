#pragma once

#include "core/worker.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

struct Plugin;

/**
 * A step of the render loop, as the observers are told of it.
 *
 * The frame's steps run on the frame's thread, one after another:
 * NextFrame, BuildWorld, PrepareFrame, DrawFrame, ReconstructFrame,
 * DeliverFrame. Evaluate runs inside NextFrame; Preparations, Pass,
 * Surfaces, Reconstruction and Keeping run inside DrawFrame. The worker's steps run on a worker
 * inside a Pass, once per tile (OpenTile, PlaceTile) or once per batch.
 */
enum class LoopStep : std::uint8_t {
	/**
	 * The Source plugins set up the next frame.
	 */
	NextFrame,
	/**
	 * The scene file is evaluated again at the frame's time (sceneAt()).
	 */
	Evaluate,
	/**
	 * The scene is built into geometry, where it changed.
	 */
	BuildWorld,
	/**
	 * The frame's place in its sequence is set.
	 */
	PrepareFrame,
	/**
	 * The frame is drawn: its preparations, passes and reconstruction;
	 * its items are the tiles its pixel passes place (frameTiles()).
	 */
	DrawFrame,
	/**
	 * The drawn frame becomes the shown image.
	 */
	ReconstructFrame,
	/**
	 * The Sink plugins hand the shown frame out.
	 */
	DeliverFrame,
	/**
	 * Inside DrawFrame: the frame's preparations.
	 */
	Preparations,
	/**
	 * Inside DrawFrame: a pass over tiles on the workers (onWorkers);
	 * its items are the tiles.
	 */
	Pass,
	/**
	 * Inside DrawFrame: the surfaces the reconstruction reads.
	 */
	Surfaces,
	/**
	 * Inside DrawFrame: the reconstruction of a frame of a sequence.
	 */
	Reconstruction,
	/**
	 * Inside DrawFrame: what a frame of a sequence keeps for the next.
	 */
	Keeping,
	/**
	 * A worker sets what its tile owes; its items are the tile's pixels.
	 */
	OpenTile,
	/**
	 * A worker starts walks.
	 */
	LaunchWalks,
	/**
	 * A worker's rays get their hits; its items are the rays.
	 */
	ExtendRays,
	/**
	 * A worker's walks arrive at the hits of the rays they flew along;
	 * its items are the walks.
	 */
	ArriveAtHits,
	/**
	 * A worker's walks take the answers to their lines to the lights;
	 * its items are the walks.
	 */
	ConnectLights,
	/**
	 * A worker's walks whose lines are spent draw their next rays or
	 * end; its items are the walks.
	 */
	ScatterWalks,
	/**
	 * A worker folds its ended walks.
	 */
	FoldWalks,
	/**
	 * A worker puts its tile into the frame, under the pass's lock; its
	 * items are the tile's pixels.
	 */
	PlaceTile,
};

/**
 * How many steps there are.
 */
constexpr int stepCount = int(LoopStep::PlaceTile) + 1;

/**
 * The plugin of a step the core carries out itself.
 */
constexpr std::uint32_t corePlugin = 0xffffffff;

/**
 * What an observer is told of one step.
 */
struct StepEvent {
	/**
	 * The step.
	 */
	LoopStep step;
	/**
	 * The plugin whose hook ran it, as its place in everyPlugin(); or
	 * corePlugin.
	 */
	std::uint32_t plugin;
	/**
	 * The worker that ran it; -1 for a step of the frame.
	 */
	int worker;
	/**
	 * How many items it took, as the step says.
	 */
	std::uint32_t items;
	/**
	 * How long it took; 0 where it has only begun.
	 */
	double seconds;
	/**
	 * Whether it is done; false where a frame's step has only begun.
	 */
	bool done;
};

/**
 * A count a plugin publishes from its state.
 */
struct Counter {
	/**
	 * Its name, as `rays.camera`.
	 */
	std::string name;
	/**
	 * Its value, counted since the run began.
	 */
	double value;
};

/**
 * One hook that is told of steps, with the state of the plugin it
 * belongs to.
 */
struct Watcher {
	/**
	 * The hook.
	 */
	void (*observed)(void* state, const StepEvent& event);
	/**
	 * The plugin's state, which only the plugin's accessor casts.
	 */
	void* state;
};

/**
 * Who is told of steps.
 *
 * A step takes a time stamp only where someone is told of it: a
 * frame's step where frames is not empty, a worker's step where
 * workers is not empty, PlaceTile where either is not.
 */
struct Observing {
	/**
	 * The observed hooks, told of the frame's steps on the frame's
	 * thread, as they begin (done false) and end; and of PlaceTile on
	 * the worker's thread, under the pass's lock.
	 */
	std::vector<Watcher> frames;
	/**
	 * The tallied hooks, told of every worker's step on the worker's
	 * thread as it ends, without a lock: each keeps a tally by worker.
	 */
	std::vector<Watcher> workers;
};

/**
 * The seconds each step of the frame took, added up from the steps a
 * watcher was told of.
 */
struct StepSeconds {
	/**
	 * The seconds, by step.
	 */
	std::array<double, stepCount> seconds;
};

/**
 * A plugin's C++ kernel of a worker's step, the hook `backend/cpu`
 * runs.
 */
using Kernel = void (*)(const Renderer& renderer, Worker& worker);

/**
 * Return the plugin's C++ kernel of a worker's step: its extendRays,
 * arriveAtHits, connectLights or scatterWalks; null for another step.
 *
 * @param plugin The plugin.
 * @param step The step.
 */
Kernel kernelOf(const Plugin& plugin, LoopStep step);

/**
 * Return an Observing that tells no one.
 */
Observing noObserving();

/**
 * Return the seconds of no step.
 */
StepSeconds noSeconds();

/**
 * Add a frame's step that ended to the seconds; others change nothing.
 *
 * @param seconds The seconds so far.
 * @param event The step.
 */
void addedStep(StepSeconds& seconds, const StepEvent& event);

/**
 * Return the seconds of one step.
 *
 * @param seconds The seconds.
 * @param step The step.
 */
double secondsOf(const StepSeconds& seconds, LoopStep step);

/**
 * Tell the frame's watchers that a frame's step begins and return when
 * it began, in seconds of a steady clock; 0 where no one is told.
 *
 * @param observing Who is told.
 * @param step The step.
 * @param plugin The plugin whose hook runs it, or corePlugin.
 * @param items How many items it takes.
 */
double frameStepBegun(const Observing& observing, LoopStep step, std::uint32_t plugin, std::uint32_t items);

/**
 * Tell the frame's watchers that a frame's step ended.
 *
 * @param observing Who is told.
 * @param step The step.
 * @param plugin The plugin whose hook ran it, or corePlugin.
 * @param items How many items it took.
 * @param begun When it began (frameStepBegun()).
 */
void frameStepEnded(const Observing& observing, LoopStep step, std::uint32_t plugin, std::uint32_t items, double begun);

/**
 * The pass's opening of a tile: sets what the worker's tile owes.
 *
 * @param renderer The renderer.
 * @param pass The pass.
 * @param worker The worker, its tile taken.
 */
void openTile(const Renderer& renderer, const Pass& pass, Worker& worker);

/**
 * The pass's launch: start walks for what the worker's tile owes and
 * branch the groups that wait.
 *
 * It returns false when the tile owes nothing and no walk is alive.
 *
 * @param renderer The renderer.
 * @param pass The pass.
 * @param worker The worker.
 */
bool launchWalks(const Renderer& renderer, const Pass& pass, Worker& worker);

/**
 * The Execution plugin's step: every waiting ray of the worker's pool
 * gets its hit and is marked answered.
 *
 * @param renderer The renderer, with the Execution plugin.
 * @param worker The worker.
 */
void extendRays(const Renderer& renderer, Worker& worker);

/**
 * The Transport plugin's step of arrival (its arriveAtHits): every
 * answered slot whose walk is Flying or Leading takes its hit. A miss
 * weighs the sky and ends the walk; a hit becomes the vertex, what
 * glows there is weighed and the lines to the lights are chosen, so the
 * walk is Lighting or, with no line, Scattering. A Leading walk's
 * answer goes to its lead (the light subpath of strategy/connect and
 * the light pass). The slot is given the walk's phase and next ray. A
 * Probing slot is left answered for its pass's fold. The step reports
 * how many slots it takes.
 *
 * @param renderer The renderer, with the Transport plugin.
 * @param worker The worker.
 */
void arriveAtHits(const Renderer& renderer, Worker& worker);

/**
 * The Transport plugin's step to the lights (its connectLights): every
 * answered slot whose walk is Lighting takes its line's answer, which
 * adds the line's light or steps the line past a shape it passes. The
 * walk waits on its next line, or is Scattering after the last. A walk
 * that arrived in this batch waits on its first line and is not taken.
 *
 * @param renderer The renderer, with the Transport plugin.
 * @param worker The worker.
 */
void connectLights(const Renderer& renderer, Worker& worker);

/**
 * The Transport plugin's step onward (its scatterWalks): every slot
 * whose walk is Scattering draws its next ray and is Flying, or waits
 * to be branched, or is done.
 *
 * @param renderer The renderer, with the Transport plugin.
 * @param worker The worker.
 */
void scatterWalks(const Renderer& renderer, Worker& worker);

/**
 * The pass's fold: the walks that are done go into what the pass
 * gathers and their slots are freed.
 *
 * @param renderer The renderer.
 * @param pass The pass.
 * @param worker The worker.
 */
void foldWalks(const Renderer& renderer, const Pass& pass, Worker& worker);

/**
 * The pass's placing of a finished tile into the frame; the caller
 * holds the pass's lock.
 *
 * @param renderer The renderer.
 * @param pass The pass.
 * @param worker The worker, its tile done.
 */
void placeTile(Renderer& renderer, const Pass& pass, const Worker& worker);

} // namespace kodanuki
