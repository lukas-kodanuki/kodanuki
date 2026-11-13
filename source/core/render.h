#pragma once

#include "core/frame.h"
#include "core/geometry.h"
#include "core/history.h"
#include "core/options.h"
#include "core/plugin.h"
#include "core/step.h"
#include "sdf/scene.h"
#include "shared/image.h"
#include "syntax/evaluator.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * The frame the Source plugins set up: its time, its camera and what
 * changed.
 */
struct FrameState {
	/**
	 * How many frames were delivered before it.
	 */
	int index;
	/**
	 * The time the scene is evaluated at, in seconds (Renderer.file).
	 */
	double time;
	/**
	 * The camera it is drawn through.
	 */
	Camera camera;
	/**
	 * What changed since the frame before: Lights, Looks or Shapes where
	 * the scene evaluated anew differs from the frame before's (sceneAt()),
	 * Cut where a clip cuts or starts again.
	 */
	Change change;
	/**
	 * Whether the scene's count of shapes differs from the frame
	 * before's (ShownFrame.recounted).
	 */
	bool recounted;
	/**
	 * Whether the sequence starts over with it: the first frame after the
	 * world was built.
	 */
	bool restarted;
};

/**
 * What the frames of one loaded scene are drawn from, built when the
 * scene loads and kept until it loads again.
 */
struct World {
	/**
	 * What rays meet, as the Geometry plugin built it.
	 */
	Geometry geometry;
};

/**
 * What the last frame drew.
 */
struct Drawn {
	/**
	 * The image, reconstructed.
	 */
	Image image;
	/**
	 * The mean samples a pixel's estimate holds: the frame's, and those
	 * of the frames before it since the camera or the scene last changed.
	 */
	double samples;
	/**
	 * How many rounds of the pixel pass the frame ran: none where it
	 * had nothing left to draw.
	 */
	int rounds;
};

/**
 * Everything a run keeps from its first frame to its last.
 */
struct Renderer {
	/**
	 * The scene evaluated at the frame's time (FrameState.time), with the
	 * command line's settings applied.
	 */
	SceneFile file;
	/**
	 * The options of the run.
	 */
	Options options;
	/**
	 * The Geometry plugin.
	 */
	Plugin geometry;
	/**
	 * The Transport plugin.
	 */
	Plugin transport;
	/**
	 * The Execution plugin, whose extendRays every pass's step calls.
	 */
	Plugin execution;
	/**
	 * The Backend plugins, whose workers draw a pass together.
	 */
	std::vector<Plugin> backends;
	/**
	 * The backend that hosts the workers on its threads and runs a step
	 * a chosen backend holds no kernel for (fallbackBackend()).
	 */
	Plugin fallback;
	/**
	 * The Source plugins, in order.
	 */
	std::vector<Plugin> sources;
	/**
	 * The Sink plugins, in order.
	 */
	std::vector<Plugin> sinks;
	/**
	 * The Observer plugins, in order.
	 */
	std::vector<Plugin> observers;
	/**
	 * The chosen plugins that prepare a frame, in order
	 * (orderedPreparations()).
	 */
	std::vector<Plugin> preparations;
	/**
	 * The Stopping plugin.
	 */
	Plugin stopping;
	/**
	 * The Reconstruction plugins, in order.
	 */
	std::vector<Plugin> reconstructions;
	/**
	 * The frame being drawn.
	 */
	FrameState frame;
	/**
	 * The world, empty before the first frame.
	 */
	std::optional<World> world;
	/**
	 * Where the run stands: the frame drawn last or being drawn.
	 */
	FrameStep step;
	/**
	 * What a frame leaves the next.
	 */
	History history;
	/**
	 * What the last frame drew.
	 */
	Drawn drawn;
	/**
	 * The frame being drawn: what its passes read and fill.
	 */
	FrameDrawing drawing;
	/**
	 * Who is told of the steps: the observed and tallied hooks of the
	 * Source, Sink and Observer plugins, set once they are open.
	 */
	Observing observing;
	/**
	 * The plugins' states.
	 */
	States states;
};

/**
 * Draw a run: open the renderer, then frames until a Source plugin
 * says none follows, each set up by the Source plugins and handed to
 * the Sink plugins, while the Observer plugins are told of the steps.
 * A Sink with a hostRenderer hook keeps the main thread and draws the
 * frames on a thread of its own; otherwise they are drawn on this one.
 *
 * @param file The scene, the command line's settings applied.
 * @param options The options read from the command line.
 */
int render(SceneFile file, const Options& options);

/**
 * Return the renderer of a run before it opens: the chosen plugins,
 * no frame drawn yet.
 *
 * @param file The scene, the command line's settings applied.
 * @param options The options read from the command line.
 */
Renderer rendererOf(SceneFile file, const Options& options);

/**
 * Open the renderer's plugins and draw its frames, as render() does.
 *
 * @param renderer The renderer (rendererOf()).
 */
int rendered(Renderer& renderer);

/**
 * Draw the frames of a run until a Source plugin says none follows:
 * nextFrame, buildWorld, prepareFrame, the frame, reconstructFrame,
 * deliverFrame, each a step the observers are told of; then
 * closeRenderer.
 *
 * @param renderer The opened renderer.
 */
int runFrames(Renderer& renderer);

/**
 * Set the frame's time and take the scene at it: a file that read
 * `time` is evaluated again where the time differs from the frame's
 * (sceneTaken()); one that did not stays as it is. A scene whose
 * `render` or `clip` differs from the frame before's stops the run with
 * an error, as does an error of the file at that time. The core times
 * the evaluation as its step Evaluate.
 *
 * @param renderer The renderer.
 * @param time The time, in seconds.
 */
void sceneAt(Renderer& renderer, double time);

/**
 * Take a scene evaluated anew as the renderer's, adding to the frame's
 * change what differs from the scene before, part by part: Lights,
 * Looks and Shapes. The shapes are kept where they are the same, so the
 * world built of them stays valid.
 *
 * @param renderer The renderer.
 * @param next The scene, the command line's settings applied.
 */
void sceneTaken(Renderer& renderer, SceneFile next);

/**
 * Return a plugin's state, null where it keeps none.
 *
 * @param renderer The renderer.
 * @param name The plugin's name.
 */
void* stateOf(const Renderer& renderer, const std::string& name);

/**
 * Keep a plugin's state for the run.
 *
 * @param renderer The renderer.
 * @param name The plugin's name.
 * @param data The state.
 */
void keptState(Renderer& renderer, const std::string& name, std::shared_ptr<void> data);

} // namespace kodanuki
