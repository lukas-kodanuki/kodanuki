#pragma once

#include "core/frame.h"
#include "core/step.h"
#include "sdf/scene.h"
#include "shared/image.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kodanuki {

struct Renderer;

}

namespace kodanuki::viewer {

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
	 * The seconds the frame took to draw and grade.
	 */
	double seconds;
	/**
	 * How long the frame's steps took.
	 */
	StepSeconds steps;
	/**
	 * How long grading it into bytes took, in seconds.
	 */
	double grading;
	/**
	 * The error of the last failed reload, `file:line:column: message`,
	 * empty where the scene drawn is the last one loaded.
	 */
	std::string error;
	/**
	 * The camera it was drawn through.
	 */
	Camera camera;
	/**
	 * How many times the world was built since the run began.
	 */
	std::uint64_t builds;
	/**
	 * How many frames of a new time were shown since the run began.
	 */
	std::uint64_t times;
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
	 * Whether the person flew the camera at all: until then the frames
	 * look through the scene's camera at their time.
	 */
	bool flown;
	/**
	 * The time of the clip the window plays, in seconds since it first
	 * started, never wrapped (Hooks.playedClip).
	 */
	double played;
	/**
	 * Whether the clip's frame at the played time changed since the
	 * drawing thread last took an order.
	 */
	bool advanced;
	/**
	 * The frames a second of the scene's clip as it opened, which an
	 * arrow key steps by; set before the threads start.
	 */
	double rate;
	/**
	 * Whether the scene plays a clip as it opened (a clip and no
	 * --time); a still's played time never moves. Set before the
	 * threads start.
	 */
	bool playing;
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
	/**
	 * The error of the last failed reload, `file:line:column: message`,
	 * empty where the scene drawn is the last one loaded; only the
	 * drawing thread reads and writes it.
	 */
	std::string failed;
	/**
	 * How long the steps of the frame being drawn took so far; only the
	 * drawing thread reads and writes it.
	 */
	StepSeconds took;
	/**
	 * How many times the world was built; only the drawing thread reads
	 * and writes it.
	 */
	std::uint64_t builds;
	/**
	 * How many frames of a new time were delivered, and the time of the
	 * last; only the drawing thread reads and writes them.
	 */
	std::uint64_t times;
	/**
	 * The time of the frame delivered last.
	 */
	double shownTime;
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
	 * Whether the person flew the camera at all.
	 */
	bool flown;
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
 * Hand the drawing thread the time of the clip the window plays; where
 * the clip's frame at it changed, a resting drawing thread wakes.
 *
 * @param exchange The exchange.
 * @param played The time, in seconds since the clip first started.
 */
void playedTo(Exchange& exchange, double played);

/**
 * Return the time of the clip the window plays (Exchange.played).
 *
 * @param exchange The exchange.
 */
double playedTime(Exchange& exchange);

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
 * Publish a finished frame as the newest, numbered one past the one
 * before.
 *
 * @param exchange The exchange.
 * @param frame The frame.
 */
void publishedFrame(Exchange& exchange, const Published& frame);

/**
 * Wait, where resting, until a wish arrives or the clip's frame
 * changes, then take the newest camera, the reload and the quit, and
 * clear the wishes taken.
 *
 * @param exchange The exchange.
 * @param resting Whether to wait for a wish.
 */
Order takenOrder(Exchange& exchange, bool resting);

/**
 * Ask the drawing thread to stop and wait for it; it finishes the frame
 * it draws first.
 *
 * @param exchange The exchange.
 * @param thread The drawing thread.
 */
void stoppedDrawing(Exchange& exchange, std::thread& thread);

/**
 * Return an image graded into display bytes, three a pixel, the work
 * shared by the threads.
 *
 * @param image The linear image.
 * @param grade The grade.
 * @param threads How many threads.
 */
std::vector<unsigned char> gradedBytes(const Image& image, const Grade& grade, int threads);

/**
 * Return the exchange a plugin state holds: the one cast of it.
 *
 * @param state The state of `sink/window`.
 */
Exchange& stateExchange(void* state);

/**
 * Return the exchange of the run's `sink/window`, which `source/flight`
 * takes its orders from.
 *
 * @param renderer The renderer; `sink/window` must be on.
 */
Exchange& windowExchange(const Renderer& renderer);

}
