#include "viewer/session.h"

#include "image/image.h"
#include "syntax/evaluator.h"
#include "transport/camera.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace kodanuki::viewer {

std::vector<StrategyMember> viewerStrategies() {
	return { StrategyMember::Meet, StrategyMember::Aim };
}

int viewerThreads() {
	const int cores = int(std::thread::hardware_concurrency());
	return std::max(1, cores / 2);
}

Choices viewerChoices() {
	Choices choices    = defaultChoices();
	choices.threads    = viewerThreads();
	choices.strategies = viewerStrategies();
	return choices;
}

bool readInternal(Render& internal, char** argv, int& i) {
	if (std::string(argv[i]) != "--internal") {
		return false;
	}
	internal.width  = std::stoi(argv[++i]);
	internal.height = std::stoi(argv[++i]);
	return true;
}

int internalFactor(const Render& output, const Render& internal) {
	const bool fits = internal.width > 0 && internal.height > 0 && output.width % internal.width == 0 && output.height % internal.height == 0;
	const int k     = fits ? output.width / internal.width : 0;
	if (k < 1 || k > 4 || output.height != internal.height * k) {
		std::fprintf(stderr, "--internal: --size must be 1, 2, 3 or 4 times it on both sides\n");
		std::exit(1);
	}
	return k;
}

ViewedScene shownSized(const ViewedScene& scene, const Render& internal) {
	ViewedScene sized  = scene;
	Render& render     = sized.file.shot.render;
	const Render drawn = internal.width > 0 ? internal : render;
	sized.outputWidth  = render.width;
	sized.outputHeight = render.height;
	sized.factor       = internalFactor(render, drawn);
	render.width       = drawn.width;
	render.height      = drawn.height;
	return sized;
}

ViewedScene settledScene(const SceneFile& file, const CommandLine& arguments) {
	ViewedScene scene = {
		.file         = file,
		.choices      = viewerChoices(),
		.outputWidth  = 0,
		.outputHeight = 0,
		.factor       = 1,
	};

	Render internal = file.shot.render;
	internal.width  = 0;
	internal.height = 0;
	for (int i = 2; i + 1 < arguments.count; ++i) {
		if (readSetting(scene.file.shot, arguments.values, i) || readInternal(internal, arguments.values, i)) {
			continue;
		}
		readChoice(scene.choices, arguments, i);
	}
	return shownSized(scene, internal);
}

std::vector<ReconstructionMember> viewerReconstruction() {
	return { ReconstructionMember::Temporal, ReconstructionMember::Denoise, ReconstructionMember::Upscale };
}

StepTimes noTimes() {
	return StepTimes {
		.preparations   = 0,
		.trace          = 0,
		.surfaces       = 0,
		.reconstruction = 0,
		.keeping        = 0,
	};
}

Published emptyPublished() {
	return Published {
		.number  = 0,
		.width   = 0,
		.height  = 0,
		.rgb     = {},
		.samples = 0,
		.seconds = 0,
		.times   = noTimes(),
		.grading = 0,
		.error   = {},
	};
}

Exchange freshExchange(const Camera& camera) {
	return Exchange {
		.mutex  = {},
		.wake   = {},
		.camera = camera,
		.moved  = false,
		.reload = false,
		.quit   = false,
		.frame  = emptyPublished(),
	};
}

void wantedCamera(Exchange& exchange, const Camera& camera) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	exchange.camera = camera;
	exchange.moved  = true;
	exchange.wake.notify_one();
}

void requestedReload(Exchange& exchange) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	exchange.reload = true;
	exchange.wake.notify_one();
}

Published newestFrame(Exchange& exchange, std::uint64_t seen) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	if (exchange.frame.number <= seen) {
		return emptyPublished();
	}
	return exchange.frame;
}

void publishedFrame(Exchange& exchange, const Published& frame) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	const std::uint64_t number = exchange.frame.number + 1;
	exchange.frame             = frame;
	exchange.frame.number      = number;
}

Order takenOrder(Exchange& exchange, bool resting) {
	std::unique_lock<std::mutex> lock(exchange.mutex);
	const auto wished = [&]() {
		return !resting || exchange.moved || exchange.reload || exchange.quit;
	};

	exchange.wake.wait(lock, wished);

	const Order order = {
		.camera = exchange.camera,
		.reload = exchange.reload,
		.quit   = exchange.quit,
	};

	exchange.moved  = false;
	exchange.reload = false;
	return order;
}

void gradedPart(const GradedPart& part, std::size_t first, std::size_t last) {
	for (std::size_t i = first; i < last; ++i) {
		part.bytes[i] = encodedByte(part.image.rgb[i], part.grade);
	}
}

std::vector<unsigned char> gradedBytes(const Image& image, const Grade& grade, int threads) {
	std::vector<unsigned char> bytes(image.rgb.size());
	const std::size_t count = bytes.size();
	const std::size_t parts = std::size_t(std::max(1, threads));

	const GradedPart part = {
		.image = image,
		.grade = grade,
		.bytes = bytes,
	};

	std::vector<std::thread> workers;
	for (std::size_t k = 1; k < parts; ++k) {
		workers.emplace_back(gradedPart, std::cref(part), count * k / parts, count * (k + 1) / parts);
	}
	gradedPart(part, 0, count / parts);
	for (std::thread& worker : workers) {
		worker.join();
	}
	return bytes;
}

Lens drawnLens(const SessionScene& scene, const Camera& camera) {
	return lensOf(camera, scene.viewed.file.shot.render);
}

StepQuery sessionQuery(const Lens& lens, const FilmPlan& film) {
	return StepQuery {
		.lens        = lens,
		.film        = film,
		.change      = Change::None,
		.keepsGuides = true,
	};
}

FrameStep firstSessionStep(const SessionScene& scene, const Camera& camera) {
	const Lens lens = drawnLens(scene, camera);
	return firstStep(sessionQuery(lens, bayerFilm(scene.viewed.factor, 0)));
}

FrameStep nextSessionStep(const SessionScene& scene, const FrameStep& last, const Camera& camera) {
	const Lens lens = drawnLens(scene, camera);
	return nextStep(last, sessionQuery(lens, bayerFilm(scene.viewed.factor, last.index + 1)));
}

bool restingAfter(const SessionScene& scene, const FrameStep& step) {
	const int samples = scene.viewed.file.shot.render.samples;
	return samples > 0 && step.still + 1 >= std::uint32_t(samples);
}

Stepped sessionFrame(const SessionScene& scene, const FrameStep& step, History history) {
	const ViewedScene& viewed                              = scene.viewed;
	const std::vector<ReconstructionMember> reconstruction = viewerReconstruction();

	const FrameInput input = {
		.geometry     = scene.geometry,
		.looks        = viewed.file.looks,
		.lighting     = viewed.file.lighting,
		.shot         = viewed.file.shot,
		.lens         = step.lens,
		.chosen       = scene.chosen,
		.preparations = scene.preparations,
		.features     = scene.features,
		.stopping     = viewed.choices.stopping,
		.smoothGlass  = viewed.choices.smoothGlass,
		.execution    = viewed.choices.execution,
		.threads      = viewed.choices.threads,
		.progress     = scene.progress,
	};

	const SequenceInput sequence = {
		.frame          = input,
		.step           = step,
		.reconstruction = reconstruction,
		.surfaced       = readsSurfaces(reconstruction),
	};

	return steppedFrame(sequence, std::move(history));
}

Stepped publishedStep(const SessionScene& scene, const Sequence& sequence, History history) {
	const std::chrono::steady_clock::time_point start  = std::chrono::steady_clock::now();
	Stepped stepped                                    = sessionFrame(scene, sequence.step, std::move(history));
	const std::chrono::steady_clock::time_point graded = std::chrono::steady_clock::now();
	std::vector<unsigned char> rgb                     = gradedBytes(stepped.image, scene.viewed.file.shot.grade, scene.viewed.choices.threads);
	const std::chrono::steady_clock::time_point done   = std::chrono::steady_clock::now();

	const Published frame = {
		.number  = 0,
		.width   = stepped.image.width,
		.height  = stepped.image.height,
		.rgb     = std::move(rgb),
		.samples = sequence.step.still + 1,
		.seconds = std::chrono::duration<double>(done - start).count(),
		.times   = stepped.times,
		.grading = std::chrono::duration<double>(done - graded).count(),
		.error   = sequence.error,
	};

	publishedFrame(scene.run.exchange, frame);
	return stepped;
}

Sequence startedSequence(const SessionScene& scene) {
	const Order order = takenOrder(scene.run.exchange, false);
	return Sequence {
		.order   = order,
		.step    = firstSessionStep(scene, order.camera),
		.history = emptyHistory(),
		.error   = {},
	};
}

Sequence nextSequence(const SessionScene& scene, Sequence sequence) {
	History history   = std::move(sequence.history);
	Stepped stepped   = publishedStep(scene, sequence, std::move(history));
	const Order order = takenOrder(scene.run.exchange, restingAfter(scene, sequence.step));
	return Sequence {
		.order   = order,
		.step    = nextSessionStep(scene, sequence.step, order.camera),
		.history = std::move(stepped.history),
		.error   = sequence.error,
	};
}

ViewedScene reloadedScene(const SceneFile& file, const DrawingRun& run) {
	ViewedScene scene             = settledScene(file, run.arguments);
	scene.file.shot.render.width  = run.scene.file.shot.render.width;
	scene.file.shot.render.height = run.scene.file.shot.render.height;
	return scene;
}

std::optional<ViewedScene> drawnFrames(const SessionScene& scene) {
	Sequence sequence = startedSequence(scene);
	while (!sequence.order.quit) {
		if (sequence.order.reload) {
			const LoadedScene loaded = loadScene(scene.run.arguments.values[1]);
			if (loaded.error.empty()) {
				return reloadedScene(loaded.file, scene.run);
			}
			sequence.error = loaded.error;
		}
		sequence = nextSequence(scene, std::move(sequence));
	}
	return std::nullopt;
}

std::optional<ViewedScene> drawnUntilReload(const ViewedScene& viewed, const DrawingRun& run) {
	const Features features                           = featuresOf(featureQueryOf(viewed.file, viewed.choices.threads));
	const BuiltGeometry built                         = builtGeometry(viewed.file, viewed.choices, features);
	const Prepared chosen                             = chosenPrepared(viewed.choices);
	const std::vector<PreparationMember> preparations = chosenPreparations(viewed.choices);
	Progress progress                                 = freshProgress();

	const Geometry geometry = {
		.scene = viewed.file.scene,
		.tree  = built.tree,
	};

	const SessionScene scene = {
		.viewed       = viewed,
		.geometry     = geometry,
		.features     = features,
		.chosen       = chosen,
		.preparations = preparations,
		.progress     = progress,
		.run          = run,
	};

	return drawnFrames(scene);
}

void drawnSession(const DrawingRun& run) {
	ViewedScene viewed = run.scene;
	while (true) {
		const std::optional<ViewedScene> next = drawnUntilReload(viewed, run);
		if (!next) {
			return;
		}
		viewed = *next;
	}
}

std::thread startedDrawing(const DrawingRun& run) {
	const auto work = [run]() {
		SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
		drawnSession(run);
	};

	return std::thread(work);
}

void quitDrawing(Exchange& exchange) {
	const std::lock_guard<std::mutex> lock(exchange.mutex);
	exchange.quit = true;
	exchange.wake.notify_one();
}

void stoppedDrawing(Exchange& exchange, std::thread& thread) {
	quitDrawing(exchange);
	thread.join();
}

}
