#pragma once

#include "frame/frame.h"
#include "frame/preparation.h"
#include "frame/reconstruction.h"
#include "frame/run.h"
#include "frame/sequence.h"
#include "frame/shown.h"
#include "sdf/scene.h"
#include "transport/feature.h"
#include "walk/walk.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kodanuki::viewer {

/**
 * A scene file with the command line's settings and choices applied.
 */
struct ViewedScene {
	/**
	 * The scene, its shot overridden by the command line's settings.
	 */
	SceneFile file;
	/**
	 * The members and settings the command line chose.
	 */
	Choices choices;
	/**
	 * The output width, the window's and the shown frames': the scene's
	 * render width or --size. The frames are drawn at the render size,
	 * the internal size, which --internal sets and which is the output
	 * size without it.
	 */
	int outputWidth;
	/**
	 * The output height.
	 */
	int outputHeight;
	/**
	 * The output size over the internal size, the same on both sides:
	 * 1 to 4. A frame is drawn jittered at the internal size, its phase
	 * rotating in the Bayer order (bayerFilm), and Upscale turns it into
	 * the output size.
	 */
	int factor;
};

/**
 * A frame the drawing thread finished, graded for the screen.
 */
struct Published {
	/**
	 * How many frames were published before it, this one included; 0
	 * before the first.
	 */
	std::uint64_t number;
	/**
	 * The width in pixels.
	 */
	int width;
	/**
	 * The height in pixels.
	 */
	int height;
	/**
	 * The graded bytes in RGB order, by rows from the top left.
	 */
	std::vector<unsigned char> rgb;
	/**
	 * How many samples each pixel holds since the camera or the scene
	 * last changed.
	 */
	std::uint32_t samples;
	/**
	 * The seconds the frame took to draw.
	 */
	double seconds;
	/**
	 * How long the frame's parts took.
	 */
	StepTimes times;
	/**
	 * How long grading it into bytes took, in seconds.
	 */
	double grading;
	/**
	 * The error of the last failed reload, `file:line:column: message`,
	 * empty where the scene drawn is the last one loaded.
	 */
	std::string error;
};

/**
 * What the window thread and the drawing thread share: the wishes going
 * in, the newest frame coming out, behind one lock.
 */
struct Exchange {
	/**
	 * The lock over every other field.
	 */
	std::mutex mutex;
	/**
	 * Wakes the drawing thread when it rests and a wish arrives.
	 */
	std::condition_variable wake;
	/**
	 * The newest camera the person flew to.
	 */
	Camera camera;
	/**
	 * Whether the camera was handed in since the drawing thread last
	 * took it.
	 */
	bool moved;
	/**
	 * Whether the scene file is to be loaded again.
	 */
	bool reload;
	/**
	 * Whether the drawing thread is to stop.
	 */
	bool quit;
	/**
	 * The newest finished frame.
	 */
	Published frame;
};

/**
 * What the drawing thread draws from: the scene it starts with, the
 * command line to apply again on a reload and the exchange it talks
 * through.
 */
struct DrawingRun {
	/**
	 * The scene it starts with; its render size is the window's.
	 */
	const ViewedScene& scene;
	/**
	 * The command line: the scene file's path, then the flags.
	 */
	CommandLine arguments;
	/**
	 * The exchange with the window thread.
	 */
	Exchange& exchange;
};

/**
 * What the drawing thread took from the exchange for its next frame.
 */
struct Order {
	/**
	 * The newest camera.
	 */
	Camera camera;
	/**
	 * Whether the scene file is to be loaded again.
	 */
	bool reload;
	/**
	 * Whether the drawing thread is to stop.
	 */
	bool quit;
};

/**
 * Everything the frames of one loaded scene are drawn from, built once
 * after each load and kept until the next.
 */
struct SessionScene {
	/**
	 * The scene and the choices.
	 */
	const ViewedScene& viewed;
	/**
	 * The tree and the scene's geometry.
	 */
	const Geometry& geometry;
	/**
	 * The Feature members.
	 */
	const Features& features;
	/**
	 * The chosen prepared members.
	 */
	const Prepared& chosen;
	/**
	 * The preparations the chosen members read.
	 */
	const std::vector<PreparationMember>& preparations;
	/**
	 * The frames' count of finished tiles, which nothing reads.
	 */
	Progress& progress;
	/**
	 * The command line and the exchange.
	 */
	const DrawingRun& run;
};

/**
 * Where the drawing thread stands in a sequence: what the frame it draws
 * next is drawn with.
 */
struct Sequence {
	/**
	 * What it took from the exchange for the frame.
	 */
	Order order;
	/**
	 * Where the frame stands in the sequence.
	 */
	FrameStep step;
	/**
	 * The history after the frame before.
	 */
	History history;
	/**
	 * The error of the last failed reload, empty where none failed since
	 * the scene loaded.
	 */
	std::string error;
};

/**
 * A share of a frame's grading, which one thread fills in place.
 */
struct GradedPart {
	/**
	 * The linear image.
	 */
	const Image& image;
	/**
	 * The grade it is shown with.
	 */
	const Grade& grade;
	/**
	 * The display bytes, three a pixel, which the threads fill apart.
	 */
	std::vector<unsigned char>& bytes;
};

/**
 * Return a scene with the command line's settings and choices applied,
 * read as the renderer reads them (readSetting, readChoice), and the
 * viewer's own --internal w h, the size frames are drawn at (the render
 * size; without it the output size, --size); other flags are skipped.
 * The output size must be 1, 2, 3 or 4 times the internal size on both
 * sides, else it prints `--internal: --size must be 1, 2, 3 or 4 times
 * it on both sides` and exits 1. Without
 * --threads the viewer draws with half the cores, leaving the rest to
 * the desktop.
 *
 * @param file The scene as loaded.
 * @param arguments The command line: the scene file's path, then the
 * flags.
 */
ViewedScene settledScene(const SceneFile& file, const CommandLine& arguments);

/**
 * Return the Reconstruction members the viewer runs: the temporal
 * accumulation at the internal size, which keeps converging while the
 * camera holds still, then the denoiser, then the upscaler to the output
 * size.
 */
std::vector<ReconstructionMember> viewerReconstruction();

/**
 * Return the strategies the viewer draws with by default: Meet and Aim.
 *
 * Join's light pass splats onto the camera's pixels and runs again on
 * every move, which costs most of a moving frame (gallery 480x300: 3.4 of
 * 3.6 s), and the temporal upscaler reads the samples, not those splats.
 * Without Join, Meet and Aim carry all the light between them. --use
 * strategies=... chooses others.
 */
std::vector<StrategyMember> viewerStrategies();

/**
 * Return how many threads the viewer draws with by default: half the
 * cores, at least one.
 */
int viewerThreads();

/**
 * Return an exchange with no wish and no frame yet, holding a camera.
 *
 * @param camera The camera the person starts at.
 */
Exchange freshExchange(const Camera& camera);

/**
 * Hand the drawing thread a camera the person flew to; the next frame
 * draws through it and the running mean starts over.
 *
 * @param exchange The exchange.
 * @param camera The camera.
 */
void wantedCamera(Exchange& exchange, const Camera& camera);

/**
 * Ask the drawing thread to load the scene file again.
 *
 * On success the scene is drawn with the command line applied again, at
 * the window's size whatever size the file now names, and the sequence
 * starts over; on an error the old scene is kept and every frame carries
 * the error until a reload succeeds.
 *
 * @param exchange The exchange.
 */
void requestedReload(Exchange& exchange);

/**
 * Return a copy of the newest frame where it is newer than the one seen,
 * else a frame of number 0 and no bytes.
 *
 * @param exchange The exchange.
 * @param seen The number of the frame last shown.
 */
Published newestFrame(Exchange& exchange, std::uint64_t seen);

/**
 * Start the drawing thread.
 *
 * It builds the tree, then draws a sequence of frames through the newest
 * camera, one sample a pixel each at the render (internal) size,
 * jittered by the Bayer phase of its index (bayerFilm), and publishes
 * each graded frame at the output size.
 * While the camera holds still the running mean adds the frames up; it
 * rests once the shot's sample count (where not 0) is reached, until a
 * wish arrives. It runs at a low priority, and so do the threads it
 * starts, so the desktop stays responsive while it draws.
 *
 * @param run The scene, the command line and the exchange, which must
 * outlive the thread.
 */
std::thread startedDrawing(const DrawingRun& run);

/**
 * Ask the drawing thread to stop and wait for it; it finishes the frame
 * it draws first.
 *
 * @param exchange The exchange.
 * @param thread The drawing thread.
 */
void stoppedDrawing(Exchange& exchange, std::thread& thread);

}
