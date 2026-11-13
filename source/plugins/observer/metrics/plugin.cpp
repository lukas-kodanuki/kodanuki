#include "plugins/observer/metrics/plugin.h"

#include "core/batch.h"
#include "core/render.h"
#include "core/step.h"
#include "shared/image.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace kodanuki {

struct StepTally {
	double calls;
	double items;
	double seconds;
	std::uint32_t plugin;
};

struct WorkerTally {
	std::array<StepTally, stepCount> steps;
};

struct StepRow {
	int frame;
	int worker;
	LoopStep step;
	StepTally tally;
};

struct CounterRow {
	int frame;
	std::string plugin;
	std::string name;
	double value;
};

struct MetricsState {
	std::vector<WorkerTally> workers;
	std::vector<WorkerTally> taken;
	std::vector<StepRow> current;
	std::vector<StepRow> steps;
	std::vector<CounterRow> counters;
	std::vector<CounterRow> totals;
	std::vector<double> samples;
	std::vector<std::string> names;
	int frame;
};

MetricsState& metricsState(void* state) {
	return *static_cast<MetricsState*>(state);
}

StepTally noTally() {
	return StepTally {
		.calls   = 0,
		.items   = 0,
		.seconds = 0,
		.plugin  = corePlugin,
	};
}

WorkerTally noWorkerTally() {
	WorkerTally tally = {
		.steps = {},
	};

	tally.steps.fill(noTally());
	return tally;
}

std::vector<std::string> pluginNames() {
	std::vector<std::string> names;
	for (const Plugin& plugin : everyPlugin()) {
		names.push_back(plugin.name);
	}
	return names;
}

void metricsOpened(Renderer& renderer) {
	const std::size_t workers = std::size_t(std::max(renderer.options.choices.threads, 1));
	const std::shared_ptr<MetricsState> state(new MetricsState {
		.workers  = std::vector<WorkerTally>(workers, noWorkerTally()),
		.taken    = std::vector<WorkerTally>(workers, noWorkerTally()),
		.current  = {},
		.steps    = {},
		.counters = {},
		.totals   = {},
		.samples  = {},
		.names    = pluginNames(),
		.frame    = 0,
	});

	keptState(renderer, "observer/metrics", state);
}

void addedTally(StepTally& tally, const StepEvent& event) {
	tally.calls += 1;
	tally.items += event.items;
	tally.seconds += event.seconds;
	tally.plugin = event.plugin;
}

void metricsTallied(void* opaque, const StepEvent& event) {
	MetricsState& state = metricsState(opaque);
	if (std::size_t(event.worker) < state.workers.size()) {
		addedTally(state.workers[std::size_t(event.worker)].steps[std::size_t(event.step)], event);
	}
}

StepRow& frameRow(MetricsState& state, const StepEvent& event) {
	for (StepRow& row : state.current) {
		if (row.step == event.step && row.tally.plugin == event.plugin) {
			return row;
		}
	}
	StepTally tally = noTally();
	tally.plugin    = event.plugin;
	state.current.push_back(StepRow { .frame = state.frame, .worker = -1, .step = event.step, .tally = tally });
	return state.current.back();
}

void metricsObserved(void* opaque, const StepEvent& event) {
	MetricsState& state = metricsState(opaque);
	if (event.done && event.worker < 0) {
		addedTally(frameRow(state, event).tally, event);
	}
}

StepTally tallySince(const StepTally& now, const StepTally& then) {
	return StepTally {
		.calls   = now.calls - then.calls,
		.items   = now.items - then.items,
		.seconds = now.seconds - then.seconds,
		.plugin  = now.plugin,
	};
}

void takenWorkers(MetricsState& state) {
	for (std::size_t worker = 0; worker < state.workers.size(); ++worker) {
		for (std::size_t step = 0; step < std::size_t(stepCount); ++step) {
			const StepTally tally = tallySince(state.workers[worker].steps[step], state.taken[worker].steps[step]);
			if (tally.calls > 0) {
				state.steps.push_back(StepRow { .frame = state.frame, .worker = int(worker), .step = LoopStep(step), .tally = tally });
			}
		}
	}
	state.taken = state.workers;
}

void takenFrameSteps(MetricsState& state) {
	for (StepRow& row : state.current) {
		row.frame = state.frame;
		state.steps.push_back(row);
	}
	state.current.clear();
}

CounterRow& totalRow(MetricsState& state, const std::string& plugin, const std::string& name) {
	for (CounterRow& row : state.totals) {
		if (row.plugin == plugin && row.name == name) {
			return row;
		}
	}
	state.totals.push_back(CounterRow { .frame = 0, .plugin = plugin, .name = name, .value = 0 });
	return state.totals.back();
}

void takenCounter(MetricsState& state, const std::string& plugin, const Counter& counter) {
	CounterRow& total = totalRow(state, plugin, counter.name);
	state.counters.push_back(CounterRow { .frame = state.frame, .plugin = plugin, .name = counter.name, .value = counter.value - total.value });
	total.value = counter.value;
}

double poolBytes(const Renderer& renderer) {
	const double slot = double(sizeof(WalkPhase) + sizeof(std::uint8_t) + sizeof(RayKind) + sizeof(Ray) + sizeof(Hit) + sizeof(PixelSample));
	return slot * double(poolCapacity) * double(renderer.options.choices.threads);
}

double historyBytes(const History& history) {
	const double estimates = double(history.estimates.size() * sizeof(PixelSum));
	const double record    = double(history.record.parts.size() * sizeof(Parts) + history.record.surfaces.size() * sizeof(Surface));
	return estimates + record;
}

std::vector<Counter> coreCounters(const Renderer& renderer) {
	return {
		Counter { .name = "bytes.pool", .value = poolBytes(renderer) },
		Counter { .name = "bytes.history", .value = historyBytes(renderer.history) },
	};
}

std::string pluginUse(const std::string& name, const std::vector<std::string>& chosen) {
	if (std::find(chosen.begin(), chosen.end(), name) != chosen.end()) {
		return "chosen";
	}
	return switchedByUse(name) ? "off" : "built";
}

void takenCounters(MetricsState& state, const Renderer& renderer) {
	const std::vector<std::string> chosen = chosenPlugins(renderer.options);
	for (const Plugin& plugin : everyPlugin()) {
		if (plugin.hooks.published && pluginUse(plugin.name, chosen) != "off") {
			for (const Counter& counter : plugin.hooks.published(renderer)) {
				takenCounter(state, plugin.name, counter);
			}
		}
	}
	for (const Counter& counter : coreCounters(renderer)) {
		CounterRow& total = totalRow(state, "core", counter.name);
		total.value       = 0;
		takenCounter(state, "core", counter);
	}
}

void metricsDelivered(Renderer& renderer, const Image&) {
	MetricsState& state = metricsState(stateOf(renderer, "observer/metrics"));
	takenWorkers(state);
	takenFrameSteps(state);
	takenCounters(state, renderer);
	state.samples.push_back(renderer.drawn.samples);
	state.frame += 1;
}

const char* stepName(LoopStep step) {
	switch (step) {
		case LoopStep::NextFrame: return "NextFrame";
		case LoopStep::Evaluate: return "Evaluate";
		case LoopStep::BuildWorld: return "BuildWorld";
		case LoopStep::PrepareFrame: return "PrepareFrame";
		case LoopStep::DrawFrame: return "DrawFrame";
		case LoopStep::ReconstructFrame: return "ReconstructFrame";
		case LoopStep::DeliverFrame: return "DeliverFrame";
		case LoopStep::Preparations: return "Preparations";
		case LoopStep::Pass: return "Pass";
		case LoopStep::Surfaces: return "Surfaces";
		case LoopStep::Reconstruction: return "Reconstruction";
		case LoopStep::Keeping: return "Keeping";
		case LoopStep::OpenTile: return "OpenTile";
		case LoopStep::LaunchWalks: return "LaunchWalks";
		case LoopStep::ExtendRays: return "ExtendRays";
		case LoopStep::ArriveAtHits: return "ArriveAtHits";
		case LoopStep::ConnectLights: return "ConnectLights";
		case LoopStep::ScatterWalks: return "ScatterWalks";
		case LoopStep::FoldWalks: return "FoldWalks";
		case LoopStep::PlaceTile: return "PlaceTile";
	}

	return "NextFrame";
}

const char* pointName(Point point) {
	switch (point) {
		case Point::Source: return "Source";
		case Point::Geometry: return "Geometry";
		case Point::Preparation: return "Preparation";
		case Point::Stopping: return "Stopping";
		case Point::Execution: return "Execution";
		case Point::Transport: return "Transport";
		case Point::Reconstruction: return "Reconstruction";
		case Point::Sink: return "Sink";
		case Point::Observer: return "Observer";
		case Point::Backend: return "Backend";
		case Point::Owned: return "Owned";
	}

	return "Owned";
}

const char* effectName(Effect effect) {
	switch (effect) {
		case Effect::Exact: return "Exact";
		case Effect::Consistent: return "Consistent";
		case Effect::Biased: return "Biased";
	}

	return "Biased";
}

std::string rowPlugin(const MetricsState& state, std::uint32_t plugin) {
	return plugin < state.names.size() ? state.names[plugin] : "core";
}

std::string reportPath(const Options& options) {
	const std::string scene         = std::filesystem::path(options.arguments.values[1]).stem().string();
	const std::filesystem::path out = options.out;
	const bool frames               = std::find(options.sinks.begin(), options.sinks.end(), "sink/frames") != options.sinks.end();
	const std::filesystem::path dir = options.out.empty() ? "output" : (frames ? out : out.parent_path());
	std::filesystem::create_directories(dir.empty() ? "." : dir);
	return ((dir.empty() ? "." : dir) / (scene + ".metrics.json")).string();
}

double peakMegabytes() {
	struct rusage usage = {};
	getrusage(RUSAGE_SELF, &usage);
	return double(usage.ru_maxrss) / 1024;
}

void writtenHead(std::FILE* file, const Renderer& renderer, const MetricsState& state) {
	const Options& options = renderer.options;
	const std::string name = std::filesystem::path(options.arguments.values[1]).stem().string();
	std::fprintf(file, "{\n\"schema\": 1,\n\"scene\": \"%s\",\n\"width\": %d,\n\"height\": %d,\n", name.c_str(), options.width, options.height);
	std::fprintf(file, "\"threads\": %d,\n\"frames\": %d,\n\"peak_mb\": %.1f,\n", options.choices.threads, state.frame, peakMegabytes());
}

void writtenPlugins(std::FILE* file, const Renderer& renderer) {
	const std::vector<std::string> chosen = chosenPlugins(renderer.options);
	const std::vector<Plugin> plugins     = everyPlugin();
	std::fprintf(file, "\"plugins\": [");
	for (std::size_t k = 0; k < plugins.size(); ++k) {
		const Plugin& plugin = plugins[k];
		std::fprintf(file, "%s\n{\"name\": \"%s\", \"point\": \"%s\", \"effect\": \"%s\", \"use\": \"%s\"}", k == 0 ? "" : ",", plugin.name.c_str(), pointName(plugin.point), effectName(plugin.effect), pluginUse(plugin.name, chosen).c_str());
	}
	std::fprintf(file, "\n],\n");
}

void writtenSteps(std::FILE* file, const MetricsState& state) {
	std::fprintf(file, "\"steps\": [");
	for (std::size_t k = 0; k < state.steps.size(); ++k) {
		const StepRow& row = state.steps[k];
		std::fprintf(file, "%s\n{\"frame\": %d, \"worker\": %d, \"step\": \"%s\", \"plugin\": \"%s\", \"calls\": %.0f, \"items\": %.0f, \"seconds\": %.9g}", k == 0 ? "" : ",", row.frame, row.worker, stepName(row.step), rowPlugin(state, row.tally.plugin).c_str(), row.tally.calls, row.tally.items, row.tally.seconds);
	}
	std::fprintf(file, "\n],\n");
}

void writtenCounters(std::FILE* file, const MetricsState& state) {
	std::fprintf(file, "\"counters\": [");
	for (std::size_t k = 0; k < state.counters.size(); ++k) {
		const CounterRow& row = state.counters[k];
		std::fprintf(file, "%s\n{\"frame\": %d, \"plugin\": \"%s\", \"name\": \"%s\", \"value\": %.17g}", k == 0 ? "" : ",", row.frame, row.plugin.c_str(), row.name.c_str(), row.value);
	}
	std::fprintf(file, "\n],\n");
}

void writtenSamples(std::FILE* file, const MetricsState& state) {
	std::fprintf(file, "\"samples\": [");
	for (std::size_t k = 0; k < state.samples.size(); ++k) {
		std::fprintf(file, "%s\n{\"frame\": %d, \"samples\": %.9g}", k == 0 ? "" : ",", int(k), state.samples[k]);
	}
	std::fprintf(file, "\n]\n}\n");
}

void metricsClosed(Renderer& renderer) {
	MetricsState& state    = metricsState(stateOf(renderer, "observer/metrics"));
	const std::string path = reportPath(renderer.options);
	takenWorkers(state);
	takenFrameSteps(state);
	std::FILE* file = std::fopen(path.c_str(), "w");
	if (file == nullptr) {
		std::fprintf(stderr, "observer/metrics: cannot write %s\n", path.c_str());
		return;
	}
	writtenHead(file, renderer, state);
	writtenPlugins(file, renderer);
	writtenSteps(file, state);
	writtenCounters(file, state);
	writtenSamples(file, state);
	std::fclose(file);
}

Plugin observerMetricsPlugin() {
	Hooks hooks         = noHooks();
	hooks.openRenderer  = metricsOpened;
	hooks.observed      = metricsObserved;
	hooks.tallied       = metricsTallied;
	hooks.deliverFrame  = metricsDelivered;
	hooks.closeRenderer = metricsClosed;
	return Plugin {
		.name   = "observer/metrics",
		.point  = Point::Observer,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
