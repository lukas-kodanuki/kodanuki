#include "check.h"
#include "core/plugin.h"
#include "core/render.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace kodanuki {

void checkDenoise();
void checkExecution();
void checkFaces();
void checkGeneric();
void checkLanguage();
void checkLight();
void checkNumbers();
void checkReconstruction();
void checkRun();
void checkSampler();
void checkSequence();
void checkShading();
void checkSpectral();
void checkStopping();
void checkTransport();
void checkTree();
void checkUpscale();
void checkViewer();
void checkWalk();
void checkWeight();

int& failures() {
	static int count = 0;
	return count;
}

void check(bool passed, const char* what) {
	if (!passed) {
		std::printf("FAIL %s\n", what);
		failures() += 1;
	}
}

void checkNear(const Near& near) {
	if (!(std::abs(near.got - near.want) <= near.tolerance)) {
		std::printf("FAIL %s: got %.9g, want %.9g\n", near.what, near.got, near.want);
		failures() += 1;
	}
}

Options testOptions(const FrameInput& input) {
	Choices choices   = defaultChoices();
	choices.execution = input.execution;
	choices.threads   = input.threads;
	return Options {
		.sources        = {},
		.sinks          = {},
		.observers      = {},
		.choices        = choices,
		.out            = "",
		.pfm            = "",
		.time           = std::nullopt,
		.reconstruction = {},
		.perFrame       = input.perFrame,
		.factor         = 1,
		.width          = input.lens.width,
		.height         = input.lens.height,
		.arguments      = CommandLine { .count = 0, .values = nullptr },
	};
}

Renderer testRenderer(const FrameInput& input, const FrameStep& step) {
	const SceneFile file = {
		.scene    = input.geometry.scene,
		.looks    = input.looks,
		.lighting = input.lighting,
		.shot     = input.shot,
		.timed    = false,
	};

	const FrameState frame = {
		.index     = 0,
		.time      = 0,
		.camera    = input.shot.camera,
		.change    = Change::None,
		.recounted = false,
		.restarted = false,
	};

	const Drawn drawn = {
		.image   = blankImage(0, 0),
		.samples = 0,
		.rounds  = 0,
	};

	Renderer renderer = {
		.file            = file,
		.options         = testOptions(input),
		.geometry        = testPlugin("geometry/tree"),
		.transport       = input.transport,
		.execution       = testPlugin(input.execution),
		.backends        = testPlugins({ "backend/cpu" }),
		.fallback        = testPlugin("backend/cpu"),
		.sources         = {},
		.sinks           = {},
		.observers       = {},
		.preparations    = input.preparations,
		.stopping        = input.stopping,
		.reconstructions = {},
		.frame           = frame,
		.world           = World { .geometry = input.geometry },
		.step            = step,
		.history         = emptyHistory(),
		.drawn           = drawn,
		.drawing         = idleDrawing(),
		.observing       = input.observing,
		.states          = States { .held = {} },
	};

	keptState(renderer, input.transport.name, std::shared_ptr<void>(std::shared_ptr<void>(), const_cast<void*>(input.transportWorld)));
	return renderer;
}

Framed framedBy(const FrameInput& input, const FrameStep& step, const std::vector<Plugin>& reconstruction, History history) {
	Renderer renderer        = testRenderer(input, step);
	renderer.reconstructions = reconstruction;
	renderer.history         = std::move(history);
	drawnFrame(renderer);
	return Framed {
		.image   = std::move(renderer.drawn.image),
		.history = std::move(renderer.history),
		.samples = renderer.drawn.samples,
		.rounds  = renderer.drawn.rounds,
	};
}

Framed stillFrame(const FrameInput& input) {
	const StepQuery first = {
		.lens        = input.lens,
		.film        = stillFilm(),
		.change      = Change::None,
		.keepsGuides = false,
	};

	return framedBy(input, firstStep(first), testPlugins({ "reconstruction/mean" }), emptyHistory());
}

ChosenMembers coreChosen() {
	return ChosenMembers {
		.sampling   = coreSampling(),
		.strategies = coreStrategies(),
	};
}

ChosenMembers defaultChosen() {
	ChosenMembers chosen = {
		.sampling   = defaultSampling(),
		.strategies = coreStrategies(),
	};

	chosen.strategies.members.push_back(StrategyMember::Join);
	return chosen;
}

const Plugin& testPlugin(const std::string& name) {
	static std::map<std::string, Plugin> plugins;
	if (plugins.count(name) == 0) {
		plugins.emplace(name, pluginNamed(name));
	}
	return plugins.at(name);
}

std::vector<Plugin> testPlugins(const std::vector<std::string>& names) {
	std::vector<Plugin> plugins;
	for (const std::string& name : names) {
		plugins.push_back(testPlugin(name));
	}
	return plugins;
}

std::vector<Plugin> testPreparations(const std::vector<std::string>& names) {
	return orderedPreparations(testPlugins(names));
}

std::vector<std::string> defaultPreparations() {
	return { "sampler/sky_map", "sampler/glowing_surfaces", "sampler/toward_mirrors", "sampler/highlight_guide", "strategy/join" };
}

const char* writeScene(const char* name, const char* text) {
	static std::string path;
	const char* directory = std::getenv("TMPDIR");
	path                  = std::string(directory ? directory : "/tmp") + "/kodanuki_" + name + ".sdf";
	std::ofstream(path) << text;
	return path.c_str();
}

}

int main() {
	kodanuki::checkLanguage();
	kodanuki::checkNumbers();
	kodanuki::checkTree();
	kodanuki::checkFaces();
	kodanuki::checkWalk();
	kodanuki::checkSpectral();
	kodanuki::checkShading();
	kodanuki::checkLight();
	kodanuki::checkWeight();
	kodanuki::checkTransport();
	kodanuki::checkExecution();
	kodanuki::checkSampler();
	kodanuki::checkStopping();
	kodanuki::checkSequence();
	kodanuki::checkReconstruction();
	kodanuki::checkDenoise();
	kodanuki::checkUpscale();
	kodanuki::checkRun();
	kodanuki::checkViewer();
	kodanuki::checkGeneric();
	if (kodanuki::failures() == 0) {
		std::printf("all checks passed\n");
	}
	return kodanuki::failures() == 0 ? 0 : 1;
}
