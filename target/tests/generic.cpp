#include "check.h"
#include "core/block.h"
#include "core/options.h"
#include "core/plugin.h"
#include "core/render.h"
#include "plugins/geometry/sphere_trace/sphere_trace.h"
#include "plugins/geometry/tree/build.h"
#include "plugins/geometry/tree/detail.h"
#include "plugins/geometry/tree/walk.h"
#include "plugins/reconstruction/temporal/temporal.h"
#include "shared/image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr double genericErrors = 4.5;

const char* genericText = R"(
	render {
		width     = 32;
		height    = 20;
		tolerance = 16;
		samples   = 48;
	}

	function sphere(r) {
		return length(p) - r;
	}

	function box(size) {
		let q = abs(p) - size / 2;
		return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
	}

	camera {
		eye    = [0, -4, 1.5];
		target = [0, 0, 0.4];
		fov    = 0.7;
	}

	sky gradient {
		zenith  = [0.3, 0.4, 0.7];
		horizon = [0.7, 0.6, 0.5];
		ground  = [0.1, 0.1, 0.1];
	}

	light sphere {
		at     = [1.2, -1, 2];
		radius = 0.3;
		power  = [3, 3, 2.5];
	}

	light quad {
		at     = [-2, -1, 0.2];
		along  = [0, 1.5, 0];
		across = [0, 0, 1.5];
		power  = [2, 2, 3];
	}

	paint(matte([0.6, 0.6, 0.6])) at([0, 0, -0.1]) box([6, 6, 0.2]);
	paint(plastic([0.8, 0.2, 0.1], roughness = 0.3)) at([-0.7, 0, 0.5]) sphere(0.5);
	paint(metal([0.9, 0.9, 0.9], roughness = 0)) at([0.7, 0.4, 0.4]) sphere(0.4);
	paint(glass(refraction = 1.5, roughness = 0)) at([0.1, -0.8, 0.3]) sphere(0.3);
	paint(matte(#000000, glow = #FFD0A0, strength = 4)) at([0.3, 0.9, 1.3]) box([1.2, 0.4, 0.1]);
)";

/**
 * The image mean of a set's runs, and the variance of that mean.
 */
struct GenericSpread {
	std::array<double, 3> mean;
	std::array<double, 3> variance;
};

/**
 * One set's runs: the --use words beyond the reference set's, and the
 * other flags.
 */
struct GenericRun {
	std::vector<std::string> uses;
	std::vector<std::string> flags;
};

std::string genericPath(const std::string& name) {
	const char* directory = std::getenv("TMPDIR");
	return std::string(directory ? directory : "/tmp") + "/kodanuki_generic_" + name;
}

std::vector<std::string> referenceUses() {
	return { "geometry=sphere_trace", "execution=single", "stopping=fixed_count", "detail=none", "build=none", "strategy=none", "sampler=none", "observer=none" };
}

Image readPfm(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::string magic;
	int width    = 0;
	int height   = 0;
	double scale = 0;
	in >> magic >> width >> height >> scale;
	in.get();
	Image image = blankImage(std::max(width, 0), std::max(height, 0));
	in.read(reinterpret_cast<char*>(image.rgb.data()), std::streamsize(image.rgb.size() * sizeof(float)));
	return image;
}

std::vector<std::string> runWords(const GenericRun& run, int seed) {
	std::vector<std::string> words = { "kodanuki_tests", genericPath("scene.sdf"), "--seed", std::to_string(seed), "--threads", "4" };
	for (const std::string& use : referenceUses()) {
		words.insert(words.end(), { "--use", use });
	}
	for (const std::string& use : run.uses) {
		words.insert(words.end(), { "--use", use });
	}
	words.insert(words.end(), run.flags.begin(), run.flags.end());
	return words;
}

void drawnWith(const GenericRun& run, int seed, const std::vector<Plugin>& backends) {
	std::vector<std::string> words = runWords(run, seed);
	std::vector<char*> values;
	for (std::string& word : words) {
		values.push_back(word.data());
	}
	const CommandLine arguments = {
		.count  = int(values.size()),
		.values = values.data(),
	};
	SceneFile file        = sceneOrExit(words[1]);
	const Options options = readOptions(file, arguments, false);
	Renderer renderer     = rendererOf(std::move(file), options);
	renderer.backends     = backends.empty() ? renderer.backends : backends;
	rendered(renderer);
}

void drawnRun(const GenericRun& run, int seed) {
	drawnWith(run, seed, {});
}

Image stillOf(const GenericRun& run, int seed) {
	GenericRun still = run;
	still.flags.insert(still.flags.end(), { "--out", genericPath("image.ppm"), "--pfm", genericPath("image.pfm") });
	drawnRun(still, seed);
	return readPfm(genericPath("image.pfm"));
}

bool finiteImage(const Image& image) {
	return !image.rgb.empty() && std::all_of(image.rgb.begin(), image.rgb.end(), [](float value) {
		return std::isfinite(value);
	});
}

GenericSpread spreadOf(const Image& a, const Image& b) {
	GenericSpread spread = {
		.mean     = { 0, 0, 0 },
		.variance = { 0, 0, 0 },
	};

	const double pixels = double(a.width) * a.height;
	for (std::size_t k = 0; k < a.rgb.size() && a.rgb.size() == b.rgb.size(); ++k) {
		const double difference = double(a.rgb[k]) - b.rgb[k];
		spread.mean[k % 3] += (double(a.rgb[k]) + b.rgb[k]) / (2 * pixels);
		spread.variance[k % 3] += difference * difference / (4 * pixels * pixels);
	}
	return spread;
}

GenericSpread stillSpread(const GenericRun& run) {
	return spreadOf(stillOf(run, 1), stillOf(run, 2));
}

double farthestErrors(const GenericSpread& a, const GenericSpread& b) {
	double farthest = 0;
	for (int c = 0; c < 3; ++c) {
		const double error = std::sqrt(a.variance[c] + b.variance[c]) + 1e-9;
		farthest           = std::max(farthest, std::abs(a.mean[c] - b.mean[c]) / error);
	}
	return farthest;
}

bool sameMeans(const GenericSpread& a, const GenericSpread& b) {
	return a.mean == b.mean;
}

void checkAgrees(const char* what, const GenericSpread& on, const GenericSpread& base) {
	const double errors = farthestErrors(on, base);
	std::printf("generic %s: means %.4f %.4f %.4f against %.4f %.4f %.4f, %.2f standard errors apart\n", what, on.mean[0], on.mean[1], on.mean[2], base.mean[0], base.mean[1], base.mean[2], errors);
	check(errors <= genericErrors, what);
}

struct GenericSets {
	GenericSpread reference;
	GenericSpread tree;
};

void checkExecutionPoint(const GenericSets& sets) {
	checkAgrees("Execution: execution/packets keeps the mean of the reference set with geometry/tree", stillSpread(GenericRun { .uses = { "geometry=tree", "execution=packets" }, .flags = {} }), sets.tree);
}

std::atomic<int>& fakeSteps() {
	static std::atomic<int> steps = 0;
	return steps;
}

Crew fakeWorkers(const Renderer&) {
	return Crew {
		.workers  = 3,
		.capacity = poolCapacity,
	};
}

std::atomic<int>& fakeBlocks() {
	static std::atomic<int> blocks = 0;
	return blocks;
}

int soundBlocks(const Renderer& renderer) {
	int sound = 0;
	for (const Block& block : frameBlocks(renderer)) {
		const bool held = block.count > 0 && block.data != nullptr;
		sound += held && block.size > 0 && block.name.find('.') != std::string::npos ? 1 : 0;
	}
	return sound;
}

bool fakeRan(const Renderer& renderer, Worker&, const Plugin&, LoopStep) {
	fakeSteps() += 1;
	fakeBlocks() = std::max(fakeBlocks().load(), soundBlocks(renderer));
	return false;
}

Plugin fakeBackend() {
	Hooks hooks       = noHooks();
	hooks.openWorkers = fakeWorkers;
	hooks.ranStep     = fakeRan;
	return Plugin {
		.name   = "backend/fake",
		.point  = Point::Backend,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

Image backendStill(const std::vector<Plugin>& backends, int seed) {
	const GenericRun still = {
		.uses  = {},
		.flags = { "--out", genericPath("image.ppm"), "--pfm", genericPath("image.pfm") },
	};

	drawnWith(still, seed, backends);
	return readPfm(genericPath("image.pfm"));
}

void checkBackendPoint(const GenericSets& sets) {
	const std::vector<Plugin> fake  = { fakeBackend() };
	const std::vector<Plugin> mixed = { pluginNamed("backend/cpu"), fakeBackend() };
	checkAgrees("Backend: a backend that owns no kernels keeps the mean of backend/cpu through the fallback", spreadOf(backendStill(fake, 3), backendStill(fake, 4)), sets.reference);
	check(fakeSteps() > 0, "a backend that owns no kernels is asked for every step of its workers and hands them to the fallback");
	const int bare = fakeBlocks().exchange(0);

	const GenericRun tree = {
		.uses  = { "geometry=tree" },
		.flags = { "--out", genericPath("image.ppm"), "--pfm", genericPath("image.pfm") },
	};

	drawnWith(tree, 3, fake);
	std::printf("generic Backend: %d blocks of the frame hold data with the reference set, %d with geometry/tree\n", bare, fakeBlocks().load());
	check(bare == 3, "a backend finds the transport's finishes, lamps and sky as blocks of the frame, each with its data and an element size");
	check(fakeBlocks() >= bare + 5, "with geometry/tree a backend finds the tree's arrays as blocks too");
	checkAgrees("Backend: workers of backend/cpu and of a backend that owns no kernels draw one frame together, keeping the mean of backend/cpu", spreadOf(backendStill(mixed, 3), backendStill(mixed, 4)), sets.reference);
}

void checkChanged(const char* what, const GenericSpread& on, const GenericSpread& base) {
	checkAgrees(what, on, base);
	check(!sameMeans(on, base), "a Stopping, Sampler or Strategy plugin changes the frame it is compared on");
}

void checkStoppingPoint(const GenericSets& sets) {
	checkChanged("Stopping: stopping/confidence_interval keeps the mean of the reference set", stillSpread(GenericRun { .uses = { "stopping=confidence_interval" }, .flags = {} }), sets.reference);
}

void checkSamplerPoint(const GenericSets& sets) {
	for (const std::string member : { "sobol", "highlight_guide", "each_lamp_first", "sky_map" }) {
		const std::string what = "Sampler: sampler/" + member + " keeps the mean of the reference set";
		checkChanged(what.c_str(), stillSpread(GenericRun { .uses = { "sampler=" + member }, .flags = {} }), sets.reference);
	}
	checkChanged("Sampler: sampler/glowing_surfaces keeps the mean of the reference set with geometry/tree", stillSpread(GenericRun { .uses = { "geometry=tree", "sampler=glowing_surfaces" }, .flags = {} }), sets.tree);
}

void checkStrategyPoint(const GenericSets& sets) {
	for (const std::string member : { "join", "connect" }) {
		const std::string what = "Strategy: strategy/" + member + " keeps the mean of the reference set with geometry/tree and sampler/toward_mirrors";
		checkChanged(what.c_str(), stillSpread(GenericRun { .uses = { "geometry=tree", "sampler=toward_mirrors", "strategy=" + member }, .flags = {} }), sets.tree);
	}
}

void checkGeometryImage(const GenericSets& sets) {
	checkAgrees("Geometry: geometry/tree keeps the mean of the reference set", sets.tree, sets.reference);
}

Image lastClipFrame(const std::string& reconstruction, int frames, int seed) {
	std::ofstream(genericPath("scene.sdf")) << genericText << "\nclip {\n\tduration = " << frames << ";\n\trate     = 1;\n}\n";
	const GenericRun run = {
		.uses  = { "reconstruction=" + reconstruction },
		.flags = { "--samples", "1", "--out", genericPath("frames") },
	};
	drawnRun(run, seed);
	std::ofstream(genericPath("scene.sdf")) << genericText;
	char name[32];
	std::snprintf(name, sizeof(name), "/frame-%04d.pfm", frames - 1);
	return readPfm(genericPath("frames") + name);
}

void checkReconstructionPoint() {
	constexpr int frames        = 16;
	const GenericSpread clipped = spreadOf(lastClipFrame("mean", frames, 1), lastClipFrame("mean", frames, 2));
	const GenericSpread still   = stillSpread(GenericRun { .uses = {}, .flags = { "--samples", std::to_string(frames) } });
	checkAgrees("Reconstruction: reconstruction/mean over a clip of 16 frames of one sample, the scene unchanged, keeps the mean of a still frame of 16 samples", clipped, still);
}

const char* bouncingText = R"(
	render {
		width   = 32;
		height  = 20;
		samples = 4;
	}

	function sphere(r) {
		return length(p) - r;
	}

	function ground() {
		return p.z;
	}

	camera {
		eye    = [0, -4, 1.5];
		target = [0, 0, 0.4];
		fov    = 0.7;
	}

	light sphere {
		at     = [1.2, -1, 2];
		radius = 0.3;
		power  = [10, 10, 10] * (1 + time);
	}

	ground();
	at([0, 0, 0.4 + 0.3 * time]) sphere(0.3);
)";

float floorHistory(const GenericRun& run, const std::string& text) {
	std::ofstream(genericPath("scene.sdf")) << text;
	std::vector<std::string> words = runWords(run, 1);
	std::vector<char*> values;
	for (std::string& word : words) {
		values.push_back(word.data());
	}
	const CommandLine arguments = {
		.count  = int(values.size()),
		.values = values.data(),
	};
	SceneFile file        = sceneOrExit(words[1]);
	const Options options = readOptions(file, arguments, false);
	Renderer renderer     = rendererOf(std::move(file), options);
	rendered(renderer);
	std::ofstream(genericPath("scene.sdf")) << genericText;
	const TemporalState& state = temporalState(renderer.history.states);
	return state.pixels[std::size_t(state.height - 2) * std::size_t(state.width) + 1].length;
}

void checkClipHistory() {
	const GenericRun run = {
		.uses  = { "reconstruction=temporal" },
		.flags = { "--out", genericPath("frames") },
	};
	const std::string bouncing = bouncingText;
	check(floorHistory(run, bouncing + "clip {\n\tduration = 3;\n\trate     = 1;\n}\n") == 3, "temporal: a pixel of the floor far from a shape that moves keeps its history over every frame of the clip, while the shape and the light change");
	check(floorHistory(run, bouncing + "clip {\n\tduration = 3;\n\trate     = 1;\n\tcuts     = [1.5];\n}\n") == 1, "temporal: the frame that reaches a cut of the clip drops the whole history");
	const std::string bounce = "import \"" + std::filesystem::absolute("scenes/clip-bounce.sdf").string() + "\";\n\nclip {\n\tduration = 0.125;\n\trate     = 24;\n}\n";
	const GenericRun small   = {
		.uses  = { "reconstruction=temporal" },
		.flags = { "--size", "32", "20", "--samples", "4", "--out", genericPath("frames") },
	};
	const float history = floorHistory(small, bounce);
	std::printf("generic Reconstruction: on clip-bounce a pixel of the floor holds %.2f frames of history after 3 frames\n", double(history));
	check(history > 1, "temporal: on clip-bounce a pixel of the floor far from the ball keeps a history longer than one frame while the clip plays");
}

void checkBiasedRun(const std::string& name) {
	const std::string point = name.substr(0, name.find('/'));
	const std::string what  = "Biased: " + name + " runs and its frame stays finite";
	if (point == "reconstruction") {
		const std::string chain = name == "reconstruction/upscale" ? "temporal+atrous+upscale" : name.substr(15);
		check(finiteImage(lastClipFrame(chain, 3, 1)), what.c_str());
		return;
	}
	check(finiteImage(stillOf(GenericRun { .uses = {}, .flags = {} }, 1)), what.c_str());
}

void checkBiasedPlugins() {
	for (const Plugin& plugin : everyPlugin()) {
		if (plugin.effect == Effect::Biased) {
			checkBiasedRun(plugin.name);
		}
	}
}

/**
 * How many rays a Geometry comparison sends and how many of them
 * disagree.
 */
struct GenericHits {
	std::size_t rays;
	std::size_t hits;
	std::size_t apart;
};

std::vector<Ray> genericRays(const SceneFile& file) {
	const Lens lens = lensOf(file.shot.camera, file.shot.render);
	std::vector<Ray> rays;
	std::mt19937 random(5);
	std::uniform_real_distribution<double> unit(0, 1);
	for (int k = 0; k < 4000; ++k) {
		rays.push_back(pixelRay(lens, int(unit(random) * lens.width), int(unit(random) * lens.height)));
	}
	return rays;
}

void scoredRay(GenericHits& score, const Geometry& tree, const Geometry& traced, const Ray& ray) {
	const Hit a = intersect(tree, ray);
	const Hit b = intersect(traced, ray);
	score.rays += 1;
	score.hits += a.kind != HitKind::Missed ? 1 : 0;
	const bool both    = a.kind != HitKind::Missed && b.kind != HitKind::Missed;
	const bool neither = a.kind == HitKind::Missed && b.kind == HitKind::Missed;
	score.apart += neither || (both && std::abs(a.distance - b.distance) <= 2e-3) ? 0 : 1;
}

GenericHits hitScore(const SceneFile& file, const TestTree& tree) {
	const Geometry built  = treeGeometry(file.scene, tree.tree);
	const Geometry traced = sphereGeometry(file.scene, treeOf(built).region);
	GenericHits score     = { .rays = 0, .hits = 0, .apart = 0 };
	for (const Ray& ray : genericRays(file)) {
		scoredRay(score, built, traced, ray);
	}
	return score;
}

void checkHits(const std::string& what, const SceneFile& file, const TestTree& tree) {
	const GenericHits score = hitScore(file, tree);
	std::printf("generic Geometry %s: %zu of %zu rays hit, %zu apart from sphere tracing\n", what.c_str(), score.hits, score.rays, score.apart);
	check(score.hits > score.rays / 2 && score.apart * 500 <= score.rays, ("Geometry: geometry/tree with " + what + " meets what sphere tracing meets, within the tolerance").c_str());
}

TestTree detailedTree(const SceneFile& file, const std::vector<DetailMember>& members) {
	const Region region = sceneRegion(file.scene, file.shot, 4);
	const Detail detail = testDetail(file, region, members);

	const BuildInput input = {
		.scene    = file.scene,
		.detail   = detail,
		.passes   = {},
		.region   = region,
		.depthCap = depthCapOf(region, detail.least),
		.workers  = 4,
		.faces    = true,
	};

	return TestTree {
		.detail = detail,
		.tree   = buildTree(input),
	};
}

void checkGeometryPoint() {
	SceneFile file          = sceneOrExit(genericPath("scene.sdf"));
	file.shot.render.width  = 320;
	file.shot.render.height = 200;
	checkHits("every Detail and Build member", file, testTree(file, 4));
	for (const std::string name : { "by_distance", "focus_on_view", "pilot", "split_hidden_primitives", "split_crowded_leaves" }) {
		checkHits("the Detail member " + name + " alone", file, detailedTree(file, { DetailMember::ByDistance, *detailMemberNamed(name) }));
	}
	for (const PassMember pass : defaultPassMembers()) {
		checkHits("one Build member", file, passedTestTree(file, { pass }, 4));
	}
}

/**
 * One plugin a config names, and whether it is on.
 */
struct ConfigPlugin {
	std::string name;
	bool on;
};

std::vector<ConfigPlugin> configPlugins(const std::filesystem::path& path) {
	std::ifstream in(path);
	std::vector<ConfigPlugin> named;
	std::string line;
	while (std::getline(in, line)) {
		const std::size_t close = line.find('"', 1);
		if (!line.empty() && line[0] == '"' && close != std::string::npos) {
			named.push_back(ConfigPlugin { .name = line.substr(1, close - 1), .on = line.find("true", close) != std::string::npos });
		}
	}
	return named;
}

bool namedIn(const std::vector<ConfigPlugin>& named, const std::string& name) {
	return std::any_of(named.begin(), named.end(), [&](const ConfigPlugin& one) {
		return one.name == name;
	});
}

bool onWith(const std::vector<ConfigPlugin>& named, bool (*refused)(const Plugin& plugin)) {
	return std::any_of(named.begin(), named.end(), [&](const ConfigPlugin& one) {
		return one.on && pluginBuilt(one.name) && refused(pluginNamed(one.name));
	});
}

bool biased(const Plugin& plugin) {
	return plugin.effect == Effect::Biased;
}

bool preparing(const Plugin& plugin) {
	return plugin.hooks.prepareFrame != nullptr;
}

void checkConfig(const std::filesystem::path& path) {
	const std::vector<ConfigPlugin> named = configPlugins(path);
	const std::string name                = path.filename().string();
	for (const Plugin& plugin : everyPlugin()) {
		check(namedIn(named, plugin.name), ("every plugin is named in " + name + ", on or off").c_str());
	}
	if (name == "render_final.toml" || name == "reference.toml") {
		check(!onWith(named, biased), (name + " turns on no plugin whose effect is Biased").c_str());
	}
	if (name == "reference.toml") {
		check(!onWith(named, preparing), "reference.toml turns on no plugin that prepares the frame");
	}
}

void checkConfigs() {
	const std::filesystem::path configs = "target/configs";
	check(std::filesystem::is_regular_file(configs / "reference.toml"), "the tests run in the repository, which holds target/configs/reference.toml");
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(configs)) {
		if (entry.path().extension() == ".toml") {
			checkConfig(entry.path());
		}
	}
}

void checkGeneric() {
	checkConfigs();
	std::ofstream(genericPath("scene.sdf")) << genericText;
	checkGeometryPoint();
	const GenericSets sets = {
		.reference = stillSpread(GenericRun { .uses = {}, .flags = {} }),
		.tree      = stillSpread(GenericRun { .uses = { "geometry=tree" }, .flags = {} }),
	};

	checkGeometryImage(sets);
	checkExecutionPoint(sets);
	checkBackendPoint(sets);
	checkStoppingPoint(sets);
	checkSamplerPoint(sets);
	checkStrategyPoint(sets);
	checkReconstructionPoint();
	checkClipHistory();
	checkBiasedPlugins();
}

} // namespace kodanuki
