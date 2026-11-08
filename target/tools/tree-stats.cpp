#include "frame/frame.h"
#include "frame/run.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"
#include "tree/build.h"
#include "tree/detail.h"
#include "tree/pass.h"
#include "tree/tree.h"
#include "walk/walk.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace kodanuki;

constexpr double slopeStep = 1e-6;

using Clock = std::chrono::steady_clock;

struct Options {
	std::vector<DetailMember> members;
	std::vector<PassMember> passes;
	bool faces;
	bool framed;
	Region region;
	double least;
	int depth;
	int threads;
	std::string normals;
	int traces;
	bool speed;
};

struct Model {
	const SceneFile& file;
	const Detail& detail;
	const Geometry& geometry;
};

struct Truth {
	bool found;
	double t;
	bool deep;
};

struct Score {
	std::size_t hits;
	std::size_t floor;
	std::size_t off;
	std::size_t missed;
	std::size_t late;
	std::size_t deep;
	double worst;
};

double secondsSince(const Clock::time_point& start) {
	return std::chrono::duration<double>(Clock::now() - start).count();
}

double peakMegabytes() {
	std::ifstream status("/proc/self/status");
	std::string line;
	while (std::getline(status, line)) {
		if (line.rfind("VmHWM:", 0) == 0) {
			return std::stod(line.substr(6)) / 1024.0;
		}
	}
	return 0;
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

std::vector<DetailMember> membersOf(const std::string& value) {
	std::vector<DetailMember> members;
	for (const std::string& name : namesIn(value)) {
		const std::optional<DetailMember> member = detailMemberNamed(name);
		if (!member) {
			std::fprintf(stderr, "unknown detail member %s\n", name.c_str());
			std::exit(1);
		}
		members.push_back(*member);
	}
	return members;
}

std::vector<PassMember> passesOf(const std::string& value) {
	std::vector<PassMember> members;
	for (const std::string& name : namesIn(value)) {
		const std::optional<PassMember> member = passMemberNamed(name);
		if (!member) {
			std::fprintf(stderr, "unknown pass member %s\n", name.c_str());
			std::exit(1);
		}
		members.push_back(*member);
	}
	std::sort(members.begin(), members.end());
	return members;
}

Region noRegion() {
	return Region {
		.lower = { 0, 0, 0 },
		.size  = { 0, 0, 0 },
	};
}

Options optionsOf(int argc, char** argv, Shot& shot) {
	Options options = {
		.members = defaultDetailMembers(),
		.passes  = defaultPassMembers(),
		.faces   = true,
		.framed  = false,
		.region  = noRegion(),
		.least   = 0,
		.depth   = -1,
		.threads = 8,
		.normals = {},
		.traces  = 0,
		.speed   = false,
	};

	for (int i = 2; i < argc; ++i) {
		const std::string flag = argv[i];
		if (flag == "--size") {
			shot.render.width  = std::atoi(argv[++i]);
			shot.render.height = std::atoi(argv[++i]);
		}
		if (flag == "--tolerance") {
			shot.render.tolerance = std::stof(argv[++i]);
		}
		if (flag == "--use") {
			const std::string use = argv[++i];
			options.faces         = use == "faces=off" ? false : options.faces;
			options.members       = use.rfind("detail=", 0) == 0 ? membersOf(use.substr(7)) : options.members;
			options.passes        = use.rfind("passes=", 0) == 0 ? passesOf(use.substr(7)) : options.passes;
		}
		if (flag == "--region") {
			for (int axis = 0; axis < 3; ++axis) {
				options.region.lower[axis] = std::stod(argv[i + 1 + axis]);
				options.region.size[axis]  = std::stod(argv[i + 4 + axis]) - options.region.lower[axis];
			}
			options.framed = true;
			i += 6;
		}
		if (flag == "--least") {
			options.least = std::stod(argv[++i]);
		}
		if (flag == "--depth") {
			options.depth = std::atoi(argv[++i]);
		}
		if (flag == "--threads") {
			options.threads = std::atoi(argv[++i]);
		}
		if (flag == "--normals") {
			options.normals = argv[++i];
		}
		if (flag == "--trace") {
			options.traces = std::atoi(argv[++i]);
		}
		if (flag == "--speed") {
			options.speed = true;
		}
	}
	return options;
}

double megabytesOf(std::size_t count, std::size_t size) {
	return double(count * size) / 1048576.0;
}

std::uint64_t mixedBytes(std::uint64_t hash, const void* data, std::size_t size) {
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < size; ++i) {
		hash = (hash ^ bytes[i]) * 0x100000001B3ull;
	}
	return hash;
}

std::uint64_t treeFingerprint(const Tree& tree) {
	std::uint64_t hash = 0xCBF29CE484222325ull;
	for (const PackedNode& node : tree.nodes) {
		const std::uint64_t words[4] = { node.lowerKinds, node.upperKinds, std::uint64_t(node.firstBranch) | std::uint64_t(node.firstSurface) << 32, std::uint64_t(node.firstSolid) | std::uint64_t(node.coarse) << 32 | std::uint64_t(node.busy) << 40 };
		hash                         = mixedBytes(hash, words, sizeof(words));
	}
	for (const SurfaceLeaf& leaf : tree.surfaces) {
		const std::uint64_t words[2] = { std::uint64_t(leaf.run) | std::uint64_t(leaf.programs) << 32, std::uint64_t(leaf.loosened) | std::uint64_t(leaf.kind) << 16 };
		hash                         = mixedBytes(hash, words, sizeof(words));
	}
	for (const SolidLeaf& leaf : tree.solids) {
		hash = mixedBytes(hash, &leaf.programs, sizeof(leaf.programs));
	}
	return mixedBytes(hash, tree.runs.data(), tree.runs.size() * sizeof(std::uint32_t));
}

std::size_t keptCount(const Tree& tree, const ProgramCut& cut) {
	std::size_t kept = 0;
	for (std::uint32_t r = 0; r < cut.ranges; ++r) {
		kept += tree.cutSteps[cut.first + 2 * r + 1] - tree.cutSteps[cut.first + 2 * r];
	}
	return kept;
}

double meanLeafSteps(const Scene& scene, const Tree& tree) {
	double steps = 0;
	for (const SurfaceLeaf& leaf : tree.surfaces) {
		const ProgramList& list = tree.lists[leaf.programs];
		for (std::uint32_t k = 0; k < list.count; ++k) {
			const ProgramReference& reference = tree.references[list.first + k];
			steps += double(reference.source == ProgramSource::Cut ? keptCount(tree, tree.cuts[reference.index]) : scene.objectPrograms[reference.index].steps.size());
		}
	}
	return tree.surfaces.empty() ? 0 : steps / double(tree.surfaces.size());
}

void printPrograms(const Scene& scene, const Tree& tree) {
	std::size_t steps    = 0;
	std::size_t ranges   = 0;
	std::size_t rerouted = 0;
	for (const ProgramCut& cut : tree.cuts) {
		steps += keptCount(tree, cut);
		ranges += cut.ranges;
		rerouted += cut.rerouted;
	}
	const double megabytes = megabytesOf(tree.cuts.size(), sizeof(ProgramCut)) + megabytesOf(tree.cutSteps.size(), sizeof(std::uint16_t));
	std::printf("cuts %zu (%zu kept steps in %zu runs, %zu rerouted, %.3f MB); a surface leaf's programs hold %.1f steps on average\n", tree.cuts.size(), steps, ranges, rerouted, megabytes, meanLeafSteps(scene, tree));
}

void printStats(const Tree& tree, double seconds) {
	const TreeStats s = treeStats(tree);
	std::printf("build %.3f s; octree cells %zu: branch %zu, empty %zu, filled %zu, surface %zu (one patch %zu, floor %zu)\n", seconds, s.cells, s.branches, s.empty, s.solids, s.surfaces, s.patched, s.floors);
	std::printf("packed nodes %zu; faces: plane %zu, quadric %zu, patch %zu, ring %zu; surface leaves by faces 1..8: %zu %zu %zu %zu %zu %zu %zu %zu\n", s.nodes, s.faces[0], s.faces[1], s.faces[2], s.faces[3], s.byCount[1], s.byCount[2], s.byCount[3], s.byCount[4], s.byCount[5], s.byCount[6], s.byCount[7], s.byCount[8]);
	std::printf("tree %.3f MB: nodes %.3f, surface leaves %.3f, solid leaves %.3f, runs %.3f, lists %zu (%zu references)\n", s.megabytes, megabytesOf(tree.nodes.size(), sizeof(PackedNode)), megabytesOf(tree.surfaces.size(), sizeof(SurfaceLeaf)), megabytesOf(tree.solids.size(), sizeof(SolidLeaf)), megabytesOf(tree.runs.size(), 4), tree.lists.size(), tree.references.size());
	std::printf("fingerprint %016llx\n", static_cast<unsigned long long>(treeFingerprint(tree)));
}

double modelField(const Model& model, const math::Vector& p) {
	return cellField(model.file.scene.objectPrograms, p);
}

math::Vector rayPoint(const Ray& ray, double t) {
	return ray.origin + ray.direction * t;
}

bool goesDeep(const Model& model, const Ray& ray, double t) {
	const double tolerance = pointTolerance(model.detail, rayPoint(ray, t)) + model.detail.least;
	double lowest          = 0;
	for (int k = 1; k <= 8; ++k) {
		lowest = std::min(lowest, modelField(model, rayPoint(ray, t + k * tolerance)));
	}
	return lowest < -1.5 * tolerance;
}

Truth traced(const Model& model, const Ray& ray) {
	const double reach = 4 * (model.geometry.tree.region.size[0] + model.geometry.tree.region.size[1] + model.geometry.tree.region.size[2]);
	double t           = 0;
	for (int i = 0; i < 20000 && t < reach; ++i) {
		const double d = modelField(model, rayPoint(ray, t));
		if (d < 1e-7) {
			return Truth {
				.found = true,
				.t     = t,
				.deep  = goesDeep(model, ray, t),
			};
		}
		t += d;
	}
	return Truth {
		.found = false,
		.t     = 0,
		.deep  = false,
	};
}

double slopeAlong(const Model& model, const math::Vector& at, const math::Vector& step) {
	return (modelField(model, at + step) - modelField(model, at - step)) / (2 * slopeStep);
}

double facingAt(const Model& model, const Ray& ray, double t) {
	const math::Vector at       = rayPoint(ray, t);
	const math::Vector stepX    = { slopeStep, 0, 0 };
	const math::Vector stepY    = { 0, slopeStep, 0 };
	const math::Vector stepZ    = { 0, 0, slopeStep };
	const math::Vector gradient = { slopeAlong(model, at, stepX), slopeAlong(model, at, stepY), slopeAlong(model, at, stepZ) };
	const double along          = math::dot(gradient, ray.direction);
	return std::max(std::abs(along) / std::max(math::length(gradient), 1e-12), 0.05);
}

void scoreRay(Score& score, const Model& model, const Ray& ray) {
	const Truth truth = traced(model, ray);
	const Hit hit     = intersect(model.geometry, ray);
	if (hit.kind != HitKind::Missed && hit.leafKind == LeafKind::Floor) {
		score.floor += 1;
		return;
	}
	if (hit.kind != HitKind::Missed) {
		const math::Vector p   = rayPoint(ray, hit.distance);
		const double tolerance = pointTolerance(model.detail, p);
		const double off       = std::abs(modelField(model, p));
		score.hits += 1;
		score.off += off <= tolerance * 1.01 + 1e-6 ? 0 : 1;
		score.worst = std::max(score.worst, off / tolerance);
	}
	if (!truth.found || !truth.deep) {
		return;
	}
	const double allowance = (pointTolerance(model.detail, rayPoint(ray, truth.t)) + model.detail.least) / facingAt(model, ray, truth.t);
	score.deep += 1;
	score.missed += hit.kind != HitKind::Missed ? 0 : 1;
	score.late += hit.kind != HitKind::Missed && hit.distance > truth.t + allowance ? 1 : 0;
}

Score noScore() {
	return Score {
		.hits   = 0,
		.floor  = 0,
		.off    = 0,
		.missed = 0,
		.late   = 0,
		.deep   = 0,
		.worst  = 0,
	};
}

Score summed(const std::vector<Score>& scores) {
	Score total = noScore();
	for (const Score& s : scores) {
		total.hits += s.hits;
		total.floor += s.floor;
		total.off += s.off;
		total.missed += s.missed;
		total.late += s.late;
		total.deep += s.deep;
		total.worst = std::max(total.worst, s.worst);
	}
	return total;
}

void traceCheck(const Model& model, int count, int threads) {
	const Lens lens = lensOf(model.file.shot.camera, model.file.shot.render);
	std::vector<Ray> rays;
	std::mt19937 random(21);
	std::uniform_int_distribution<int> column(0, lens.width - 1);
	std::uniform_int_distribution<int> row(0, lens.height - 1);
	for (int s = 0; s < count; ++s) {
		rays.push_back(pixelRay(lens, column(random), row(random)));
	}
	std::vector<Score> scores(rays.size(), noScore());
	std::atomic<std::size_t> next = 0;
	std::vector<std::thread> pool;
	for (int k = 0; k < threads; ++k) {
		pool.emplace_back([&]() {
			for (std::size_t i = next++; i < rays.size(); i = next++) {
				scoreRay(scores[i], model, rays[i]);
			}
		});
	}
	for (std::thread& thread : pool) {
		thread.join();
	}
	const Score total = summed(scores);
	std::printf("trace %d camera rays: %zu held hits (%zu at floor leaves skipped), %zu deep true hits; off the tolerance %zu (worst %.3f tolerances), missed %zu, late %zu\n", count, total.hits, total.floor, total.deep, total.off, total.worst, total.missed, total.late);
}

double raysPerSecond(const Geometry& geometry, const Lens& lens, int threads) {
	const Clock::time_point start = Clock::now();
	normalsImage(geometry, lens, threads);
	return double(lens.width) * double(lens.height) / secondsSince(start);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: kodanuki_tree_stats scene.sdf [--size w h] [--tolerance t] [--use faces=off] [--use detail=none|a+b...] [--use passes=none|a+b...] [--region lx ly lz hx hy hz] [--least e] [--depth n] [--threads n] [--normals out.ppm] [--trace rays] [--speed]\n");
		return 1;
	}
	SceneFile file        = sceneOrExit(argv[1]);
	const Scene& scene    = file.scene;
	const Options options = optionsOf(argc, argv, file.shot);
	std::size_t steps     = 0;
	for (const Program& program : scene.objectPrograms) {
		steps += program.steps.size();
	}
	std::printf("%zu objects, %zu leaves, %zu steps in the object programs (%.1f MB)\n", scene.objectPrograms.size(), scene.leaves.size(), steps, double(steps * sizeof(Step)) / 1048576.0);
	const Clock::time_point at = Clock::now();
	const Region region        = options.framed ? options.region : sceneRegion(scene, file.shot, options.threads);
	const double searched      = secondsSince(at);

	const SceneDetailQuery query = {
		.scene   = scene,
		.shot    = file.shot,
		.region  = region,
		.members = options.members,
	};

	Detail detail   = sceneDetail(query);
	detail.least    = options.least > 0 ? options.least : detail.least;
	const int depth = options.depth >= 0 ? options.depth : depthCapOf(region, detail.least);
	std::printf("%s at %dx%d: region lower %.4f %.4f %.4f size %.4f %.4f %.4f (%s, %.3f s), least %g, depth cap %d, share %.4f, faces %s, %zu detail members, %d threads\n", argv[1], file.shot.render.width, file.shot.render.height, region.lower[0], region.lower[1], region.lower[2], region.size[0], region.size[1], region.size[2], options.framed ? "given" : "searched", searched, detail.least, depth, detail.share, options.faces ? "on" : "off", detail.members.size(), options.threads);

	const BuildInput input = {
		.scene    = scene,
		.detail   = detail,
		.passes   = options.passes,
		.region   = region,
		.depthCap = depth,
		.workers  = options.threads,
		.faces    = options.faces,
	};

	const Clock::time_point start = Clock::now();
	const Tree tree               = buildTree(input);
	printStats(tree, secondsSince(start));
	printPrograms(scene, tree);
	std::printf("peak resident %.1f MB\n", peakMegabytes());

	const Geometry geometry = {
		.scene = scene,
		.tree  = tree,
	};

	const Model model = {
		.file     = file,
		.detail   = detail,
		.geometry = geometry,
	};

	const Lens lens = lensOf(file.shot.camera, file.shot.render);
	if (options.speed) {
		std::printf("rays per second at %dx%d: 1 thread %.0f, %d threads %.0f\n", lens.width, lens.height, raysPerSecond(geometry, lens, 1), options.threads, raysPerSecond(geometry, lens, options.threads));
	}
	if (options.traces > 0) {
		traceCheck(model, options.traces, options.threads);
	}
	if (!options.normals.empty()) {
		writeUngradedPpm(normalsImage(geometry, lens, options.threads), options.normals);
	}
	return 0;
}
