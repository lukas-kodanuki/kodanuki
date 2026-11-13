#include "core/options.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace kodanuki {

SceneFile sceneAtOrExit(const std::string& path, double time) {
	LoadedScene loaded = loadScene(path, time);
	if (!loaded.error.empty()) {
		std::fprintf(stderr, "%s\n", loaded.error.c_str());
		std::exit(1);
	}
	return std::move(loaded.file);
}

SceneFile sceneOrExit(const std::string& path) {
	return sceneAtOrExit(path, 0);
}

UseKey useKey(const std::string& name, UseForm form, const std::vector<std::string>& members, const std::string& meaning) {
	return UseKey {
		.name    = name,
		.form    = form,
		.members = members,
		.meaning = meaning,
	};
}

std::vector<UseKey> pluginKeys() {
	return {
		useKey("geometry", UseForm::One, { "tree", "sphere_trace" }, "what rays meet: the tree of exact faces, or the scene's field sphere-traced (with execution=single and a sampler without glowing_surfaces and toward_mirrors)"),
		useKey("execution", UseForm::One, { "single", "packets" }, "how rays are traced, one by one or in packets (with geometry=tree); the frame is the same within noise"),
		useKey("backend", UseForm::One, { "cpu" }, "what the workers run the steps on: the CPU's threads"),
		useKey("stopping", UseForm::One, { "fixed_count", "confidence_interval" }, "when a pixel is done: after --samples, or once its noise is within --tolerance"),
		useKey("detail", UseForm::Joined, { "focus_on_view", "pilot", "split_hidden_primitives", "split_crowded_leaves" }, "what says how fine the tree's cells must be, besides their distance"),
		useKey("build", UseForm::Joined, { "split_beside_finer", "cut_long_programs", "face_slabs", "shared_coefficients", "shared_subtrees" }, "the build passes that rewrite the tree"),
		useKey("strategy", UseForm::Joined, { "join", "connect" }, "how paths are made besides meeting light and aiming at it"),
		useKey("sampler", UseForm::Joined, { "sobol", "highlight_guide", "each_lamp_first", "sky_map", "glowing_surfaces", "toward_mirrors" }, "how numbers, directions and lights are drawn; without any, independent numbers, material directions, one light by contribution, aimed at lamps"),
	};
}

std::vector<UseKey> loopKeys() {
	return {
		useKey("source", UseForm::Listed, { "scene", "flight" }, "where frames come from"),
		useKey("sink", UseForm::Listed, { "file", "frames", "window" }, "where frames go"),
		useKey("observer", UseForm::Listed, { "progress", "metrics" }, "what watches the steps"),
		useKey("reconstruction", UseForm::Listed, { "mean", "temporal", "atrous", "upsample", "upscale" }, "what turns the drawn frames into the shown image"),
	};
}

std::vector<UseKey> settingKeys() {
	return {
		useKey("faces", UseForm::One, { "on", "off" }, "whether the tree derives exact faces in its cells"),
		useKey("smooth_glass", UseForm::One, { "exact", "lobe" }, "glass of roughness 0: an exact mirror and passage, or the narrowest lobe"),
	};
}

std::vector<UseKey> useKeys() {
	std::vector<UseKey> keys          = pluginKeys();
	const std::vector<UseKey> loop    = loopKeys();
	const std::vector<UseKey> setting = settingKeys();
	keys.insert(keys.end(), loop.begin(), loop.end());
	keys.insert(keys.end(), setting.begin(), setting.end());
	return keys;
}

UseKey useKeyNamed(const std::string& name) {
	const std::vector<UseKey> keys = useKeys();
	return *std::find_if(keys.begin(), keys.end(), [&](const UseKey& key) {
		return key.name == name;
	});
}

std::string joinedNames(const std::vector<std::string>& names, const std::string& between) {
	std::string joined;
	for (const std::string& name : names) {
		joined += joined.empty() ? name : between + name;
	}
	return joined;
}

std::string membersText(const UseKey& key) {
	return key.form == UseForm::One ? joinedNames(key.members, " | ") : "none | any of " + joinedNames(key.members, "+");
}

std::vector<std::string> ownedKeys() {
	return { "detail", "build", "strategy", "sampler" };
}

std::vector<std::string> pluginsOfKey(const std::string& key, const std::vector<std::string>& names) {
	std::vector<std::string> plugins;
	for (const std::string& name : names) {
		plugins.push_back(key + "/" + name);
	}
	return plugins;
}

std::vector<std::string> defaultOwned() {
	std::vector<std::string> owned;
	for (const std::string& key : ownedKeys()) {
		const std::vector<std::string> plugins = pluginsOfKey(key, useKeyNamed(key).members);
		owned.insert(owned.end(), plugins.begin(), plugins.end());
	}
	std::erase(owned, "strategy/connect");
	return owned;
}

Choices defaultChoices() {
	const Region none = {
		.lower = { 0, 0, 0 },
		.size  = { 0, 0, 0 },
	};

	return Choices {
		.geometry    = "geometry/tree",
		.transport   = "transport/paths",
		.execution   = "execution/packets",
		.backend     = "backend/cpu",
		.stopping    = "stopping/confidence_interval",
		.owned       = defaultOwned(),
		.smoothGlass = "exact",
		.faces       = true,
		.framed      = false,
		.region      = none,
		.least       = 0,
		.depth       = -1,
		.threads     = 8,
	};
}

std::string fallbackBackend() {
	return "backend/cpu";
}

std::string takesText(const UseKey& key) {
	return "--use " + key.name + "= takes " + membersText(key) + " (" + key.meaning + ")";
}

[[noreturn]] void refuse(const std::string& what) {
	std::fprintf(stderr, "kodanuki: %s\n", what.c_str());
	std::exit(1);
}

std::vector<std::string> namesIn(const std::string& value) {
	std::vector<std::string> names;
	std::size_t start = 0;
	while (value != "none" && start <= value.size()) {
		const std::size_t plus = std::min(value.find('+', start), value.size());
		names.push_back(value.substr(start, plus - start));
		start = plus + 1;
	}
	return names;
}

std::vector<std::string> inKeyOrder(const UseKey& key, const std::vector<std::string>& named) {
	std::vector<std::string> members;
	for (const std::string& name : key.members) {
		if (std::find(named.begin(), named.end(), name) != named.end()) {
			members.push_back(name);
		}
	}
	return members;
}

std::vector<std::string> keyMembers(const UseKey& key, const std::string& value) {
	const std::vector<std::string> named = namesIn(value);
	if (key.form == UseForm::One && named.size() != 1) {
		refuse(takesText(key) + ", not " + value);
	}
	for (const std::string& name : named) {
		if (std::find(key.members.begin(), key.members.end(), name) == key.members.end()) {
			refuse(takesText(key) + ", not " + value);
		}
	}
	return key.form == UseForm::Joined ? inKeyOrder(key, named) : named;
}

std::vector<std::string> ownedAt(const std::vector<std::string>& owned, const std::string& key) {
	std::vector<std::string> plugins;
	for (const std::string& name : owned) {
		if (name.rfind(key + "/", 0) == 0) {
			plugins.push_back(name);
		}
	}
	return plugins;
}

std::vector<std::string> ownedWith(const std::vector<std::string>& owned, const std::string& key, const std::vector<std::string>& names) {
	std::vector<std::string> next;
	for (const std::string& point : ownedKeys()) {
		const std::vector<std::string> kept = point == key ? pluginsOfKey(key, names) : ownedAt(owned, point);
		next.insert(next.end(), kept.begin(), kept.end());
	}
	return next;
}

std::string useKeysText() {
	std::vector<std::string> keys;
	for (const UseKey& key : useKeys()) {
		keys.push_back(key.name + "=" + membersText(key));
	}
	return "--use takes " + joinedNames(keys, ", ");
}

std::optional<UseKey> keyNamedIn(const std::vector<UseKey>& keys, const std::string& name) {
	for (const UseKey& key : keys) {
		if (key.name == name) {
			return key;
		}
	}
	return std::nullopt;
}

std::string* singleChoice(Choices& choices, const std::string& name) {
	if (name == "geometry") {
		return &choices.geometry;
	}
	if (name == "execution") {
		return &choices.execution;
	}
	if (name == "backend") {
		return &choices.backend;
	}
	return name == "stopping" ? &choices.stopping : nullptr;
}

void readSettingUse(Choices& choices, const std::string& name, const std::string& member) {
	if (name == "faces") {
		choices.faces = member == "on";
		return;
	}
	choices.smoothGlass = member;
}

void readUse(Choices& choices, const std::string& use) {
	const std::size_t equals           = use.find('=');
	const std::string name             = use.substr(0, equals);
	const std::optional<UseKey> plugin = keyNamedIn(pluginKeys(), name);
	const std::optional<UseKey> key    = plugin ? plugin : keyNamedIn(settingKeys(), name);
	if (!key || equals == std::string::npos) {
		refuse(useKeysText() + "; not " + use);
	}
	const std::vector<std::string> names = keyMembers(*key, use.substr(equals + 1));
	std::string* single                  = singleChoice(choices, name);
	if (!plugin) {
		readSettingUse(choices, name, names.front());
	} else if (single) {
		*single = name + "/" + names.front();
	} else {
		choices.owned = ownedWith(choices.owned, name, names);
	}
}

Choices withUse(const Choices& choices, const std::string& use) {
	Choices chosen = choices;
	readUse(chosen, use);
	return chosen;
}

int splitArgument(const std::string& value) {
	const int split = std::stoi(value);
	if (split != 0 && split != 1 && split != 2 && split != 4 && split != 8) {
		std::fprintf(stderr, "--split: 0, 1, 2, 4 or 8\n");
		std::exit(1);
	}
	return split;
}

bool readSetting(Shot& shot, char** argv, int& i) {
	const std::string flag = argv[i];
	Render& render         = shot.render;
	if (flag == "--size") {
		render.width  = std::stoi(argv[++i]);
		render.height = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--seed") {
		render.seed = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--tolerance") {
		render.tolerance = std::stof(argv[++i]);
		return true;
	}
	if (flag == "--samples") {
		render.samples = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--batch") {
		render.batch = std::max(0, std::stoi(argv[++i]));
		return true;
	}
	if (flag == "--split") {
		render.split = splitArgument(argv[++i]);
		return true;
	}
	if (flag == "--surfaces") {
		render.surfaces = std::max(1, std::stoi(argv[++i]));
		return true;
	}
	if (flag == "--collisions") {
		render.collisions = std::max(1, std::stoi(argv[++i]));
		return true;
	}
	return false;
}

Region regionArgument(char** argv, int at) {
	Region region = {
		.lower = { 0, 0, 0 },
		.size  = { 0, 0, 0 },
	};

	for (int axis = 0; axis < 3; ++axis) {
		region.lower[axis] = std::stod(argv[at + axis]);
		region.size[axis]  = std::stod(argv[at + 3 + axis]) - region.lower[axis];
	}
	return region;
}

bool readChoice(Choices& choices, const CommandLine& arguments, int& i) {
	const int argc         = arguments.count;
	char** argv            = arguments.values;
	const std::string flag = argv[i];
	if (flag == "--use" && i + 1 < argc) {
		choices = withUse(choices, argv[++i]);
		return true;
	}
	if (flag == "--region" && i + 6 < argc) {
		choices.region = regionArgument(argv, i + 1);
		choices.framed = true;
		i += 6;
		return true;
	}
	if (flag == "--least" && i + 1 < argc) {
		choices.least = std::stod(argv[++i]);
		return true;
	}
	if (flag == "--depth" && i + 1 < argc) {
		choices.depth = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--threads" && i + 1 < argc) {
		choices.threads = std::max(1, std::stoi(argv[++i]));
		return true;
	}
	return false;
}

int viewerThreads() {
	const int cores = int(std::thread::hardware_concurrency());
	return std::max(1, cores / 2);
}

std::vector<std::string> defaultReconstruction() {
	return { "reconstruction/mean" };
}

std::vector<std::string> upscaledReconstruction() {
	return { "reconstruction/temporal", "reconstruction/atrous", "reconstruction/upscale" };
}

Choices programChoices(bool windowed) {
	Choices choices = defaultChoices();
	if (windowed) {
		choices.threads = viewerThreads();
		choices.owned   = ownedWith(choices.owned, "strategy", {});
	}
	return choices;
}

Options defaultOptions(const CommandLine& arguments, bool windowed) {
	return Options {
		.sources        = {},
		.sinks          = {},
		.observers      = windowed ? std::vector<std::string> {} : std::vector<std::string> { "observer/progress" },
		.choices        = programChoices(windowed),
		.out            = {},
		.pfm            = {},
		.time           = std::nullopt,
		.reconstruction = windowed ? upscaledReconstruction() : defaultReconstruction(),
		.perFrame       = 0,
		.factor         = 1,
		.width          = 0,
		.height         = 0,
		.arguments      = arguments,
	};
}

void refusedOption(const std::string& message) {
	std::fprintf(stderr, "%s\n", message.c_str());
	std::exit(1);
}

std::vector<std::string> trackReconstruction(const std::string& name) {
	if (name == "mean") {
		return defaultReconstruction();
	}
	if (name == "temporal") {
		return { "reconstruction/temporal" };
	}
	if (name == "temporal+denoise") {
		return { "reconstruction/temporal", "reconstruction/atrous" };
	}
	if (name == "temporal+denoise+upscale") {
		return upscaledReconstruction();
	}
	refusedOption("--reconstruction: mean, temporal, temporal+denoise or temporal+denoise+upscale");
	return {};
}

int trackFactor(const std::string& value) {
	const int factor = std::stoi(value);
	if (factor < 1 || factor > 4) {
		refusedOption("--factor: 1, 2, 3 or 4");
	}
	return factor;
}

Options readOutput(const Options& options, const std::string& flag, const std::string& value) {
	Options read        = options;
	read.out            = flag == "--out" ? value : read.out;
	read.pfm            = flag == "--pfm" ? value : read.pfm;
	read.time           = flag == "--time" ? std::optional<double>(std::stod(value)) : read.time;
	read.reconstruction = flag == "--reconstruction" ? trackReconstruction(value) : read.reconstruction;
	read.factor         = flag == "--factor" ? trackFactor(value) : read.factor;
	return read;
}

std::vector<std::string>* loopPlugins(Options& options, const std::string& point) {
	if (point == "source") {
		return &options.sources;
	}
	if (point == "sink") {
		return &options.sinks;
	}
	if (point == "reconstruction") {
		return &options.reconstruction;
	}
	return point == "observer" ? &options.observers : nullptr;
}

bool readPlugins(Options& options, const CommandLine& arguments, int& i) {
	const std::string flag           = arguments.values[i];
	const std::string use            = arguments.values[i + 1];
	const std::string point          = use.substr(0, use.find('='));
	std::vector<std::string>* chosen = loopPlugins(options, point);
	if (flag != "--use" || use.find('=') == std::string::npos || chosen == nullptr) {
		return false;
	}
	*chosen = pluginsOfKey(point, keyMembers(useKeyNamed(point), use.substr(use.find('=') + 1)));
	++i;
	return true;
}

bool readInternal(Render& internal, char** argv, int& i) {
	if (std::string(argv[i]) != "--internal") {
		return false;
	}
	internal.width  = std::stoi(argv[++i]);
	internal.height = std::stoi(argv[++i]);
	return true;
}

bool playsClip(const SceneFile& file, const Options& options) {
	return file.shot.clip.duration > 0 && !options.time;
}

std::vector<std::string> defaultSinks(const Options& options, bool windowed, bool clip) {
	if (windowed) {
		return options.out.empty() ? std::vector<std::string> { "sink/window" } : std::vector<std::string> { "sink/window", "sink/frames" };
	}
	return { clip ? "sink/frames" : "sink/file" };
}

Options withPlugins(const SceneFile& file, const Options& options, bool windowed) {
	const std::vector<std::string> own = windowed ? std::vector<std::string> { "source/scene", "source/flight" } : std::vector<std::string> { "source/scene" };
	Options chosen                     = options;
	chosen.sources                     = chosen.sources.empty() ? own : chosen.sources;
	chosen.sinks                       = chosen.sinks.empty() ? defaultSinks(options, windowed, playsClip(file, options)) : chosen.sinks;
	return chosen;
}

int internalFactor(const Render& output, const Render& internal) {
	const bool fits = internal.width > 0 && internal.height > 0 && output.width % internal.width == 0 && output.height % internal.height == 0;
	const int k     = fits ? output.width / internal.width : 0;
	if (k < 1 || k > 4 || output.height != internal.height * k) {
		refusedOption("--internal: --size must be 1, 2, 3 or 4 times it on both sides");
	}
	return k;
}

Options windowSized(Shot& shot, const Options& options, const Render& internal) {
	Render& render     = shot.render;
	const Render drawn = internal.width > 0 ? internal : render;
	Options sized      = options;
	sized.factor       = internalFactor(render, drawn);
	sized.width        = drawn.width;
	sized.height       = drawn.height;
	render.width       = drawn.width;
	render.height      = drawn.height;
	return sized;
}

void checkedFactor(const Render& render, const Options& options) {
	if (options.factor > 1 && options.reconstruction != upscaledReconstruction()) {
		refusedOption("--factor: 2 to 4 need --reconstruction temporal+denoise+upscale");
	}
	if (render.width % options.factor != 0 || render.height % options.factor != 0) {
		refusedOption("--factor: the size must be a multiple of the factor");
	}
}

Options fileSized(const Shot& shot, const Options& options) {
	Options sized = options;
	sized.factor  = options.factor;
	checkedFactor(shot.render, sized);
	sized.width  = shot.render.width / sized.factor;
	sized.height = shot.render.height / sized.factor;
	return sized;
}

bool namesConfidence(const CommandLine& arguments) {
	for (int i = 2; i + 1 < arguments.count; ++i) {
		if (std::string(arguments.values[i]) == "--use" && std::string(arguments.values[i + 1]) == "stopping=confidence_interval") {
			return true;
		}
	}
	return false;
}

int samplesPerFrame(const Options& options, const CommandLine& arguments) {
	const bool still = options.sources == std::vector<std::string> { "source/scene" };
	return still || namesConfidence(arguments) ? 0 : 1;
}

Options readOptions(SceneFile& file, const CommandLine& arguments, bool windowed) {
	Options options = defaultOptions(arguments, windowed);
	Render internal = file.shot.render;
	internal.width  = 0;
	internal.height = 0;
	for (int i = 2; i + 1 < arguments.count; ++i) {
		const bool sized = windowed && readInternal(internal, arguments.values, i);
		if (sized || readSetting(file.shot, arguments.values, i) || readPlugins(options, arguments, i) || readChoice(options.choices, arguments, i)) {
			continue;
		}
		options = readOutput(options, arguments.values[i], arguments.values[i + 1]);
	}
	options          = withPlugins(file, options, windowed);
	options.perFrame = samplesPerFrame(options, arguments);
	return windowed ? windowSized(file.shot, options, internal) : fileSized(file.shot, options);
}

double timeArgument(const CommandLine& arguments) {
	for (int i = 2; i + 1 < arguments.count; ++i) {
		if (std::string(arguments.values[i]) == "--time") {
			return std::stod(arguments.values[i + 1]);
		}
	}
	return 0;
}

SceneFile reloadedFile(SceneFile loaded, const Options& options) {
	for (int i = 2; i + 1 < options.arguments.count; ++i) {
		readSetting(loaded.shot, options.arguments.values, i);
	}
	loaded.shot.render.width  = options.width;
	loaded.shot.render.height = options.height;
	return loaded;
}

bool chosenIn(const Choices& choices, const std::string& name) {
	const std::vector<std::string> singles = { choices.geometry, choices.execution, choices.backend, choices.stopping };
	return std::count(singles.begin(), singles.end(), name) > 0 || std::count(choices.owned.begin(), choices.owned.end(), name) > 0;
}

bool switchedByUse(const std::string& name) {
	const std::size_t slash         = name.find('/');
	const std::optional<UseKey> key = keyNamedIn(useKeys(), name.substr(0, slash));
	const std::string member        = slash == std::string::npos ? "" : name.substr(slash + 1);
	return key && std::count(key->members.begin(), key->members.end(), member) > 0;
}

std::vector<std::string> chosenPlugins(const Options& options) {
	const Choices& choices          = options.choices;
	std::vector<std::string> chosen = options.sources;
	chosen.insert(chosen.end(), { choices.transport, choices.geometry, choices.execution, choices.backend, choices.stopping });
	chosen.insert(chosen.end(), choices.owned.begin(), choices.owned.end());
	chosen.insert(chosen.end(), options.reconstruction.begin(), options.reconstruction.end());
	chosen.insert(chosen.end(), options.sinks.begin(), options.sinks.end());
	chosen.insert(chosen.end(), options.observers.begin(), options.observers.end());
	return chosen;
}

} // namespace kodanuki
