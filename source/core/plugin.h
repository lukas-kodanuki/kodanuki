#pragma once

#include "core/state.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kodanuki {

struct Block;
struct Colour;
struct CountQuery;
struct Counter;
struct Crew;
struct Geometry;
struct Image;
enum class LoopStep : std::uint8_t;
struct ReconstructionQuery;
struct Renderer;
struct StepEvent;
struct Surface;
struct Stopping;
struct TransportFrame;
struct TrialPixel;
struct Plugin;
struct Worker;

/**
 * A point of the render loop that plugins fill.
 */
enum class Point : std::uint8_t {
	/**
	 * The next frame's time and camera, and what changed.
	 */
	Source,
	/**
	 * The scene at a frame's time as geometry rays meet.
	 */
	Geometry,
	/**
	 * The tables a frame reads.
	 */
	Preparation,
	/**
	 * How many samples each pixel of a frame draws.
	 */
	Stopping,
	/**
	 * How a batch of rays is cut into calls that find their hits.
	 */
	Execution,
	/**
	 * What a path does at its hits: arrive, connect to lights, scatter.
	 */
	Transport,
	/**
	 * How a frame's estimates and history become the shown image.
	 */
	Reconstruction,
	/**
	 * Where a finished frame goes.
	 */
	Sink,
	/**
	 * What watches the steps, as the progress line and the metrics.
	 */
	Observer,
	/**
	 * What a worker runs a step on.
	 */
	Backend,
	/**
	 * A member of a point another plugin owns, which that plugin
	 * switches in its own dispatch file; the core calls only its
	 * per-frame hooks (prepareFrame, broughtLight, broughtSpread).
	 */
	Owned,
};

/**
 * What a plugin does to the mean of the image.
 */
enum class Effect : std::uint8_t {
	/**
	 * The mean stays the reference's, or within the scene's stated
	 * tolerance.
	 */
	Exact,
	/**
	 * The mean reaches the reference's in the limit.
	 */
	Consistent,
	/**
	 * The mean changes, in all or in part.
	 */
	Biased,
};

/**
 * What a plugin's work reads of a frame, as flags that combine: what
 * the core reruns a preparation by, and what it records for a
 * reconstruction.
 */
enum class Reads : std::uint8_t {
	/**
	 * Nothing the core keeps track of.
	 */
	Nothing = 0,
	/**
	 * The lights, the sky, the clouds and the stars: a preparation runs
	 * again on a Lights change.
	 */
	Lights = 1,
	/**
	 * The lens: a preparation runs again when the camera moved.
	 */
	Camera = 2,
	/**
	 * Each drawn pixel's first surface, traced for the frame (the
	 * G-buffer, ShownFrame.surfaces).
	 */
	Surfaces = 4,
	/**
	 * What the frame's samples left (ShownFrame.record's light and first
	 * surfaces).
	 */
	Records = 8,
	/**
	 * The materials, coats and groups: a preparation runs again on a
	 * Looks change.
	 */
	Looks = 16,
	/**
	 * The shapes and the tree built of them: a preparation runs again on
	 * a Shapes change.
	 */
	Shapes = 32,
};

/**
 * The steps and hooks a plugin fills, each called once per batch, once
 * per frame or once per run, null where the plugin fills none.
 */
struct Hooks {
	/**
	 * Opens the plugin's state for the run, before the first frame.
	 */
	void (*openRenderer)(Renderer& renderer);
	/**
	 * A Source's step: sets the next frame's time, scene and camera and
	 * what changed; false where no frame follows.
	 */
	bool (*nextFrame)(Renderer& renderer);
	/**
	 * A Transport's hook: keeps what the plugin reads of a loaded scene
	 * as its state, before the Geometry builds the world.
	 */
	void (*openWorld)(Renderer& renderer);
	/**
	 * A Geometry's step: builds the world from the renderer's scene,
	 * keeps it as the plugin's state and returns what rays meet in it,
	 * with the plugin's kernel for one ray.
	 */
	Geometry (*buildWorld)(Renderer& renderer);
	/**
	 * A Transport's hook: returns its state of a frame, which only its
	 * own accessor casts (Renderer.drawing.transported).
	 */
	std::shared_ptr<void> (*openFrame)(const TransportFrame& frame);
	/**
	 * A Transport's hook: returns its fresh state of a worker, its walk
	 * of each slot of the pool (Worker.walks).
	 */
	std::shared_ptr<void> (*openWorker)();
	/**
	 * An Execution's step: gives every waiting ray of the worker's pool
	 * its hit and marks it answered.
	 */
	void (*extendRays)(const Renderer& renderer, Worker& worker);
	/**
	 * A Transport's step: the walks whose ray of flight is answered
	 * arrive at their hits: a hit becomes a vertex, sky and glow are
	 * weighed, the vertex's lines to light are chosen.
	 */
	void (*arriveAtHits)(const Renderer& renderer, Worker& worker);
	/**
	 * A Transport's step: the walks whose line to a light is answered
	 * add its light.
	 */
	void (*connectLights)(const Renderer& renderer, Worker& worker);
	/**
	 * A Transport's step: the walks done with their vertex draw their
	 * next ray, or end.
	 */
	void (*scatterWalks)(const Renderer& renderer, Worker& worker);
	/**
	 * A Transport's hook for a pass's launch: starts the slot's walk
	 * from the slot's sample (Pool.samples) and sets the slot's phase,
	 * kind and ray.
	 */
	void (*startWalk)(const Renderer& renderer, Worker& worker, std::uint32_t slot);
	/**
	 * A Transport's hook for a pass's launch: branches the Branching
	 * walk of the slot into its group, the slot and those after it, one
	 * a sample, and returns the light the group shares.
	 */
	Colour (*branchWalks)(const Renderer& renderer, Worker& worker, std::uint32_t slot);
	/**
	 * A Transport's hook for a pass's fold: returns the light of the
	 * slot's ended walk.
	 */
	Colour (*walkColour)(const Worker& worker, std::uint32_t slot);
	/**
	 * A Transport's hook for a pass's fold: returns whether the slot's
	 * walk was branched from its group's stem.
	 */
	bool (*walkBranched)(const Worker& worker, std::uint32_t slot);
	/**
	 * A Transport's hook for a pass's fold: returns what the slot's
	 * walk's camera ray met first (Renderer.drawing.recording).
	 */
	Surface (*firstSurface)(const Worker& worker, std::uint32_t slot);
	/**
	 * A Transport's hook for the surface pass's fold: returns what the
	 * slot's answered camera ray met.
	 */
	Surface (*cameraSurface)(const Renderer& renderer, Worker& worker, std::uint32_t slot);
	/**
	 * A Sink's step: hands the shown frame out.
	 */
	void (*deliverFrame)(Renderer& renderer, const Image& shown);
	/**
	 * A Sink that must keep the main thread (a window): runs the frames
	 * (runFrames) on a thread of its own while it serves the main
	 * thread, and returns the run's exit code once both are done.
	 */
	int (*hostRenderer)(Renderer& renderer);
	/**
	 * A Sink that plays a clip as it is watched: returns the time of the
	 * clip it shows, in seconds since the clip first started, never
	 * wrapped. A Sink that fills it says the run is interactive, and
	 * `source/scene` loops the clip by it.
	 */
	double (*playedClip)(Renderer& renderer);
	/**
	 * Closes the plugin's state for the run, after the last frame.
	 */
	void (*closeRenderer)(Renderer& renderer);
	/**
	 * Told of the frame's steps as they begin and end, and of every
	 * placed tile (Observing.frames), with the plugin's state.
	 */
	void (*observed)(void* state, const StepEvent& event);
	/**
	 * Told of every worker's step on the worker's thread, without a
	 * lock (Observing.workers), with the plugin's state.
	 */
	void (*tallied)(void* state, const StepEvent& event);
	/**
	 * Returns the plugin's counters, counted since the run began; read
	 * at the end of a frame.
	 */
	std::vector<Counter> (*published)(const Renderer& renderer);
	/**
	 * Returns the flat arrays the plugin hands the kernels of the frame
	 * being drawn (frameBlocks()), for a backend to copy.
	 */
	std::vector<Block> (*blocks)(const Renderer& renderer);
	/**
	 * A preparation: works out the tables the frame reads into the
	 * plugin's state among the kept states (History.states), once per
	 * frame where what it reads changed (Plugin.reads): on every Cut,
	 * and on a Moved, Lights, Looks or Shapes change where it reads the
	 * Camera, Lights, Looks or Shapes. The hooks run in the order the plugins name
	 * (Plugin.after); the Transport's state of the frame they read
	 * (Renderer.drawing.transported) holds the tables the earlier ones filled,
	 * and the scene's stopping is Renderer.drawing.stopping. A hook works out its
	 * state before it replaces the old one, so the transport may read
	 * the state it fills.
	 */
	void (*prepareFrame)(Renderer& renderer);
	/**
	 * What the plugin's preparation brought a pixel directly, beside
	 * its samples; read once per pixel when the frame is shown and when
	 * a trial is judged.
	 */
	Colour (*broughtLight)(States& states, std::size_t pixel);
	/**
	 * The variance of broughtLight(), per channel.
	 */
	Colour (*broughtSpread)(States& states, std::size_t pixel);
	/**
	 * A Stopping's hook: how many samples every pixel's trial draws,
	 * zero for none.
	 */
	int (*trialSamples)(const Stopping& stopping);
	/**
	 * A Stopping's hook that draws a trial: how many samples share a
	 * first vertex in the trials.
	 */
	int (*trialSplit)(const Stopping& stopping);
	/**
	 * A Stopping's hook that draws a trial: how many samples a pixel's
	 * own trial says it needs. A member that fills it judges noise.
	 */
	float (*wantedSamples)(const Stopping& stopping, const TrialPixel& pixel);
	/**
	 * A Stopping's hook: how many samples a pixel draws in all, its
	 * trial included, where trials were drawn.
	 */
	int (*allottedSamples)(const Stopping& stopping, const CountQuery& query);
	/**
	 * A Stopping's hook that draws a trial: how many samples share a
	 * first vertex in a pixel's count pass.
	 */
	int (*allottedSplit)(const Stopping& stopping, const CountQuery& query);
	/**
	 * A Reconstruction's step: the shown image from the image the
	 * plugins before show, the frame and the plugin's state among the
	 * kept states.
	 */
	Image (*reconstructFrame)(States& states, Image shown, const ReconstructionQuery& query);
	/**
	 * Returns the plugins it reads beside the core, the foundation and
	 * the owner of its point, as `geometry/tree`; a run that chooses it
	 * without them is refused.
	 */
	std::vector<std::string> (*needed)();
	/**
	 * A Backend's hook: how many workers it brings to a pass and the
	 * pool size of each.
	 */
	Crew (*openWorkers)(const Renderer& renderer);
	/**
	 * A Backend's hook that hosts the workers of every chosen backend:
	 * runs each worker, from 0 to count, at once, and waits for them all.
	 */
	void (*hostWorkers)(const Renderer& renderer, int count, const std::function<void(int worker)>& run);
	/**
	 * A Backend's hook: runs the plugin's kernel of a worker's step on
	 * the worker's batch; false where it holds no kernel for that step,
	 * so the fallback backend runs it (Renderer.fallback).
	 */
	bool (*ranStep)(const Renderer& renderer, Worker& worker, const Plugin& plugin, LoopStep step);
	/**
	 * A Backend's hook: closes what its workers held, after a pass.
	 */
	void (*closeWorkers)(const Renderer& renderer);
	/**
	 * A Backend's hook: told that a block the kernels read is new or
	 * changed.
	 */
	void (*uploadBlock)(const Renderer& renderer, const Block& block);
};

/**
 * A plugin as the core knows it.
 */
struct Plugin {
	/**
	 * Its folder below `source/plugins/`, as `execution/packets`.
	 */
	std::string name;
	/**
	 * The point it fills.
	 */
	Point point;
	/**
	 * What it does to the mean.
	 */
	Effect effect;
	/**
	 * The steps and hooks it fills.
	 */
	Hooks hooks;
	/**
	 * What its work reads: Camera, Lights, Looks or Shapes for a
	 * preparation, Surfaces or Records for a reconstruction.
	 */
	Reads reads;
	/**
	 * The plugins whose preparation its own preparation reads, which
	 * run before it where they are chosen.
	 */
	std::vector<std::string> after;
};

/**
 * Return hooks that fill nothing.
 */
Hooks noHooks();

/**
 * Return whether flags hold a flag.
 *
 * @param reads The flags.
 * @param flag The flag.
 */
bool readsOf(Reads reads, Reads flag);

/**
 * Return the flags of two together.
 *
 * @param a One.
 * @param b The other.
 */
Reads joinedReads(Reads a, Reads b);

/**
 * Return the flags of a preparation that reads the camera and the whole
 * scene: Camera, Lights, Looks and Shapes, so it runs again on any
 * change.
 */
Reads everyRead();

/**
 * Return every plugin of this build, in the order of their folders.
 *
 * The build generates its definition (`plugins.cpp`, one call per
 * `plugin.h`); a folder the build leaves out (CMake's `-DOFF`) is not
 * listed.
 */
std::vector<Plugin> everyPlugin();

/**
 * Return whether this build holds the plugin of that name.
 *
 * @param name The plugin's name, as `feature/clouds`.
 */
bool pluginBuilt(const std::string& name);

/**
 * Return the place of the plugin of that name in everyPlugin(), or
 * 0xffffffff where this build holds none of that name.
 *
 * @param name The plugin's name, as `feature/clouds`.
 */
std::uint32_t pluginIndex(const std::string& name);

/**
 * Return the plugin of that name, or print that this build holds none
 * of that name and exit 1.
 *
 * @param name The plugin's name, as `feature/clouds`.
 */
Plugin pluginNamed(const std::string& name);

} // namespace kodanuki
