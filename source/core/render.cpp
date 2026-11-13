#include "core/render.h"

#include "core/camera.h"
#include "core/frame.h"
#include "core/reconstruction.h"
#include "core/shown.h"
#include "sdf/compare.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace kodanuki {

std::vector<Plugin> pluginsAt(const std::vector<std::string>& names, Point point) {
	std::vector<Plugin> plugins;
	for (const std::string& name : names) {
		const Plugin plugin = pluginNamed(name);
		if (plugin.point != point) {
			std::fprintf(stderr, "kodanuki: %s is not a plugin of the point it is named for\n", name.c_str());
			std::exit(1);
		}
		plugins.push_back(plugin);
	}
	return plugins;
}

std::vector<Plugin> builtPlugins(const std::vector<std::string>& names) {
	std::vector<Plugin> plugins;
	for (const std::string& name : names) {
		if (pluginBuilt(name)) {
			plugins.push_back(pluginNamed(name));
		}
	}
	return plugins;
}

Render drawnRender(const Renderer& renderer) {
	Render render = renderer.file.shot.render;
	render.width  = renderer.options.width;
	render.height = renderer.options.height;
	return render;
}

FrameStep openingStep(const SceneFile& file, const Options& options) {
	Render render   = file.shot.render;
	render.width    = options.width;
	render.height   = options.height;
	const Lens lens = lensOf(file.shot.camera, render);

	const StepQuery query = {
		.lens        = lens,
		.film        = bayerFilm(options.factor, 0),
		.change      = Change::None,
		.keepsGuides = false,
	};

	return firstStep(query);
}

std::vector<const std::vector<Plugin>*> loopPlugins(const Renderer& renderer) {
	return { &renderer.observers, &renderer.sinks, &renderer.sources };
}

void watchedBy(const Renderer& renderer, const Plugin& plugin, Observing& observing) {
	void* state = stateOf(renderer, plugin.name);
	if (plugin.hooks.observed) {
		observing.frames.push_back(Watcher { .observed = plugin.hooks.observed, .state = state });
	}
	if (plugin.hooks.tallied) {
		observing.workers.push_back(Watcher { .observed = plugin.hooks.tallied, .state = state });
	}
}

void refusedNeeds(const std::vector<std::string>& chosen, const Plugin& plugin) {
	for (const std::string& name : plugin.hooks.needed()) {
		if (std::find(chosen.begin(), chosen.end(), name) == chosen.end()) {
			std::fprintf(stderr, "kodanuki: %s needs %s, which this run does not choose\n", plugin.name.c_str(), name.c_str());
			std::exit(1);
		}
	}
}

void checkedNeeds(const Options& options) {
	const std::vector<std::string> chosen = chosenPlugins(options);
	for (const std::string& name : chosen) {
		if (pluginBuilt(name) && pluginNamed(name).hooks.needed) {
			refusedNeeds(chosen, pluginNamed(name));
		}
	}
}

int openRenderer(Renderer& renderer) {
	for (const std::vector<Plugin>* plugins : loopPlugins(renderer)) {
		for (const Plugin& plugin : *plugins) {
			if (plugin.hooks.openRenderer) {
				plugin.hooks.openRenderer(renderer);
			}
		}
	}
	for (const std::vector<Plugin>* plugins : loopPlugins(renderer)) {
		for (const Plugin& plugin : *plugins) {
			watchedBy(renderer, plugin, renderer.observing);
		}
	}
	return 0;
}

bool nextSource(Renderer& renderer, const Plugin& source) {
	const std::uint32_t plugin = pluginIndex(source.name);
	const double begun         = frameStepBegun(renderer.observing, LoopStep::NextFrame, plugin, 0);
	const bool next            = source.hooks.nextFrame(renderer);
	frameStepEnded(renderer.observing, LoopStep::NextFrame, plugin, 0, begun);
	return next;
}

Change sceneChange(const SceneFile& before, const SceneFile& next) {
	const Change lights = sameLighting(before.lighting, next.lighting) ? Change::None : Change::Lights;
	const Change looks  = sameLooks(before.looks, next.looks) ? Change::None : Change::Looks;
	const Change shapes = sameShapes(before.scene, next.scene) ? Change::None : Change::Shapes;
	return joinedChange(lights, joinedChange(looks, shapes));
}

void sceneTaken(Renderer& renderer, SceneFile next) {
	SceneFile& file          = renderer.file;
	const Change change      = sceneChange(file, next);
	renderer.frame.change    = joinedChange(renderer.frame.change, change);
	renderer.frame.recounted = renderer.frame.recounted || file.scene.leaves.size() != next.scene.leaves.size();
	if (changedBy(change, Change::Shapes)) {
		file.scene = std::move(next.scene);
	}
	file.looks    = std::move(next.looks);
	file.lighting = std::move(next.lighting);
	file.shot     = std::move(next.shot);
	file.timed    = next.timed;
}

[[noreturn]] void refusedScene(const std::string& message) {
	std::fprintf(stderr, "%s\n", message.c_str());
	std::exit(1);
}

SceneFile sceneEvaluated(const Renderer& renderer, double time) {
	const std::string path = renderer.options.arguments.values[1];
	LoadedScene loaded     = loadScene(path, time);
	if (!loaded.error.empty()) {
		refusedScene(loaded.error);
	}
	SceneFile next          = reloadedFile(std::move(loaded.file), renderer.options);
	next.shot.render.width  = renderer.file.shot.render.width;
	next.shot.render.height = renderer.file.shot.render.height;
	if (!sameSettings(renderer.file.shot, next.shot)) {
		refusedScene(path + ": render and clip must come out the same at every time; at time " + std::to_string(time) + " they differ");
	}
	return next;
}

void sceneAt(Renderer& renderer, double time) {
	if (time == renderer.frame.time) {
		return;
	}
	renderer.frame.time = time;
	if (!renderer.file.timed) {
		return;
	}
	const double begun = frameStepBegun(renderer.observing, LoopStep::Evaluate, corePlugin, 0);
	sceneTaken(renderer, sceneEvaluated(renderer, time));
	frameStepEnded(renderer.observing, LoopStep::Evaluate, corePlugin, 0, begun);
}

bool nextFrame(Renderer& renderer) {
	renderer.frame.change    = Change::None;
	renderer.frame.recounted = false;
	for (const Plugin& source : renderer.sources) {
		if (!nextSource(renderer, source)) {
			return false;
		}
	}
	return true;
}

World builtWorld(Renderer& renderer) {
	renderer.transport.hooks.openWorld(renderer);
	return World {
		.geometry = renderer.geometry.hooks.buildWorld(renderer),
	};
}

bool rebuildsWorld(const Renderer& renderer) {
	return !renderer.world || changedBy(renderer.frame.change, joinedChange(Change::Cut, Change::Shapes));
}

void restartedHistory(Renderer& renderer) {
	if (!renderer.world || changedBy(renderer.frame.change, Change::Cut)) {
		renderer.history         = emptyHistory();
		renderer.frame.restarted = true;
	}
}

void buildWorld(Renderer& renderer) {
	if (!rebuildsWorld(renderer)) {
		if (changedBy(renderer.frame.change, joinedChange(Change::Lights, Change::Looks))) {
			renderer.transport.hooks.openWorld(renderer);
		}
		return;
	}
	const std::uint32_t plugin = pluginIndex(renderer.geometry.name);
	const double begun         = frameStepBegun(renderer.observing, LoopStep::BuildWorld, plugin, 0);
	restartedHistory(renderer);
	renderer.world.reset();
	renderer.world.emplace(builtWorld(renderer));
	frameStepEnded(renderer.observing, LoopStep::BuildWorld, plugin, 0, begun);
}

void prepareFrame(Renderer& renderer) {
	const double begun     = frameStepBegun(renderer.observing, LoopStep::PrepareFrame, corePlugin, 0);
	const Options& options = renderer.options;
	const Lens lens        = lensOf(renderer.frame.camera, drawnRender(renderer));
	const bool restarted   = renderer.frame.restarted;

	const StepQuery query = {
		.lens        = lens,
		.film        = bayerFilm(options.factor, restarted ? 0 : renderer.step.index + 1),
		.change      = renderer.frame.change,
		.keepsGuides = options.reconstruction != defaultReconstruction(),
	};

	renderer.step            = restarted ? firstStep(query) : nextStep(renderer.step, query);
	renderer.frame.restarted = false;
	frameStepEnded(renderer.observing, LoopStep::PrepareFrame, corePlugin, 0, begun);
}

void drawFrame(Renderer& renderer) {
	const std::uint32_t tiles = frameTiles(renderer);
	const double begun        = frameStepBegun(renderer.observing, LoopStep::DrawFrame, corePlugin, tiles);
	drawnFrame(renderer);
	frameStepEnded(renderer.observing, LoopStep::DrawFrame, corePlugin, tiles, begun);
}

Image reconstructFrame(Renderer& renderer) {
	const double begun = frameStepBegun(renderer.observing, LoopStep::ReconstructFrame, corePlugin, 0);
	Image shown        = std::move(renderer.drawn.image);
	frameStepEnded(renderer.observing, LoopStep::ReconstructFrame, corePlugin, 0, begun);
	return shown;
}

void deliveredTo(Renderer& renderer, const Plugin& plugin, const Image& shown) {
	const std::uint32_t index = pluginIndex(plugin.name);
	const double begun        = frameStepBegun(renderer.observing, LoopStep::DeliverFrame, index, 0);
	plugin.hooks.deliverFrame(renderer, shown);
	frameStepEnded(renderer.observing, LoopStep::DeliverFrame, index, 0, begun);
}

void deliverFrame(Renderer& renderer, const Image& shown) {
	for (const std::vector<Plugin>* plugins : { &renderer.sinks, &renderer.observers }) {
		for (const Plugin& plugin : *plugins) {
			if (plugin.hooks.deliverFrame) {
				deliveredTo(renderer, plugin, shown);
			}
		}
	}
	renderer.frame.index += 1;
}

int closeRenderer(Renderer& renderer) {
	renderer.world.reset();
	for (const std::vector<Plugin>* plugins : loopPlugins(renderer)) {
		for (const Plugin& plugin : *plugins) {
			if (plugin.hooks.closeRenderer) {
				plugin.hooks.closeRenderer(renderer);
			}
		}
	}
	return 0;
}

int runFrames(Renderer& renderer) {
	while (nextFrame(renderer)) {
		buildWorld(renderer);
		prepareFrame(renderer);
		drawFrame(renderer);
		deliverFrame(renderer, reconstructFrame(renderer));
	}
	return closeRenderer(renderer);
}

int hostedFrames(Renderer& renderer) {
	for (const Plugin& sink : renderer.sinks) {
		if (sink.hooks.hostRenderer) {
			return sink.hooks.hostRenderer(renderer);
		}
	}
	return runFrames(renderer);
}

Renderer rendererOf(SceneFile file, const Options& options) {
	checkedNeeds(options);
	const FrameStep step = openingStep(file, options);

	const FrameState frame = {
		.index     = 0,
		.time      = options.time.value_or(0),
		.camera    = file.shot.camera,
		.change    = Change::None,
		.recounted = false,
		.restarted = true,
	};

	const Drawn drawn = {
		.image   = blankImage(0, 0),
		.samples = 0,
		.rounds  = 0,
	};

	Renderer renderer = {
		.file            = std::move(file),
		.options         = options,
		.geometry        = pluginsAt({ options.choices.geometry }, Point::Geometry).front(),
		.transport       = pluginsAt({ options.choices.transport }, Point::Transport).front(),
		.execution       = pluginsAt({ options.choices.execution }, Point::Execution).front(),
		.backends        = pluginsAt({ options.choices.backend }, Point::Backend),
		.fallback        = pluginsAt({ fallbackBackend() }, Point::Backend).front(),
		.sources         = pluginsAt(options.sources, Point::Source),
		.sinks           = pluginsAt(options.sinks, Point::Sink),
		.observers       = pluginsAt(options.observers, Point::Observer),
		.preparations    = orderedPreparations(builtPlugins(chosenPlugins(options))),
		.stopping        = pluginsAt({ options.choices.stopping }, Point::Stopping).front(),
		.reconstructions = pluginsAt(options.reconstruction, Point::Reconstruction),
		.frame           = frame,
		.world           = std::nullopt,
		.step            = step,
		.history         = emptyHistory(),
		.drawn           = drawn,
		.drawing         = idleDrawing(),
		.observing       = noObserving(),
		.states          = States { .held = {} },
	};

	return renderer;
}

int rendered(Renderer& renderer) {
	openRenderer(renderer);
	return hostedFrames(renderer);
}

int render(SceneFile file, const Options& options) {
	Renderer renderer = rendererOf(std::move(file), options);
	return rendered(renderer);
}

void* stateOf(const Renderer& renderer, const std::string& name) {
	return stateIn(renderer.states, name);
}

void keptState(Renderer& renderer, const std::string& name, std::shared_ptr<void> data) {
	const State state = {
		.name = name,
		.data = std::move(data),
	};

	renderer.states.held.push_back(state);
}

} // namespace kodanuki
