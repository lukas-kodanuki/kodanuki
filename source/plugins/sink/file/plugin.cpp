#include "plugins/sink/file/plugin.h"

#include "core/render.h"
#include "core/step.h"
#include "shared/image.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unistd.h>

namespace kodanuki {

StepSeconds& fileSeconds(void* state) {
	return *static_cast<StepSeconds*>(state);
}

void fileOpened(Renderer& renderer) {
	if (renderer.options.out.empty()) {
		std::fprintf(stderr, "sink/file: --out names the image\n");
		std::exit(1);
	}
	keptState(renderer, "sink/file", std::make_shared<StepSeconds>(noSeconds()));
}

void fileObserved(void* state, const StepEvent& event) {
	addedStep(fileSeconds(state), event);
}

void fileDelivered(Renderer& renderer, const Image& shown) {
	const Options& options = renderer.options;
	const Render& render   = renderer.file.shot.render;
	writePpm(shown, renderer.file.shot.grade, options.out);
	if (!options.pfm.empty()) {
		writePfm(shown, options.pfm);
	}
	const StepSeconds& seconds = fileSeconds(stateOf(renderer, "sink/file"));
	std::fprintf(stderr, "%s%dx%d, %.1f samples a pixel, tree %.2f s, render %.2f s\n", isatty(STDERR_FILENO) ? "\r\x1b[K" : "", render.width, render.height, renderer.drawn.samples, secondsOf(seconds, LoopStep::BuildWorld), secondsOf(seconds, LoopStep::DrawFrame));
}

Plugin sinkFilePlugin() {
	Hooks hooks        = noHooks();
	hooks.openRenderer = fileOpened;
	hooks.deliverFrame = fileDelivered;
	hooks.observed     = fileObserved;
	return Plugin {
		.name   = "sink/file",
		.point  = Point::Sink,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
