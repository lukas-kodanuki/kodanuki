#include "frame/run.h"
#include "viewer/fly.h"
#include "viewer/session.h"
#include "viewer/window.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace kodanuki;
using namespace kodanuki::viewer;

struct Viewing {
	const Window& window;
	Exchange& exchange;
	const Camera& camera;
	std::string name;
};

std::string timesText(const Published& frame) {
	const StepTimes& times = frame.times;
	char text[160];
	std::snprintf(text, sizeof(text), "%.3f s a frame: prepare %.3f, trace %.3f, surfaces %.3f, reconstruct %.3f, keep %.3f, grade %.3f", frame.seconds, times.preparations, times.trace, times.surfaces, times.reconstruction, times.keeping, frame.grading);
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
	std::fprintf(stderr, "%.2f fps, show %.3f s; last %s\n", next.frames / seconds, next.showing / next.frames, timesText(frame).c_str());
	return freshPace(now);
}

void viewedLoop(const Viewing& viewing) {
	Fly fly                                    = startedFly(viewing.camera);
	std::uint64_t seen                         = 0;
	std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
	Pace pace                                  = freshPace(last);
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
		if (steers(input.steering)) {
			fly = flown(fly, input.steering, seconds);
			wantedCamera(viewing.exchange, fly.camera);
		}
		const Published frame = newestFrame(viewing.exchange, seen);
		if (frame.number > seen) {
			shownFrame(viewing.window, frame.rgb);
			titledWindow(viewing.window, frameTitle(viewing.name, frame));
			const double showing = std::chrono::duration<double>(std::chrono::steady_clock::now() - now).count();
			pace                 = pacedFrame(pace, frame, showing);
			seen                 = frame.number;
		}
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: kodanuki_view scene.sdf [--size w h] [--seed n] [--tolerance t] [--samples n] [--batch n] [--split n] [--surfaces n] [--collisions n] [--threads n] [--use key=value ...] [--region lx ly lz hx hy hz] [--least e] [--depth n] [--internal w h]\n");
		return 1;
	}

	const CommandLine arguments = {
		.count  = argc,
		.values = argv,
	};

	const ViewedScene scene = settledScene(sceneOrExit(argv[1]), arguments);
	const std::string name  = std::filesystem::path(argv[1]).stem().string();

	const WindowShape shape = {
		.width       = scene.outputWidth,
		.height      = scene.outputHeight,
		.frameWidth  = scene.outputWidth,
		.frameHeight = scene.outputHeight,
	};

	const Window window = openedWindow(name + " · building the tree", shape);
	Exchange exchange   = freshExchange(scene.file.shot.camera);

	const DrawingRun run = {
		.scene     = scene,
		.arguments = arguments,
		.exchange  = exchange,
	};

	std::thread drawing = startedDrawing(run);

	const Viewing viewing = {
		.window   = window,
		.exchange = exchange,
		.camera   = scene.file.shot.camera,
		.name     = name,
	};

	viewedLoop(viewing);
	stoppedDrawing(exchange, drawing);
	closedWindow(window);
	return 0;
}
