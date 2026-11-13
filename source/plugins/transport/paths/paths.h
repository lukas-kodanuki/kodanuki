#pragma once

#include "core/frame.h"
#include "core/options.h"
#include "core/pixels.h"
#include "core/stopping.h"
#include "core/worker.h"
#include "plugins/transport/paths/feature.h"
#include "plugins/transport/paths/light/light.h"
#include "plugins/transport/paths/material/material.h"
#include "plugins/transport/paths/sampler.h"
#include "plugins/transport/paths/strategy.h"
#include "plugins/transport/paths/trace.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * What `transport/paths` reads of a loaded scene: its state of the run,
 * opened again whenever the scene loads (openWorld).
 */
struct PathsWorld {
	/**
	 * The Feature members.
	 */
	Features features;
	/**
	 * How glass of roughness 0 answers, `--use smooth_glass=`.
	 */
	SmoothGlass smoothGlass;
	/**
	 * The Sampler members `--use` chose, their states empty.
	 */
	Sampling sampling;
	/**
	 * The Strategy members `--use` chose.
	 */
	Strategies strategies;
};

/**
 * What `transport/paths` works out for a frame: its state of the frame
 * (openFrame), which the walks and the preparations read.
 */
struct PathsFrame {
	/**
	 * The finishes, with the features applied.
	 */
	Materials materials;
	/**
	 * The lights.
	 */
	Lights lights;
	/**
	 * The Sampler members and the states the preparations left them
	 * (preparedSampling()).
	 */
	Sampling sampling;
	/**
	 * The Strategy members and their shares (preparedStrategies()).
	 */
	Strategies strategies;
	/**
	 * The transport, reading the fields above; set once they are.
	 */
	std::optional<Tracing> tracing;
};

/**
 * What `transport/paths` keeps a worker: the walk of every slot of the
 * worker's pool and, beside them, the light walks they lead with. Both
 * are plain data, without a pointer or a container inside.
 */
struct PathsWorker {
	/**
	 * The walk of each slot.
	 */
	Walk walks[poolCapacity];
	/**
	 * The light walk of each slot's walk, read only while the walk
	 * leads with one (Walk.led); a walk's place is its slot.
	 */
	LightWalk leads[poolCapacity];
};

/**
 * Return what `transport/paths` reads of the loaded scene, from its
 * state of the run; the one place that casts it.
 *
 * @param world The plugin's state of the run (stateOf()).
 */
const PathsWorld& pathsWorld(const void* world);

/**
 * Return the transport of a frame, from the plugin's state of the
 * frame; the one place that casts it.
 *
 * @param transported The plugin's state of the frame
 *        (Renderer.drawing.transported).
 */
const Tracing& tracingOf(const void* transported);

/**
 * Return a worker's walks, one a slot of its pool, from the plugin's
 * state of the worker; the one place that casts it.
 *
 * @param worker The worker.
 */
Walk* walksOf(const Worker& worker);

/**
 * Return a worker's light walks, one a slot of its pool, from the
 * plugin's state of the worker.
 *
 * @param worker The worker.
 */
LightWalk* leadsOf(const Worker& worker);

/**
 * Return how glass of roughness 0 answers, by its `--use smooth_glass=`
 * name.
 *
 * @param name `exact` or `lobe`.
 */
SmoothGlass smoothGlassNamed(const std::string& name);

/**
 * Return the Sampler members `--use` chose, with emission split toward
 * mirrors where connect is chosen; their states empty.
 *
 * @param choices The members and settings.
 */
Sampling chosenSampling(const Choices& choices);

/**
 * Return the Strategy members `--use` chose.
 *
 * @param choices The members and settings.
 */
Strategies chosenStrategies(const Choices& choices);

/**
 * Return what `transport/paths` reads of a loaded scene.
 *
 * @param file The scene.
 * @param choices The members and settings.
 */
PathsWorld pathsWorldOf(const SceneFile& file, const Choices& choices);

/**
 * Give a slot of the worker's pool the phase of its walk and, where the
 * walk waits on a ray, that ray (wantedRay()) and its kind.
 *
 * @param tracing What the transport reads.
 * @param slot The slot.
 * @param worker The worker.
 */
void waitingSlot(const Tracing& tracing, std::uint32_t slot, Worker& worker);

} // namespace kodanuki
