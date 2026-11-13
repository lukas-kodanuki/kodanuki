#include "plugins/sink/frames/plugin.h"

#include "core/render.h"
#include "core/step.h"
#include "math/colour.h"
#include "plugins/reconstruction/temporal/temporal.h"
#include "shared/image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>

namespace kodanuki {

struct NoiseSum {
	double ofMean;
	double perSample;
	double mature;
};

double perSampleNoise(const TemporalPixel& pixel) {
	const double luminance = luminanceOf(pixel.colour);
	const double squared   = luminance * luminance;
	const double length    = double(pixel.length);
	return std::max(pixel.moment - squared, 0.0) * length / ((length - 1) * (squared + 0.01));
}

NoiseSum noiseSumOf(const TemporalState& state) {
	NoiseSum sum = {
		.ofMean    = 0,
		.perSample = 0,
		.mature    = 0,
	};

	for (const TemporalPixel& pixel : state.pixels) {
		if (pixel.length < 2) {
			continue;
		}
		const double noise = perSampleNoise(pixel);
		sum.ofMean += noise / double(pixel.length);
		sum.perSample += noise;
		sum.mature += 1;
	}
	return sum;
}

std::string noiseNote(const TemporalState& state) {
	if (state.pixels.empty()) {
		return {};
	}
	const NoiseSum sum    = noiseSumOf(state);
	const double count    = double(state.pixels.size());
	const double young    = sum.mature > 0 ? sum.perSample / sum.mature : 1;
	const double estimate = (sum.ofMean + (count - sum.mature) * young) / count;
	char note[48];
	std::snprintf(note, sizeof(note), ", noise %.6f", estimate);
	return note;
}

std::string framePath(const std::string& directory, int frame, const char* extension) {
	char name[32];
	std::snprintf(name, sizeof(name), "frame-%04d.%s", frame, extension);
	return (std::filesystem::path(directory) / name).string();
}

StepSeconds& framesSeconds(void* state) {
	return *static_cast<StepSeconds*>(state);
}

void framesOpened(Renderer& renderer) {
	if (renderer.options.out.empty()) {
		std::fprintf(stderr, "sink/frames: --out names the directory\n");
		std::exit(1);
	}
	std::filesystem::create_directories(renderer.options.out);
	keptState(renderer, "sink/frames", std::make_shared<StepSeconds>(noSeconds()));
}

void framesObserved(void* state, const StepEvent& event) {
	addedStep(framesSeconds(state), event);
}

void framesDelivered(Renderer& renderer, const Image& shown) {
	const int frame         = renderer.frame.index;
	const std::string& out  = renderer.options.out;
	StepSeconds& took       = framesSeconds(stateOf(renderer, "sink/frames"));
	const std::string noise = noiseNote(temporalState(renderer.history.states));
	writePpm(shown, renderer.file.shot.grade, framePath(out, frame, "ppm"));
	writePfm(shown, framePath(out, frame, "pfm"));
	std::fprintf(stderr, "%sframe %d, %.3f s: prepare %.3f, trace %.3f, surfaces %.3f, reconstruct %.3f, keep %.3f; time %g, %.1f samples a pixel, evaluate %.3f s, build %.3f s%s\n", isatty(STDERR_FILENO) ? "\r\x1b[K" : "", frame, secondsOf(took, LoopStep::DrawFrame), secondsOf(took, LoopStep::Preparations), secondsOf(took, LoopStep::Pass), secondsOf(took, LoopStep::Surfaces), secondsOf(took, LoopStep::Reconstruction), secondsOf(took, LoopStep::Keeping), renderer.frame.time, renderer.drawn.samples, secondsOf(took, LoopStep::Evaluate), secondsOf(took, LoopStep::BuildWorld), noise.c_str());
	took = noSeconds();
}

Plugin sinkFramesPlugin() {
	Hooks hooks        = noHooks();
	hooks.openRenderer = framesOpened;
	hooks.deliverFrame = framesDelivered;
	hooks.observed     = framesObserved;
	return Plugin {
		.name   = "sink/frames",
		.point  = Point::Sink,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
