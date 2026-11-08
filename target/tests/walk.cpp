#include "walk/walk.h"
#include "check.h"
#include "extras/build_correct_face_slabs/face_slabs.h"
#include "frame/frame.h"
#include "frame/run.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"
#include "tree/build.h"
#include "tree/tree.h"
#include "walk/leaf.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <thread>
#include <vector>

namespace kodanuki {

using Place = std::array<double, 3>;

struct Truth {
	bool found;
	double t;
	bool deep;
};

struct Model {
	const Scene& scene;
	const std::vector<Program>& programs;
	const Geometry& geometry;
	const Detail& detail;
};

struct Sample {
	Ray ray;
	Truth truth;
};

struct Score {
	std::size_t hits;
	std::size_t floor;
	std::size_t off;
	std::size_t missed;
	std::size_t late;
	std::size_t deep;
	std::size_t strays;
	double farthest;
};

double modelDistance(const Model& model, const Place& p) {
	std::vector<double> registers;

	const Probe probe = {
		.p         = { p[0], p[1], p[2] },
		.registers = registers,
	};

	double nearest = 1e30;
	for (const Program& program : model.programs) {
		nearest = std::min(nearest, programDistance(program, probe));
	}
	return nearest;
}

Place rayPoint(const Ray& ray, double t) {
	return { ray.origin.x + ray.direction.x * t, ray.origin.y + ray.direction.y * t, ray.origin.z + ray.direction.z * t };
}

double toleranceNear(const Model& model, const Place& p) {
	const math::Vector point = { p[0], p[1], p[2] };
	return pointTolerance(model.detail, point) + 1e-4;
}

bool goesDeep(const Model& model, const Ray& ray, double t) {
	const double tolerance = toleranceNear(model, rayPoint(ray, t));
	double lowest          = 0;
	for (int k = 1; k <= 8; ++k) {
		lowest = std::min(lowest, modelDistance(model, rayPoint(ray, t + k * tolerance)));
	}
	return lowest < -1.5 * tolerance;
}

Truth traced(const Model& model, const Ray& ray) {
	double t = 0;
	for (int i = 0; i < 4000 && t < 60; ++i) {
		const double d = modelDistance(model, rayPoint(ray, t));
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

double facing(const Model& model, const Ray& ray, double t) {
	const Place p  = rayPoint(ray, t);
	const double h = 1e-6;
	double gradient[3];
	for (int i = 0; i < 3; ++i) {
		Place forward  = p;
		Place backward = p;
		forward[i] += h;
		backward[i] -= h;
		gradient[i] = (modelDistance(model, forward) - modelDistance(model, backward)) / (2 * h);
	}
	const double length = std::sqrt(gradient[0] * gradient[0] + gradient[1] * gradient[1] + gradient[2] * gradient[2]);
	const double along  = gradient[0] * ray.direction.x + gradient[1] * ray.direction.y + gradient[2] * ray.direction.z;
	return std::max(std::abs(along) / std::max(length, 1e-12), 0.05);
}

double patchSlack(const Model& model, const Hit& hit) {
	if (hit.cell == noLeaf) {
		return 1;
	}
	return runHeader(model.geometry.tree.runs[model.geometry.tree.surfaces[hit.cell].run]).patches != 0 ? 1.25 : 1;
}

void scoreLeaf(Score& score, const Model& model, const Hit& hit, const Place& p) {
	if (hit.leaf == noLeaf || hit.leaf >= model.scene.leaves.size()) {
		score.strays += 1;
		return;
	}
	const Leaf& leaf = model.scene.leaves[hit.leaf];
	std::vector<double> registers;

	const Probe probe = {
		.p         = { p[0], p[1], p[2] },
		.registers = registers,
	};

	const double distance = std::abs(leafDistance(model.scene, leaf, probe));
	score.farthest        = std::max(score.farthest, distance);
}

void scoreRay(Score& score, const Model& model, const Sample& sample) {
	const Ray& ray     = sample.ray;
	const Truth& truth = sample.truth;
	const Hit hit      = intersect(model.geometry, ray);
	if (hit.kind != HitKind::Missed && hit.leafKind == LeafKind::Floor) {
		score.floor += 1;
		return;
	}
	if (hit.kind != HitKind::Missed) {
		const Place p = rayPoint(ray, hit.distance);
		score.hits += 1;
		score.off += std::abs(modelDistance(model, p)) <= patchSlack(model, hit) * toleranceNear(model, p) ? 0 : 1;
		scoreLeaf(score, model, hit, p);
	}
	if (!truth.found || !truth.deep) {
		return;
	}
	score.deep += 1;
	const double allowance = patchSlack(model, hit) * toleranceNear(model, rayPoint(ray, truth.t)) / facing(model, ray, truth.t);
	score.missed += hit.kind != HitKind::Missed ? 0 : 1;
	score.late += hit.kind != HitKind::Missed && hit.distance > truth.t + allowance ? 1 : 0;
}

Place nearLeaf(std::mt19937& random, const Scene& scene, double spread) {
	std::uniform_int_distribution<std::size_t> pick(0, scene.leaves.size() - 1);
	std::uniform_real_distribution<double> offset(-spread, spread);
	const double* centre = scene.leaves[pick(random)].placement.origin;
	return { centre[0] + offset(random), centre[1] + offset(random), centre[2] + offset(random) };
}

Ray randomRay(std::mt19937& random, const Scene& scene) {
	const Place from    = nearLeaf(random, scene, 1.5);
	const Place to      = nearLeaf(random, scene, 0.3);
	const Place d       = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
	const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	return Ray {
		.origin    = { float(from[0]), float(from[1]), float(from[2]) },
		.direction = { float(d[0] / length), float(d[1] / length), float(d[2] / length) },
		.far       = 1000,
		.inside    = false,
		.within    = 0,
	};
}

Score emptyScore() {
	return Score {
		.hits     = 0,
		.floor    = 0,
		.off      = 0,
		.missed   = 0,
		.late     = 0,
		.deep     = 0,
		.strays   = 0,
		.farthest = 0,
	};
}

Sample sampleOf(const Model& model, const Ray& ray) {
	return Sample {
		.ray   = ray,
		.truth = traced(model, ray),
	};
}

void inParallel(std::size_t count, const std::function<void(std::size_t)>& work) {
	std::atomic<std::size_t> next = 0;
	const auto run                = [&]() {
		for (std::size_t i = next++; i < count; i = next++) {
			work(i);
		}
	};

	std::vector<std::thread> threads;
	for (int k = 0; k < 8; ++k) {
		threads.emplace_back(run);
	}
	for (std::thread& thread : threads) {
		thread.join();
	}
}

std::vector<Ray> sampleRays(const Model& model, const SceneFile& file) {
	std::vector<Ray> rays;
	const Lens lens = lensOf(file.shot.camera, file.shot.render);
	std::mt19937 random(21);
	std::uniform_int_distribution<int> column(0, lens.width - 1);
	std::uniform_int_distribution<int> row(0, lens.height - 1);
	for (int s = 0; s < 1500; ++s) {
		rays.push_back(pixelRay(lens, column(random), row(random)));
	}
	for (int s = 0; s < 3000; ++s) {
		const Ray ray = randomRay(random, file.scene);
		if (modelDistance(model, rayPoint(ray, 0)) > toleranceNear(model, rayPoint(ray, 0))) {
			rays.push_back(ray);
		}
	}
	return rays;
}

std::vector<Sample> samplesOf(const Model& model, const SceneFile& file) {
	const std::vector<Ray> rays = sampleRays(model, file);
	std::vector<Sample> samples(rays.size(), sampleOf(model, rays[0]));
	inParallel(rays.size(), [&](std::size_t i) {
		samples[i] = sampleOf(model, rays[i]);
	});

	return samples;
}

Score summed(const Score& a, const Score& b) {
	return Score {
		.hits     = a.hits + b.hits,
		.floor    = a.floor + b.floor,
		.off      = a.off + b.off,
		.missed   = a.missed + b.missed,
		.late     = a.late + b.late,
		.deep     = a.deep + b.deep,
		.strays   = a.strays + b.strays,
		.farthest = std::max(a.farthest, b.farthest),
	};
}

Score scoreSamples(const Model& model, const std::vector<Sample>& samples) {
	std::vector<Score> scores(samples.size(), emptyScore());
	inParallel(samples.size(), [&](std::size_t i) {
		scoreRay(scores[i], model, samples[i]);
	});

	Score score = emptyScore();
	for (const Score& one : scores) {
		score = summed(score, one);
	}
	return score;
}

void shiftFace(Tree& tree, std::uint32_t at, FaceKind kind) {
	const std::uint32_t constant = at + (kind == FaceKind::Plane ? 3 : 9);
	tree.runs[constant]          = std::bit_cast<std::uint32_t>(std::bit_cast<float>(tree.runs[constant]) + 0.05f);
}

void reslabbed(Tree& tree, std::uint32_t at, FaceKind kind) {
	const float* face                        = reinterpret_cast<const float*>(tree.runs.data() + at);
	const std::array<std::uint32_t, 3> words = slabWords(kind == FaceKind::Quadric ? quadricSlab(face) : ringSlab(face));
	std::copy(words.begin(), words.end(), tree.runs.begin() + at + (kind == FaceKind::Quadric ? 10 : 14));
}

Tree shrunkTree(const Tree& tree) {
	Tree shrunk = tree;
	for (const SurfaceLeaf& leaf : tree.surfaces) {
		const RunHeader header                 = runHeader(tree.runs[leaf.run]);
		const std::vector<std::uint32_t> faces = faceOffsets(tree.runs.data() + leaf.run);
		for (std::uint32_t j = 0; j < header.count; ++j) {
			const FaceKind kind = faceKind(header, j);
			if (kind != FaceKind::Patch) {
				shiftFace(shrunk, leaf.run + faces[j], kind);
			}
			if (header.slabbed && (kind == FaceKind::Quadric || kind == FaceKind::Ring)) {
				reslabbed(shrunk, leaf.run + faces[j], kind);
			}
		}
	}
	return shrunk;
}

void checkScene(const char* path) {
	SceneFile file          = sceneOrExit(path);
	const Scene& scene      = file.scene;
	file.shot.render.width  = 320;
	file.shot.render.height = 200;
	const TestTree tree     = testTree(file, 8);

	const Geometry geometry = {
		.scene = scene,
		.tree  = tree.tree,
	};

	const Model model = {
		.scene    = scene,
		.programs = scene.objectPrograms,
		.geometry = geometry,
		.detail   = tree.detail,
	};

	const std::vector<Sample> samples = samplesOf(model, file);
	const Score score                 = scoreSamples(model, samples);
	std::printf("walk %s: %zu hits (%zu at floor), %zu deep true hits; off %zu, missed %zu, late %zu; leaves %zu stray, farthest %g\n", path, score.hits, score.floor, score.deep, score.off, score.missed, score.late, score.strays, score.farthest);
	check(score.hits > 1000 && score.deep > 800, "many rays hit");
	check(score.off == 0, "tree hits lie within the tolerance of the surface");
	check(score.missed == 0 && score.late == 0, "every clear true hit is found, not late");
	check(score.strays == 0 && score.farthest < 0.02, "hits carry the leaf they lie on (within a blend radius)");

	const Tree shrunk = shrunkTree(tree.tree);

	const Geometry moved = {
		.scene = scene,
		.tree  = shrunk,
	};

	const Model mutated = {
		.scene    = scene,
		.programs = scene.objectPrograms,
		.geometry = moved,
		.detail   = tree.detail,
	};

	const Score broken = scoreSamples(mutated, samples);
	check(broken.off + broken.missed + broken.late > 100, "faces moved by 0.05 are caught");
}

void checkExit() {
	const SceneFile file = sceneOrExit(writeScene("ball", R"(
		function sphere(r) {
			return length(p) - r;
		}

		sphere(1);
	)"));

	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	std::mt19937 random(4);
	std::uniform_real_distribution<float> unit(-0.5f, 0.5f);
	std::normal_distribution<float> normal(0, 1);
	double worst = 0;
	for (int s = 0; s < 500; ++s) {
		const float o[3]   = { unit(random), unit(random), unit(random) };
		const float d[3]   = { normal(random), normal(random), normal(random) };
		const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

		const Ray ray = {
			.origin    = { o[0], o[1], o[2] },
			.direction = { d[0] / length, d[1] / length, d[2] / length },
			.far       = 1000,
			.inside    = true,
			.within    = 0,
		};

		const double b    = o[0] * ray.direction.x + o[1] * ray.direction.y + o[2] * ray.direction.z;
		const double c    = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - 1;
		const double want = -b + std::sqrt(b * b - c);
		const Hit hit     = intersect(tree, ray);
		const double dot  = math::dot(hit.normal, ray.direction);
		worst             = std::max(worst, hit.kind != HitKind::Missed && dot > 0 ? std::abs(hit.distance - want) : 1.0);
	}

	const Near near = {
		.got       = worst,
		.want      = 0,
		.tolerance = 0.01,
		.what      = "rays leaving a sphere exit where it ends, with the normal outward",
	};

	checkNear(near);
}

void checkLawn() {
	const SceneFile file = sceneOrExit(writeScene("lawn", R"(
		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		shape garden() {
			paint(matte([0.9, 0.9, 0.9])) cut {
				at([0, 0, 0.3]) box([12, 11, 0.6]);
				at([0, 0, 0.15]) box([11.4, 10.4, 1]);
			}

			paint(matte([0.1, 0.5, 0.1])) at([0, 0, 0.15]) box([11.4, 10.4, 0.3]);
		}

		paint(matte([0.8, 0.2, 0.2])) at([0, 0, -5]) box([12, 11, 10]);
		garden();
	)"));

	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	int wrong = 0;
	for (int k = 0; k < 41; ++k) {
		const Ray ray = {
			.origin    = { -5 + 0.25f * float(k), 0.3f, 20 },
			.direction = { 0, 0, -1 },
			.far       = 1000,
			.inside    = false,
			.within    = 0,
		};

		const Hit hit = intersect(tree, ray);
		wrong += hit.kind != HitKind::Missed && std::abs(hit.distance - 19.7f) < 0.01f && hit.leaf == file.scene.leaves.size() - 1 ? 0 : 1;
	}
	std::printf("walk lawn: %d of 41 rays miss the lawn's own leaf\n", wrong);
	check(wrong == 0, "a surface on the plane of another shape's crease keeps its own leaf");
}

void checkFar() {
	const SceneFile file = sceneOrExit(writeScene("far", R"(
		render {
			width  = 64;
			height = 40;
			far    = 10;
		}

		camera {
			eye    = [0, -14, 0];
			target = [0, 0, 0];
			fov    = 0.5;
		}

		function sphere(r) {
			return length(p) - r;
		}

		sphere(1);
	)"));

	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	const Hit hit = intersect(tree, pixelRay(lensOf(file.shot.camera, file.shot.render), 32, 20));

	const Near near = {
		.got       = hit.kind != HitKind::Missed ? hit.distance : 0,
		.want      = 13,
		.tolerance = 0.02,
		.what      = "a camera ray meets a shape farther from the eye than render.far",
	};

	checkNear(near);
}

struct Strip {
	float x;
	float z;
	std::uint32_t leaf;
};

int wrongLeaves(const Geometry& tree, const std::vector<Strip>& strips) {
	int wrong = 0;
	for (const Strip& strip : strips) {
		for (int k = 0; k < 20; ++k) {
			const Ray ray = {
				.origin    = { strip.x, -9.5f + float(k), 20 },
				.direction = { 0, 0, -1 },
				.far       = 1000,
				.inside    = false,
				.within    = 0,
			};

			const Hit hit = intersect(tree, ray);
			wrong += hit.kind != HitKind::Missed && std::abs(hit.distance - (20 - strip.z)) < 0.05f && hit.leaf == strip.leaf ? 0 : 1;
		}
	}
	return wrong;
}

void checkStripe() {
	const SceneFile file = sceneOrExit(writeScene("stripe", R"(
		function box(size, r) {
			let q = abs(p) - size / 2 + r;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0) - r;
		}

		camera {
			eye    = [0, -30, 12];
			target = [0, 0, 0];
			fov    = 0.9;
		}

		paint(matte([0.1, 0.1, 0.1])) cut {
			at([0, 0, -0.5]) box([30, 30, 1], 0);
			at([-0.4, 0, -0.1]) box([0.3, 31, 0.2], 0);
		}

		paint(matte([0.9, 0.9, 0.9])) at([-0.4, 0, -0.1]) box([0.3, 30, 0.2], 0);
		paint(matte([0.9, 0.9, 0.2])) at([0.4, 0, -0.02]) box([0.3, 30, 0.2], 0);
	)"));

	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	const std::vector<Strip> strips = { { -0.4f, 0, 2 }, { 0.4f, 0.08f, 3 }, { -1.2f, 0, 0 }, { 0, 0, 0 }, { 1.2f, 0, 0 } };
	const int wrong                 = wrongLeaves(tree, strips);
	std::printf("walk stripe: %d of 100 rays miss their paint\n", wrong);
	check(wrong == 0, "a paint stripe set into the road plane or just above it keeps its own leaf, the road beside it the road's");
}

void checkGable() {
	const SceneFile file = sceneOrExit(writeScene("gable", R"(
		function box(size, r) {
			let q = abs(p) - size / 2 + r;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0) - r;
		}

		paint(matte([0.9, 0.85, 0.7])) at([0, 0, 1.5]) box([4, 6, 3], 0);
		paint(matte([0.6, 0.25, 0.1])) at([0, 0, 3]) turn(x, pi * 0.25) box([4, 4.5, 4.5], 0);
	)"));

	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	const float top = 3 + 4.5f / std::sqrt(2.0f);
	int wrong       = 0;
	for (int k = 0; k < 60; ++k) {
		const float z = 0.1f + (top - 0.2f) * float(k) / 59;
		const float y = 0.37f * (float(k % 7) - 3);

		const Ray ray = {
			.origin    = { 10, y, z },
			.direction = { -1, 0, 0 },
			.far       = 1000,
			.inside    = false,
			.within    = 0,
		};

		const Hit hit           = intersect(tree, ray);
		const bool roof         = z > 3 && std::abs(y) < top - z;
		const bool wall         = z < 3 && std::abs(y) < 3;
		const std::uint32_t own = roof ? 1 : 0;
		const bool border       = std::abs(z - 3) < 0.02f || std::abs(std::abs(y) - (top - z)) < 0.02f;
		wrong += !border && (roof || wall) && !(hit.kind != HitKind::Missed && std::abs(hit.distance - 8) < 0.01f && hit.leaf == own) ? 1 : 0;
	}
	std::printf("walk gable: %d of 60 rays on the end wall miss their paint\n", wrong);
	check(wrong == 0, "the gable under a roof of another paint is the roof's, the wall below the eave the wall's");
}

double ballDistance(const Place& p, const Place& centre, double radius) {
	const Place d = { p[0] - centre[0], p[1] - centre[1], p[2] - centre[2] };
	return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - radius;
}

void checkBlend() {
	const SceneFile file = sceneOrExit(writeScene("blend", R"(
		camera {
			eye    = [0, -40, 0];
			target = [0, 0, 0];
			fov    = 0.9;
		}

		function sphere(r) {
			return length(p) - r;
		}

		merge(0.4) {
			paint(matte([0.8, 0.2, 0.2])) at([-0.4, 0, 0]) sphere(0.6);
			paint(matte([0.2, 0.2, 0.8])) at([0.5, 0.1, 0.05]) sphere(0.4);
		}

	)"));

	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	int wrong  = 0;
	int tested = 0;
	for (int i = 0; i < 31; ++i) {
		for (int k = 0; k < 31; ++k) {
			const Ray ray = {
				.origin    = { -0.3f + 0.6f * float(i) / 30, -5, -0.3f + 0.6f * float(k) / 30 },
				.direction = { 0, 1, 0 },
				.far       = 1000,
				.inside    = false,
				.within    = 0,
			};

			const Hit hit  = intersect(tree, ray);
			const Place p  = rayPoint(ray, hit.distance);
			const double a = ballDistance(p, { -0.4, 0, 0 }, 0.6);
			const double b = ballDistance(p, { 0.5, 0.1, 0.05 }, 0.4);
			const bool far = hit.kind != HitKind::Missed && std::abs(a - b) > 0.02;
			tested += far ? 1 : 0;
			wrong += far && hit.leaf != (a < b ? 0u : 1u) ? 1 : 0;
		}
	}
	std::printf("walk blend: %d of %d hits in a smooth join take the paint of the farther operand\n", wrong, tested);
	check(tested > 300 && wrong == 0, "in a smooth join of two paints a hit takes the paint of the nearer operand there");
}

std::vector<std::uint32_t> planeWords(float slope, float offset) {
	return { std::bit_cast<std::uint32_t>(slope), 0, 0, std::bit_cast<std::uint32_t>(offset) };
}

void checkEntryCrossing() {
	const SceneFile file = sceneOrExit(writeScene("entry", R"(
		function sphere(r) {
			return length(p) - r;
		}

		sphere(1);
	)"));

	const RunHeader header = {
		.count     = 2,
		.patches   = 0,
		.planes    = 3,
		.needed    = 3,
		.slabbed   = false,
		.passWords = 0,
	};

	std::vector<std::uint32_t> run          = { runHeaderWord(header), 1u << 3 };
	const std::vector<std::uint32_t> first  = planeWords(-2, -0.00001f);
	const std::vector<std::uint32_t> second = planeWords(-2, 1);
	run.insert(run.end(), first.begin(), first.end());
	run.insert(run.end(), second.begin(), second.end());

	const SurfaceLeaf leaf = {
		.run      = 0,
		.programs = 0,
		.loosened = 0,
		.kind     = LeafKind::Held,
	};

	const ProgramList list = {
		.first = 0,
		.count = 1,
	};

	const ProgramReference reference = {
		.index  = 0,
		.source = ProgramSource::Object,
	};

	const Region region = {
		.lower = { 0, 0, 0 },
		.size  = { 2, 2, 2 },
	};

	const CellEntry root = {
		.kind    = CellKind::Surface,
		.payload = 0,
	};

	const Tree tree = {
		.region     = region,
		.root       = root,
		.nodes      = {},
		.surfaces   = { leaf },
		.solids     = {},
		.runs       = run,
		.lists      = { list },
		.references = { reference },
		.cuts       = {},
		.cutSteps   = {},
	};

	const Geometry geometry = {
		.scene = file.scene,
		.tree  = tree,
	};

	const Ray ray = {
		.origin    = { -5, 1, 1 },
		.direction = { 1, 0, 0 },
		.far       = 1000,
		.inside    = false,
		.within    = 0,
	};

	const Hit hit = intersect(geometry, ray);

	const Near near = {
		.got       = hit.kind != HitKind::Missed ? hit.distance : 0,
		.want      = 6,
		.tolerance = 1e-4,
		.what      = "a ray entering a leaf just past one face's crossing still meets the next face",
	};

	checkNear(near);
}

void checkThinSolid() {
	const SceneFile file = sceneOrExit(writeScene("thin", R"(
		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		camera {
			eye    = [0, -12, 6];
			target = [0, 0, 0];
			fov    = 0.9;
		}

		paint(matte([0.8, 0.2, 0.2])) at([0, 0, -2]) box([80, 80, 4]);
		paint(matte([0.2, 0.2, 0.8])) at([0.3, 0.2, 0.0005]) box([60, 60, 0.001]);
	)"));

	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	int wrong = 0;
	for (int k = 0; k < 64; ++k) {
		const Ray ray = {
			.origin    = { -1.9 + 0.06 * (k % 8) * 8, -1.9 + 0.48 * (k / 8), 5 },
			.direction = { 0.05, 0.03, -std::sqrt(1 - 0.05 * 0.05 - 0.03 * 0.03) },
			.far       = 1000,
			.inside    = false,
			.within    = 0,
		};

		const Hit hit = intersect(tree, ray);
		wrong += hit.kind == HitKind::Surface && std::abs(hit.point.z - 0.001) < 0.0001 && hit.leaf == 1 ? 0 : 1;
	}
	std::printf("walk thin: %d of 64 rays on a plate 1 mm thick miss its leaf\n", wrong);
	check(wrong == 0, "a hit on a thin solid lying on another takes the thin solid's own leaf");
}

double ringAlongLeg(const std::vector<float>& ring, const Leg& leg, double s) {
	double v[3];
	for (int k = 0; k < 3; ++k) {
		v[k] = ring[std::size_t(k)];
		for (int axis = 0; axis < 3; ++axis) {
			v[k] += double(ring[std::size_t(3 + 3 * axis + k)]) * (double(leg.base[axis]) + double(leg.rate[axis]) * s);
		}
	}
	const double minor  = ring[13];
	const double around = v[0] * v[0] + v[1] * v[1];
	const double sum    = around + v[2] * v[2] + 1 - minor * minor;
	return sum * sum - 4 * around;
}

std::vector<double> numericRoots(const std::vector<float>& ring, const Leg& leg) {
	std::vector<double> roots;
	const int steps = 20000;
	for (int k = 0; k < steps; ++k) {
		double a = double(k) / steps;
		double b = double(k + 1) / steps;
		if ((ringAlongLeg(ring, leg, a) < 0) == (ringAlongLeg(ring, leg, b) < 0)) {
			continue;
		}
		for (int i = 0; i < 60; ++i) {
			const double m   = (a + b) / 2;
			const bool lower = (ringAlongLeg(ring, leg, a) < 0) == (ringAlongLeg(ring, leg, m) < 0);
			a                = lower ? m : a;
			b                = lower ? b : m;
		}
		roots.push_back((a + b) / 2);
	}
	return roots;
}

void checkRingRoots() {
	const std::vector<float> ring = { -1.6f, -1.5f, -0.7f, 3.2f, 0.1f, 0, -0.1f, 3.0f, 0.2f, 0, -0.3f, 1.4f, 1, 0.35f };

	const RunHeader header = {
		.count     = 1,
		.patches   = 1,
		.planes    = 1,
		.needed    = 1,
		.slabbed   = false,
		.passWords = 0,
	};

	std::vector<std::uint32_t> words = { runHeaderWord(header) };
	for (const float value : ring) {
		words.push_back(std::bit_cast<std::uint32_t>(value));
	}
	const LeafRun run = leafRun(words.data());
	std::mt19937 random(12);
	std::uniform_real_distribution<float> unit(0, 1);
	int wrong   = 0;
	int crossed = 0;
	for (int k = 0; k < 400; ++k) {
		const float a[3] = { unit(random), unit(random), unit(random) };
		const float b[3] = { unit(random), unit(random), unit(random) };

		const Leg leg = {
			.base = { a[0], a[1], a[2] },
			.rate = { b[0] - a[0], b[1] - a[1], b[2] - a[2] },
		};

		const LeafVisit visit = {
			.run    = run,
			.leg    = leg,
			.inside = true,
		};

		const Crossings crossings       = solvedFaces(visit).crossings;
		const std::vector<double> wants = numericRoots(ring, visit.leg);
		std::vector<double> gots;
		for (int i = 0; i < crossings.count; ++i) {
			const double at = crossings.items[std::size_t(i)].at;
			if (at > 0 && at < 1) {
				gots.push_back(at);
			}
		}
		bool same = gots.size() == wants.size();
		for (std::size_t i = 0; same && i < wants.size(); ++i) {
			same = std::abs(gots[i] - wants[i]) < 1e-4;
		}
		wrong += same ? 0 : 1;
		crossed += wants.empty() ? 0 : 1;
	}
	std::printf("walk ring: %d of 400 legs (%d crossing the ring) find other roots than a numeric search\n", wrong, crossed);
	check(crossed > 100 && wrong == 0, "a ring face's quartic finds the roots a numeric search finds");
}

int leakedRays(const char* name, const char* text) {
	const SceneFile file = sceneOrExit(writeScene(name, text));
	const TestTree built = testTree(file, 8);

	const Geometry tree = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	const std::uint32_t glass = file.scene.leaves[1].material;
	int wrong                 = 0;
	for (int k = 0; k < 64; ++k) {
		const double dx     = -0.3 + 0.6 * (k % 8) / 7;
		const double dy     = -0.3 + 0.6 * (k / 8) / 7;
		const double length = std::sqrt(dx * dx + dy * dy + 1);

		const Ray ray = {
			.origin    = { 0.2 * dx, -0.1 * dy, 0.4 },
			.direction = { dx / length, dy / length, -1 / length },
			.far       = 1000,
			.inside    = true,
			.within    = glass,
		};

		const Hit hit = intersect(tree, ray);
		wrong += hit.kind == HitKind::Surface && std::abs(hit.distance - 0.4 * length) < 1e-4 && hit.leaf == 0 && hit.normal.z < -0.99 ? 0 : 1;
	}
	return wrong;
}

void checkContact() {
	const int below = leakedRays("contact", R"(
		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		paint(matte([0.1, 0.1, 0.1])) at([0, 0, -0.1]) box([4, 4, 0.2]);
		paint(glass(refraction = 1.5, roughness = 0)) at([0, 0, 0.5]) box([1, 1, 1]);
	)");

	const int joined = leakedRays("contact-joined", R"(
		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		merge {
			paint(matte([0.1, 0.1, 0.1])) at([0, 0, -0.1]) box([4, 4, 0.2]);
			paint(glass(refraction = 1.5, roughness = 0)) at([0, 0, 0.5]) box([1, 1, 1]);
		}

	)");

	std::printf("walk contact: %d and %d of 64 rays from a glass box down to the slab it rests on miss the interface\n", below, joined);
	check(below == 0 && joined == 0, "a ray inside a glass resting on a slab stops where it passes into the slab, as two objects and as one union");
}

bool sameLanding(const Landing& a, const Landing& b) {
	bool same = a.found == b.found && std::bit_cast<std::uint32_t>(a.at) == std::bit_cast<std::uint32_t>(b.at) && a.passings.count == b.passings.count;
	for (int k = 0; same && k < a.passings.count; ++k) {
		same = std::bit_cast<std::uint32_t>(a.passings.at[std::size_t(k)]) == std::bit_cast<std::uint32_t>(b.passings.at[std::size_t(k)]);
	}
	return same;
}

Leg randomLeg(std::mt19937& random) {
	std::uniform_real_distribution<float> unit(0, 1);
	const float a[3] = { unit(random), unit(random), unit(random) };
	const float b[3] = { unit(random), unit(random), unit(random) };
	const float part = unit(random) < 0.5f ? 1.0f : unit(random);
	return Leg {
		.base = { a[0], a[1], a[2] },
		.rate = { (b[0] - a[0]) * part, (b[1] - a[1]) * part, (b[2] - a[2]) * part },
	};
}

struct SlabTally {
	int differ;
	int answered[4];
};

int answeredBySlab(const LeafRun& run, const SlabLeg& slab, std::uint32_t face) {
	if (!run.header.slabbed || faceKind(run.header, face) == FaceKind::Plane) {
		return 0;
	}
	return slabSide(slab, run.words + run.offsets[face + 1] - slabWordCount) != 0 ? 1 : 0;
}

void tallyLeg(SlabTally& tally, const LeafRun& run, const LeafRun& bare, const Leg& leg) {
	const SlabLeg slab = slabLegOf(leg.base, leg.rate);
	for (std::uint32_t face = 0; face < run.header.count; ++face) {
		tally.answered[int(faceKind(run.header, face))] += answeredBySlab(run, slab, face);
	}
	for (const bool inside : { false, true }) {
		const LeafVisit slabbed = {
			.run    = run,
			.leg    = leg,
			.inside = inside,
		};

		const LeafVisit solved = {
			.run    = bare,
			.leg    = leg,
			.inside = inside,
		};

		tally.differ += sameLanding(landLeaf(slabbed), landLeaf(solved)) && reachesSurface(slabbed) == reachesSurface(solved) ? 0 : 1;
	}
}

void checkSlabLandings() {
	const SceneFile file   = sceneOrExit(writeScene("slabs", R"(
		function sphere(r) {
			return length(p) - r;
		}

		function torus(big, small) {
			return length([length(p.xy) - big, p.z]) - small;
		}

		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		paint(matte([0.8, 0.8, 0.8])) at([0, 0, -0.5]) box([6, 6, 1]);
		paint(matte([0.8, 0.2, 0.2])) at([-1.5, 0, 0.6]) sphere(0.6);
		paint(matte([0.2, 0.8, 0.2])) at([1.5, 0, 0.4]) turn(x, 0.6) torus(0.7, 0.2);

		merge(0.3) {
			paint(matte([0.2, 0.2, 0.8])) at([0, 1.5, 0.5]) sphere(0.5);
			paint(matte([0.2, 0.2, 0.8])) at([0.4, 1.8, 0.7]) sphere(0.4);
		}

	)"));
	const TestTree built   = testTree(file, 8);
	const TestTree unbuilt = passedTestTree(file, passesWithoutFaceSlabs(), 8);
	const Tree& tree       = built.tree;
	const Tree& bare       = unbuilt.tree;
	std::mt19937 random(21);

	SlabTally tally = {
		.differ   = 0,
		.answered = { 0, 0, 0, 0 },
	};

	for (std::size_t k = 0; k < tree.surfaces.size() && tree.surfaces.size() == bare.surfaces.size(); ++k) {
		const LeafRun run       = leafRun(tree.runs.data() + tree.surfaces[k].run);
		const LeafRun unslabbed = leafRun(bare.runs.data() + bare.surfaces[k].run);
		for (int leg = 0; leg < 32; ++leg) {
			tallyLeg(tally, run, unslabbed, randomLeg(random));
		}
	}
	std::printf("walk slabs: %zu and %zu leaves with the face slabs on and off, %zu and %zu run words; slabs answer %d quadric, %d patch and %d ring faces; %d landings differ from solving every face\n", tree.surfaces.size(), bare.surfaces.size(), tree.runs.size(), bare.runs.size(), tally.answered[1], tally.answered[2], tally.answered[3], tally.differ);
	check(tree.surfaces.size() == bare.surfaces.size(), "the face slabs keep every leaf");
	check(!faceSlabsBuilt || (tally.answered[1] > 400 && tally.answered[2] > 400 && tally.answered[3] > 20), "the slabs of quadric, patch and ring faces answer many legs");
	check(faceSlabsBuilt || tree.runs.size() == bare.runs.size(), "a build with the face slabs off keeps no slabs");
	check(tally.differ == 0, "a face its slab keeps to one side changes no landing, to the bit");
}

void checkWalk() {
	checkEntryCrossing();
	checkRingRoots();
	checkSlabLandings();
	checkExit();
	checkBlend();
	checkFar();
	checkStripe();
	checkGable();
	checkLawn();
	checkThinSolid();
	checkContact();
	checkScene("scenes/magic-cube.sdf");
	checkScene("scenes/chess-board.sdf");
}

}
