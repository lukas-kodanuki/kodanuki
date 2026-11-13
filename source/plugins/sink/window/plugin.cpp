#include "plugins/sink/window/plugin.h"

#include "core/render.h"
#include "plugins/sink/window/exchange.h"
#include "plugins/sink/window/fly.h"
#include "plugins/sink/window/window.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace kodanuki {

using namespace kodanuki::viewer;

struct Viewing {
	const Window& window;
	Exchange& exchange;
	const Camera& camera;
	std::string name;
};

std::string timesText(const Published& frame) {
	const StepSeconds& took = frame.steps;
	char text[160];
	std::snprintf(text, sizeof(text), "%.3f s a frame: prepare %.3f, trace %.3f, surfaces %.3f, reconstruct %.3f, keep %.3f, grade %.3f", frame.seconds, secondsOf(took, LoopStep::Preparations), secondsOf(took, LoopStep::Pass), secondsOf(took, LoopStep::Surfaces), secondsOf(took, LoopStep::Reconstruction), secondsOf(took, LoopStep::Keeping), frame.grading);
	return text;
}

std::string frameTitle(const std::string& name, const Published& frame) {
	if (!frame.error.empty()) {
		return frame.error;
	}
	char text[80];
	std::snprintf(text, sizeof(text), " · %u sample%s · ", frame.samples, frame.samples == 1 ? "" : "s");
	return name + text + timesText(frame);
}

struct Pace {
	std::chrono::steady_clock::time_point since;
	int frames;
	double showing;
};

Pace freshPace(std::chrono::steady_clock::time_point now) {
	return Pace {
		.since   = now,
		.frames  = 0,
		.showing = 0,
	};
}

Pace pacedFrame(const Pace& pace, const Published& frame, double showing) {
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	const Pace next = {
		.since   = pace.since,
		.frames  = pace.frames + 1,
		.showing = pace.showing + showing,
	};

	const double seconds = std::chrono::duration<double>(now - next.since).count();
	if (seconds < 1) {
		return next;
	}
	std::fprintf(stderr, "%.2f fps, show %.3f s, %.0f builds for %.0f clip frames; last %s\n", next.frames / seconds, next.showing / next.frames, double(frame.builds), double(frame.times), timesText(frame).c_str());
	return freshPace(now);
}

struct Playback {
	double at;
	bool paused;
};

Playback playedOn(const Playback& playback, const WindowInput& input, double seconds, double rate) {
	const bool paused  = playback.paused != input.pause;
	const double frame = std::floor(playback.at * rate + 1e-6);

	const Playback next = {
		.at     = input.step != 0 ? (frame + input.step) / rate : playback.at + (paused ? 0 : seconds),
		.paused = paused,
	};

	return next;
}

Playback startedPlayback() {
	const Playback playback = {
		.at     = 0,
		.paused = false,
	};

	return playback;
}

struct Flight {
	Fly fly;
	bool flying;
};

Flight groundedFlight(const Camera& camera) {
	const Flight flight = {
		.fly    = startedFly(camera),
		.flying = false,
	};

	return flight;
}

Flight flownBy(const Flight& flight, const WindowInput& input, double seconds, const Camera& shown) {
	const Fly from = flight.flying ? flight.fly : startedFly(shown);

	const Flight next = {
		.fly    = flown(from, input.steering, seconds),
		.flying = true,
	};

	return next;
}

void viewedLoop(const Viewing& viewing) {
	Camera shown                               = viewing.camera;
	std::uint64_t seen                         = 0;
	std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
	Pace pace                                  = freshPace(last);
	Playback playback                          = startedPlayback();
	Flight flight                              = groundedFlight(viewing.camera);
	while (true) {
		const WindowInput input                         = polledInput(viewing.window, 16);
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		const double seconds                            = std::chrono::duration<double>(now - last).count();
		last                                            = now;
		if (input.closed) {
			return;
		}
		if (input.reload) {
			requestedReload(viewing.exchange);
		}
		if (viewing.exchange.playing) {
			playback = playedOn(playback, input, seconds, viewing.exchange.rate);
			playedTo(viewing.exchange, playback.at);
		}
		if (steers(input.steering)) {
			flight = flownBy(flight, input, seconds, shown);
			wantedCamera(viewing.exchange, flight.fly.camera);
		}
		const Published frame = newestFrame(viewing.exchange, seen);
		if (frame.number > seen) {
			shownFrame(viewing.window, frame.rgb);
			titledWindow(viewing.window, frameTitle(viewing.name, frame));
			const double showing = std::chrono::duration<double>(std::chrono::steady_clock::now() - now).count();
			pace                 = pacedFrame(pace, frame, showing);
			seen                 = frame.number;
			shown                = frame.camera;
		}
	}
}

void windowOpened(Renderer& renderer) {
	keptState(renderer, "sink/window", std::shared_ptr<Exchange>(new Exchange(freshExchange(renderer.file.shot.camera))));
	windowExchange(renderer).rate    = renderer.file.shot.clip.rate;
	windowExchange(renderer).playing = !renderer.options.time && renderer.file.shot.clip.duration > 0;
}

void countedTime(Exchange& exchange, double time) {
	if (exchange.times == 0 || time != exchange.shownTime) {
		exchange.times += 1;
	}
	exchange.shownTime = time;
}

void windowDelivered(Renderer& renderer, const Image& shown) {
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	Exchange& exchange                                = windowExchange(renderer);
	std::vector<unsigned char> rgb                    = gradedBytes(shown, renderer.file.shot.grade, renderer.options.choices.threads);
	const double grading                              = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	countedTime(exchange, renderer.frame.time);

	const Published frame = {
		.number  = 0,
		.width   = shown.width,
		.height  = shown.height,
		.rgb     = std::move(rgb),
		.samples = std::uint32_t(renderer.drawn.samples),
		.seconds = secondsOf(exchange.took, LoopStep::DrawFrame) + grading,
		.steps   = exchange.took,
		.grading = grading,
		.error   = exchange.failed,
		.camera  = renderer.frame.camera,
		.builds  = exchange.builds,
		.times   = exchange.times,
	};

	publishedFrame(exchange, frame);
	exchange.took = noSeconds();
}

void windowObserved(void* state, const StepEvent& event) {
	Exchange& exchange = stateExchange(state);
	if (event.step == LoopStep::BuildWorld && event.done) {
		exchange.builds += 1;
	}
	addedStep(exchange.took, event);
}

double windowPlayed(Renderer& renderer) {
	return playedTime(windowExchange(renderer));
}

std::thread startedDrawing(Renderer& renderer) {
	const auto work = [&renderer]() {
		SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
		runFrames(renderer);
	};

	return std::thread(work);
}

int windowHosted(Renderer& renderer) {
	const Options& options = renderer.options;
	const std::string name = std::filesystem::path(options.arguments.values[1]).stem().string();
	Exchange& exchange     = windowExchange(renderer);

	const WindowShape shape = {
		.width       = options.width * options.factor,
		.height      = options.height * options.factor,
		.frameWidth  = options.width * options.factor,
		.frameHeight = options.height * options.factor,
	};

	const Camera camera = renderer.file.shot.camera;
	const Window window = openedWindow(name + " · building the tree", shape);

	const Viewing viewing = {
		.window   = window,
		.exchange = exchange,
		.camera   = camera,
		.name     = name,
	};

	std::thread drawing = startedDrawing(renderer);
	viewedLoop(viewing);
	stoppedDrawing(exchange, drawing);
	closedWindow(window);
	return 0;
}

std::vector<std::string> windowNeeded() {
	return { "source/flight" };
}

Plugin sinkWindowPlugin() {
	Hooks hooks        = noHooks();
	hooks.openRenderer = windowOpened;
	hooks.deliverFrame = windowDelivered;
	hooks.hostRenderer = windowHosted;
	hooks.playedClip   = windowPlayed;
	hooks.observed     = windowObserved;
	hooks.needed       = windowNeeded;
	return Plugin {
		.name   = "sink/window",
		.point  = Point::Sink,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
