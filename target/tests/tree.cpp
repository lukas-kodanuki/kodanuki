#include "check.h"
#include "extras/build_correct_face_slabs/face_slabs.h"
#include "frame/frame.h"
#include "frame/run.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"
#include "tree/build.h"
#include "tree/classify.h"
#include "tree/cut.h"
#include "tree/grow.h"
#include "tree/pack.h"
#include "tree/pass.h"
#include "tree/patch.h"
#include "walk/walk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

namespace kodanuki {

using Spot = std::array<double, 3>;

struct Sampler {
	std::mt19937 random;
	std::vector<Spot> centres;
	double spread;
};

struct Level {
	const Program& whole;
	const Cut& cut;
	const Box& box;
};

struct Tally {
	double worst;
	std::size_t empty;
	std::size_t filled;
	std::size_t wrong;
	std::size_t before;
	std::size_t after;
};

Spot leafOrigin(const Leaf& leaf) {
	return { leaf.placement.origin[0], leaf.placement.origin[1], leaf.placement.origin[2] };
}

std::vector<Spot> nodeCentres(const Scene& scene, std::uint32_t index) {
	const Node& node = scene.nodes[index];
	if (node.join == Join::Leaf) {
		return { leafOrigin(scene.leaves[node.leaf]) };
	}
	std::vector<Spot> centres;
	for (const std::uint32_t child : node.children) {
		const std::vector<Spot> more = nodeCentres(scene, child);
		centres.insert(centres.end(), more.begin(), more.end());
	}
	return centres;
}

Sampler samplerFor(const std::vector<Spot>& centres, double spread) {
	return Sampler {
		.random  = std::mt19937(11),
		.centres = centres,
		.spread  = spread,
	};
}

std::vector<Spot> sceneCentres(const Scene& scene) {
	std::vector<Spot> centres;
	for (const Leaf& leaf : scene.leaves) {
		centres.push_back(leafOrigin(leaf));
	}
	return centres;
}

Spot nearSpot(Sampler& sampler) {
	std::uniform_int_distribution<std::size_t> pick(0, sampler.centres.size() - 1);
	std::uniform_real_distribution<double> offset(-sampler.spread, sampler.spread);
	const Spot& centre = sampler.centres[pick(sampler.random)];
	return { centre[0] + offset(sampler.random), centre[1] + offset(sampler.random), centre[2] + offset(sampler.random) };
}

double insideSpan(Sampler& sampler, const math::Interval& span) {
	std::uniform_real_distribution<double> unit(0, 1);
	return span.lower + (span.upper - span.lower) * unit(sampler.random);
}

Spot insideBox(Sampler& sampler, const Box& box) {
	return { insideSpan(sampler, box.axes[0]), insideSpan(sampler, box.axes[1]), insideSpan(sampler, box.axes[2]) };
}

double programAt(const Program& program, const Spot& p) {
	std::vector<double> registers;

	const Probe probe = {
		.p         = { p[0], p[1], p[2] },
		.registers = registers,
	};

	return programDistance(program, probe);
}

double nodeAt(const Scene& scene, std::uint32_t node, const Spot& p) {
	std::vector<double> registers;

	const Probe probe = {
		.p         = { p[0], p[1], p[2] },
		.registers = registers,
	};

	return nodeDistance(scene, node, probe);
}

void checkObjectPrograms(const char* path) {
	const SceneFile file = sceneOrExit(path);
	const Scene& scene   = file.scene;
	Sampler sampler      = samplerFor(sceneCentres(scene), 0.6);
	double worst         = 0;
	for (int s = 0; s < 300; ++s) {
		const Spot p = nearSpot(sampler);
		for (std::uint32_t i = 0; i < scene.objects.size(); ++i) {
			worst = std::max(worst, std::abs(programAt(scene.objectPrograms[i], p) - nodeAt(scene, scene.objects[i], p)));
		}
	}

	const Near agreement = {
		.got       = worst,
		.want      = 0,
		.tolerance = 1e-9,
		.what      = path,
	};

	checkNear(agreement);
}

math::Interval cellSpan(double coordinate, double size) {
	const double lower = std::floor(coordinate / size) * size;
	return math::Interval {
		.lower = lower,
		.upper = lower + size,
	};
}

Box cellAround(const Spot& p, double size) {
	return Box {
		.axes = { cellSpan(p[0], size), cellSpan(p[1], size), cellSpan(p[2], size) },
	};
}

bool agreesWithOccupancy(Occupancy occupancy, double value) {
	switch (occupancy) {
		case Occupancy::Empty: return value > 0;
		case Occupancy::Filled: return value < 0;
		case Occupancy::Surface: return true;
	}

	return true;
}

void checkLevel(Tally& tally, Sampler& sampler, const Level& level) {
	tally.empty += level.cut.occupancy == Occupancy::Empty ? 1 : 0;
	tally.filled += level.cut.occupancy == Occupancy::Filled ? 1 : 0;
	tally.before += level.whole.steps.size();
	tally.after += level.cut.program.steps.size();
	for (int s = 0; s < 8; ++s) {
		const Spot p       = insideBox(sampler, level.box);
		const double whole = programAt(level.whole, p);
		tally.worst        = std::max(tally.worst, std::abs(programAt(level.cut.program, p) - whole));
		tally.wrong += agreesWithOccupancy(level.cut.occupancy, whole) ? 0 : 1;
	}
}

void checkCutChain(std::vector<Tally>& tallies, Sampler& sampler, const Program& whole) {
	const double sizes[3] = { 1, 0.25, 0.0625 };
	const Spot p          = nearSpot(sampler);
	Program parent        = whole;
	for (int k = 0; k < 3; ++k) {
		const Box box = cellAround(p, sizes[k]);
		const Cut cut = cutProgram(parent, box);

		const Level level = {
			.whole = whole,
			.cut   = cut,
			.box   = box,
		};

		checkLevel(tallies[k], sampler, level);
		parent = cut.program;
	}
}

Tally emptyTally() {
	return Tally {
		.worst  = 0,
		.empty  = 0,
		.filled = 0,
		.wrong  = 0,
		.before = 0,
		.after  = 0,
	};
}

void checkCuts() {
	const SceneFile file = sceneOrExit("scenes/chess-board.sdf");
	std::vector<Tally> tallies(3, emptyTally());
	for (std::uint32_t i = 0; i < file.scene.objectPrograms.size(); ++i) {
		Sampler sampler = samplerFor(nodeCentres(file.scene, file.scene.objects[i]), 0.4);
		for (int s = 0; s < 40; ++s) {
			checkCutChain(tallies, sampler, file.scene.objectPrograms[i]);
		}
	}
	for (const Tally& tally : tallies) {
		check(tally.worst <= 1e-12, "a cut program equals the whole one inside its box");
		check(tally.wrong == 0, "empty and filled cells agree with point samples");
	}
	check(tallies[2].after * 4 < tallies[2].before, "small cells cut programs to under a quarter");
	check(tallies[2].empty > 0 && tallies[2].filled > 0, "small cells are found empty and filled");
}

math::Interval pointAt(double x) {
	return math::Interval {
		.lower = x,
		.upper = x,
	};
}

Box boxAt(const Spot& p, const Spot& shift) {
	return Box {
		.axes = { pointAt(p[0] - shift[0]), pointAt(p[1] - shift[1]), pointAt(p[2] - shift[2]) },
	};
}

Box grownBox(const Box& box, const Spot& grow) {
	Box grown = box;
	for (int axis = 0; axis < 3; ++axis) {
		grown.axes[axis].lower -= grow[axis];
		grown.axes[axis].upper += grow[axis];
	}
	return grown;
}

Spot lensDirection(const Lens& lens, double u, double v) {
	Spot d = { 0, 0, 0 };
	for (int axis = 0; axis < 3; ++axis) {
		d[axis] = math::component(lens.forward, axis) + math::component(lens.right, axis) * u * lens.reach * lens.width / lens.height + math::component(lens.up, axis) * v * lens.reach;
	}
	return d;
}

void checkFocusOnView() {
	const SceneFile file = sceneOrExit(writeScene("view", R"(
		render {
			width  = 64;
			height = 40;
		}

		camera {
			eye    = [0, -10, 3];
			target = [1, 0, 0];
			fov    = 0.7;
		}

		function sphere(r) {
			return length(p) - r;
		}

		sphere(1);
	)"));

	const Region region = {
		.lower = { -50, -50, -50 },
		.size  = { 100, 100, 100 },
	};

	const Detail detail = testDetail(file, region, defaultDetailMembers());
	const Lens lens     = lensOf(file.shot.camera, file.shot.render);
	std::mt19937 random(5);
	std::uniform_real_distribution<double> unit(-1, 1);
	std::uniform_real_distribution<double> depth(0.01, 60);
	int hidden = 0;
	for (int s = 0; s < 4000; ++s) {
		const bool corner = s % 4 == 0;
		const double u    = corner ? (unit(random) < 0 ? -1 : 1) : unit(random);
		const double v    = corner ? (unit(random) < 0 ? -1 : 1) : unit(random);
		const Spot d      = lensDirection(lens, u, v);
		const double t    = depth(random);
		const Spot p      = { lens.eye.x + d[0] * t, lens.eye.y + d[1] * t, lens.eye.z + d[2] * t };
		const Spot grow   = { 0.5 + unit(random) * 0.5, 0.5 + unit(random) * 0.5, 0.5 + unit(random) * 0.5 };
		const Spot shift  = { unit(random) * grow[0], unit(random) * grow[1], unit(random) * grow[2] };
		const Box box     = grownBox(boxAt(p, shift), grow);
		hidden += cellBeyond(detail.screen, box) ? 1 : 0;
	}
	std::printf("tree view: %d of 4000 cells on camera rays counted unseen\n", hidden);
	check(hidden == 0, "a cell any camera ray passes through keeps the seen tolerance");

	const Spot behind = { lens.eye.x - lens.forward.x * 5, lens.eye.y - lens.forward.y * 5, lens.eye.z - lens.forward.z * 5 };
	const Spot side   = lensDirection(lens, 1.3, 0);
	const Spot beside = { lens.eye.x + side[0] * 20, lens.eye.y + side[1] * 20, lens.eye.z + side[2] * 20 };
	const Spot half   = { 0.3, 0.3, 0.3 };
	const Box back    = grownBox(boxAt(behind, { 0, 0, 0 }), half);
	const Box out     = grownBox(boxAt(beside, { 0, 0, 0 }), half);
	const double seen = detail.share * footprintOver(detail.view, out);
	check(cellBeyond(detail.screen, back) && cellBeyond(detail.screen, out) && cellTolerance(detail, out) > 10 * seen, "cells behind the eye or beside the frame are unseen and held looser");
}

Program sphereProgram() {
	const SceneFile file = sceneOrExit(writeScene("patch", R"(
		function sphere(r) {
			return length(p) - r;
		}

		sphere(1);
	)"));

	return file.scene.objectPrograms[0];
}

Box cubeAt(const Spot& lower, double size) {
	return Box {
		.axes = { math::Interval { .lower = lower[0], .upper = lower[0] + size }, math::Interval { .lower = lower[1], .upper = lower[1] + size }, math::Interval { .lower = lower[2], .upper = lower[2] + size } },
	};
}

double patchAtWorld(const Patch& patch, const Box& box, const Spot& p) {
	double u[3];
	for (int axis = 0; axis < 3; ++axis) {
		u[axis] = (p[std::size_t(axis)] - box.axes[axis].lower) / (box.axes[axis].upper - box.axes[axis].lower);
	}
	return patchValue(patch, u);
}

void checkPatchFit() {
	const std::vector<Program> programs                      = { sphereProgram() };
	const std::vector<std::shared_ptr<const Program>> shared = { std::make_shared<const Program>(programs[0]) };
	const Box box                                            = cubeAt({ 0.55, 0.55, 0.55 }, 0.05);

	const PatchQuery query = {
		.programs = shared,
		.box      = box,
	};

	const Fitted fitted = fitPatch(query);
	std::mt19937 random(3);
	double worst             = 0;
	bool netted              = true;
	const math::Interval net = patchNet(fitted.patch);
	for (int s = 0; s < 500; ++s) {
		Sampler sampler = samplerFor({}, 0);
		sampler.random  = std::mt19937(unsigned(s));
		const Spot p    = insideBox(sampler, box);
		const double v  = patchAtWorld(fitted.patch, box, p);
		worst           = std::max(worst, std::abs(v - programAt(programs[0], p)));
		netted          = netted && v >= net.lower && v <= net.upper;
	}
	std::printf("tree patch: sphere over a 0.05 cell strays %.3g at the fit's points, %.3g at random points\n", fitted.stray, worst);
	check(fitted.patch.bent && fitted.stray < 1e-5 && worst < 2e-5, "a patch over a small cell of a sphere bends and strays little");
	check(netted, "a patch's Bernstein net holds every value it takes over its cell");

	const std::vector<std::uint32_t> words = patchWords(fitted.patch);
	const Patch read                       = readPatch(words.data());
	bool same                              = read.bent == fitted.patch.bent && read.step == fitted.patch.step && words.size() == patchWordCount(words.data());
	for (int k = 0; k < 20; ++k) {
		same = same && read.weights[k] == fitted.patch.weights[k];
	}
	check(same && words.size() == 11, "a bent patch face is eleven words and reads back the same to the bit");

	const Box beside = cubeAt({ 0.6, 0.55, 0.55 }, 0.05);

	const PatchQuery next = {
		.programs = shared,
		.box      = beside,
	};

	const Fitted neighbour = fitPatch(next);
	double gap             = 0;
	for (int s = 0; s < 100; ++s) {
		const Spot p = { 0.6, 0.55 + 0.05 * (s % 10) / 9.0, 0.55 + 0.05 * (s / 10) / 9.0 };
		gap          = std::max(gap, std::abs(patchAtWorld(fitted.patch, box, p) - patchAtWorld(neighbour.patch, beside, p)));
	}
	check(gap <= 10 * double(fitted.patch.step + neighbour.patch.step), "two patches of one size read the same field on the face they share, up to their rounding");
}

double zeroAlongW(const Patch& patch, double u, double v) {
	for (int k = 0; k < 200; ++k) {
		double low        = k / 200.0;
		double high       = (k + 1) / 200.0;
		const double a[3] = { u, v, low };
		const double b[3] = { u, v, high };
		if ((patchValue(patch, a) < 0) == (patchValue(patch, b) < 0)) {
			continue;
		}
		for (int step = 0; step < 50; ++step) {
			const double middle = (low + high) / 2;
			const double m[3]   = { u, v, middle };
			const bool below    = (patchValue(patch, m) < 0) == (patchValue(patch, a) < 0);
			low                 = below ? middle : low;
			high                = below ? high : middle;
		}
		return (low + high) / 2;
	}
	return -1;
}

void checkPatchSlab() {
	const std::vector<std::shared_ptr<const Program>> programs = { std::make_shared<const Program>(sphereProgram()) };
	const Box box                                              = cubeAt({ 0.5, 0.5, 0.55 }, 0.1);

	const PatchQuery query = {
		.programs = programs,
		.box      = box,
	};

	const Patch patch = fitPatch(query).patch;
	const Slab slab   = patchSlab(patch);
	int outside       = 0;
	int zeros         = 0;
	for (int i = 0; i <= 40; ++i) {
		for (int j = 0; j <= 40; ++j) {
			const double zero = zeroAlongW(patch, i / 40.0, j / 40.0);
			if (zero < 0) {
				continue;
			}
			const double rise = slab.slope[0] * i / 40.0 + slab.slope[1] * j / 40.0 + slab.slope[2] * zero + slab.offset;
			zeros += 1;
			outside += std::abs(rise) > slab.stray ? 1 : 0;
		}
	}
	std::printf("tree slab: %d zeros, %d outside a stray of %g (slope %g %g %g, offset %g)\n", zeros, outside, slab.stray, slab.slope[0], slab.slope[1], slab.slope[2], slab.offset);
	check(zeros > 100 && slab.stray < 0.01 && outside == 0, "every zero of a patch lies within its slab's stray of the slab's plane");
}

double quadricAt(const std::vector<float>& f, const double* u) {
	const double square = f[0] * u[0] * u[0] + f[1] * u[1] * u[1] + f[2] * u[2] * u[2];
	const double cross  = f[3] * u[0] * u[1] + f[4] * u[0] * u[2] + f[5] * u[1] * u[2];
	return square + cross + f[6] * u[0] + f[7] * u[1] + f[8] * u[2] + f[9];
}

double ringAt(const std::vector<float>& f, const double* u) {
	double v[3];
	for (int k = 0; k < 3; ++k) {
		v[k] = f[std::size_t(k)] + f[std::size_t(3 + k)] * u[0] + f[std::size_t(6 + k)] * u[1] + f[std::size_t(9 + k)] * u[2];
	}
	const double major = f[12];
	const double minor = f[13];
	const double q     = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + major * major - minor * minor;
	const double t     = q * q - 4 * major * major * (v[0] * v[0] + v[1] * v[1]);
	return minor < 0 ? -t : t;
}

struct SlabCount {
	int decided;
	int wrong;
};

std::vector<float> randomQuadric(std::mt19937& random) {
	std::uniform_real_distribution<float> coefficient(-1, 1);
	std::vector<float> f;
	for (int k = 0; k < 10; ++k) {
		f.push_back(coefficient(random));
	}
	return f;
}

std::vector<float> randomRing(std::mt19937& random) {
	std::uniform_real_distribution<float> origin(-1.2f, 1.2f);
	std::uniform_real_distribution<float> axis(-0.4f, 0.4f);
	std::uniform_real_distribution<float> minor(0.1f, 0.5f);
	std::vector<float> f = { origin(random), origin(random), origin(random) };
	for (int k = 0; k < 9; ++k) {
		f.push_back(axis(random));
	}
	f.push_back(1);
	f.push_back(minor(random) * (random() % 2 == 0 ? 1.0f : -1.0f));
	return f;
}

void countSlab(SlabCount& count, const Slab& slab, double value, const double* u) {
	const double plane = slab.slope[0] * u[0] + slab.slope[1] * u[1] + slab.slope[2] * u[2] + slab.offset;
	if (std::abs(plane) <= slab.stray) {
		return;
	}
	count.decided += 1;
	count.wrong += (plane > 0) == (value > 0) ? 0 : 1;
}

void checkFaceSlabs() {
	std::mt19937 random(31);
	std::uniform_real_distribution<double> widened(-slabReach, 1 + slabReach);

	SlabCount quadrics = {
		.decided = 0,
		.wrong   = 0,
	};

	SlabCount rings = quadrics;
	for (int face = 0; face < 200; ++face) {
		const std::vector<float> quadric = randomQuadric(random);
		const std::vector<float> ring    = randomRing(random);
		const Slab quadricBound          = quadricSlab(quadric.data());
		const Slab ringBound             = ringSlab(ring.data());
		for (int k = 0; k < 200; ++k) {
			const double u[3] = { widened(random), widened(random), widened(random) };
			countSlab(quadrics, quadricBound, quadricAt(quadric, u), u);
			countSlab(rings, ringBound, ringAt(ring, u), u);
		}
	}
	std::printf("tree face slabs: of 40000 points the slab decides %d for quadrics (%d wrong) and %d for rings (%d wrong)\n", quadrics.decided, quadrics.wrong, rings.decided, rings.wrong);
	check(quadrics.decided > 10000 && rings.decided > 4000 && quadrics.wrong == 0 && rings.wrong == 0, "where a quadric's or a ring's slab plane stands further off than the stray, the face has the plane's sign");
}

struct Shared {
	std::size_t differ;
	std::size_t hits;
};

struct Passing {
	const Scene& scene;
	const Detail& detail;
	const std::vector<PassMember>& passes;
	Region region;
};

Tree passedTree(const Passing& passing) {
	const BuildInput input = {
		.scene    = passing.scene,
		.detail   = passing.detail,
		.passes   = passing.passes,
		.region   = passing.region,
		.depthCap = depthCapOf(passing.region, passing.detail.least),
		.workers  = 8,
		.faces    = true,
	};

	return buildTree(input);
}

Detail testDetail(const SceneFile& file, const Region& region, const std::vector<DetailMember>& members) {
	const SceneDetailQuery query = {
		.scene   = file.scene,
		.shot    = file.shot,
		.region  = region,
		.members = members,
	};

	return sceneDetail(query);
}

TestTree passedTestTree(const SceneFile& file, const std::vector<PassMember>& passes, int workers) {
	const Region region = sceneRegion(file.scene, file.shot, 4);
	const Detail detail = testDetail(file, region, defaultDetailMembers());

	const BuildInput input = {
		.scene    = file.scene,
		.detail   = detail,
		.passes   = passes,
		.region   = region,
		.depthCap = depthCapOf(region, detail.least),
		.workers  = workers,
		.faces    = true,
	};

	return TestTree {
		.detail = detail,
		.tree   = buildTree(input),
	};
}

TestTree testTree(const SceneFile& file, int workers) {
	return passedTestTree(file, defaultPassMembers(), workers);
}

std::vector<PassMember> passesWithoutFaceSlabs() {
	std::vector<PassMember> passes = defaultPassMembers();
	std::erase(passes, PassMember::FaceSlabs);
	return passes;
}

CellPrograms allObjects(const std::vector<Program>& programs) {
	CellPrograms cell = {
		.objects  = {},
		.programs = {},
	};

	for (std::uint32_t i = 0; i < programs.size(); ++i) {
		cell.objects.push_back(i);
		cell.programs.push_back(std::make_shared<const Program>(programs[i]));
	}
	return cell;
}

struct Asked {
	Box box;
	double tolerance;
	bool deeper;
};

Plan classified(const std::vector<Program>& programs, const Asked& asked) {
	const CellPrograms cell = allObjects(programs);

	const CellQuery query = {
		.programs  = cell,
		.box       = asked.box,
		.tolerance = asked.tolerance,
		.deeper    = asked.deeper,
		.faces     = true,
	};

	return classifyCell(query);
}

bool hasPatchFace(const Plan& plan) {
	return plan.kind == CellKind::Surface && !plan.patched && runHeader(plan.run[0]).patches != 0;
}

void checkClassifySphere() {
	const std::vector<Program> programs = { sphereProgram() };

	const Asked asked = {
		.box       = cubeAt({ 0.53, 0.53, 0.53 }, 0.1),
		.tolerance = 1e-4,
		.deeper    = true,
	};

	const Plan plan        = classified(programs, asked);
	const RunHeader header = runHeader(plan.run.empty() ? 0 : plan.run[0]);
	check(plan.kind == CellKind::Surface && !plan.patched && plan.leafKind == LeafKind::Held && header.count == 1 && faceKind(header, 0) == FaceKind::Quadric, "a cell on a sphere is one proven quadric face");
}

int unprovenFaces(const std::vector<Program>& programs, const Asked& asked) {
	const CellCut cut = cutCell(allObjects(programs), asked.box);

	const Cell cell = {
		.programs  = cut.programs.programs,
		.box       = asked.box,
		.tolerance = asked.tolerance,
	};

	int unproven = 0;
	for (const Face& face : cellFaces(cell).faces) {
		unproven += face.proven ? 0 : 1;
	}
	return unproven;
}

void checkClassifyBlend() {
	const SceneFile file = sceneOrExit(writeScene("blend", R"(
		function sphere(r) {
			return length(p) - r;
		}

		merge(0.3) {
			at([-0.4, 0, 0]) sphere(0.5);
			at([0.4, 0, 0]) sphere(0.5);
		}

	)"));

	int patched  = 0;
	int wrong    = 0;
	int surfaces = 0;
	for (int i = 0; i < 14; ++i) {
		for (int j = 0; j < 5; ++j) {
			const Asked asked = {
				.box       = cubeAt({ -0.35 + 0.05 * i, 0.3 + 0.05 * j, 0 }, 0.05),
				.tolerance = 1e-4,
				.deeper    = true,
			};

			const Plan plan = classified(file.scene.objectPrograms, asked);
			surfaces += plan.kind == CellKind::Surface ? 1 : 0;
			if (hasPatchFace(plan)) {
				patched += 1;
				wrong += plan.leafKind == LeafKind::Held && unprovenFaces(file.scene.objectPrograms, asked) > 0 ? 0 : 1;
			}
		}
	}
	std::printf("tree classify: %d of %d surface cells of a smooth join keep a patch face, %d without an unproven face\n", patched, surfaces, wrong);
	check(patched > 0 && wrong == 0, "a smooth join keeps a patch face where its Taylor quadric is not proven");
}

void checkClassifyCrowded() {
	const SceneFile file = sceneOrExit(writeScene("crowd", R"(
		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		for i in 0..3 {
			for j in 0..3 {
				at([0.1 * i, 0.1 * j, 0]) box([0.06, 0.06, 0.06]);
			}
		}
	)"));

	Asked asked = {
		.box       = cubeAt({ -0.05, -0.05, -0.05 }, 0.3),
		.tolerance = 1e-4,
		.deeper    = true,
	};

	const Plan deeper = classified(file.scene.objectPrograms, asked);
	asked.deeper      = false;
	const Plan floor  = classified(file.scene.objectPrograms, asked);
	check(deeper.kind == CellKind::Branch, "a cell crowded with more faces than a leaf holds splits");
	check(floor.kind == CellKind::Surface && floor.patched && floor.leafKind == LeafKind::Floor, "a crowded cell at the depth cap is a floor leaf of one patch");
}

bool sameSurface(const SurfaceLeaf& a, const SurfaceLeaf& b) {
	return a.run == b.run && a.programs == b.programs && a.loosened == b.loosened && a.kind == b.kind;
}

struct PackPair {
	const Subtree& octree;
	const Subtree& packed;
};

int nodeMismatches(const PackPair& pair, std::uint32_t cell, const PackedNode& node);

struct NodeSlot {
	const PackedNode& node;
	int slot;
};

NodeSlot slotOf(const PackedNode& node, int slot) {
	return NodeSlot {
		.node = node,
		.slot = slot,
	};
}

int slotMismatches(const PackPair& pair, std::uint32_t cell, const NodeSlot& at) {
	const PackedNode& node = at.node;
	const int slot         = at.slot;
	const CellEntry& entry = pair.octree.cells[cell];
	const CellKind kind    = slotKind(node, slot);
	if (kind != entry.kind) {
		return 1;
	}
	switch (kind) {
		case CellKind::Surface: return sameSurface(pair.octree.surfaces[entry.payload], pair.packed.surfaces[slotPayload(node, slot)]) ? 0 : 1;
		case CellKind::Filled: return pair.octree.solids[entry.payload].programs == pair.packed.solids[slotPayload(node, slot)].programs ? 0 : 1;
		case CellKind::Branch: return nodeMismatches(pair, cell, pair.packed.nodes[slotPayload(node, slot)]);
		case CellKind::Empty: return 0;
	}

	return 1;
}

int coarseMismatches(const PackPair& pair, std::uint32_t child, const NodeSlot& at) {
	const PackedNode& node = at.node;
	const int octant       = at.slot;
	const bool coarse      = ((node.coarse >> octant) & 1) != 0;
	if (pair.octree.cells[child].kind != CellKind::Branch) {
		int padding = 0;
		for (int sub = 1; sub < 8; ++sub) {
			padding += slotKind(node, octant * 8 + sub) == CellKind::Empty ? 0 : 1;
		}
		return (coarse ? 0 : 1) + padding + slotMismatches(pair, child, slotOf(node, octant * 8));
	}
	int wrong = coarse ? 1 : 0;
	for (int sub = 0; sub < 8; ++sub) {
		wrong += slotMismatches(pair, pair.octree.cells[child].payload + std::uint32_t(sub), slotOf(node, octant * 8 + sub));
	}
	return wrong;
}

int nodeMismatches(const PackPair& pair, std::uint32_t cell, const PackedNode& node) {
	int wrong = 0;
	for (int octant = 0; octant < 8; ++octant) {
		wrong += coarseMismatches(pair, pair.octree.cells[cell].payload + std::uint32_t(octant), slotOf(node, octant));
		const bool busy = ((node.busy >> octant) & 1) != 0;
		const bool held = (((node.lowerKinds | node.upperKinds) >> (octant * 8)) & 0xFF) != 0;
		wrong += busy == held ? 0 : 1;
	}
	return wrong;
}

void checkPackRoundTrip() {
	const SceneFile file = sceneOrExit("scenes/chess-board.sdf");
	const Region region  = sceneRegion(file.scene, file.shot, 4);
	const Detail detail  = testDetail(file, region, defaultDetailMembers());

	const GrowContext context = {
		.scene    = file.scene,
		.detail   = detail,
		.depthCap = 7,
		.faces    = true,
	};

	const Subtree octree = growSubtree(context, rootSprout(context, region));
	const Subtree packed = packedSubtree(octree);

	const PackPair pair = {
		.octree = octree,
		.packed = packed,
	};

	const bool branch = packed.root.kind == CellKind::Branch && octree.cells[0].kind == CellKind::Branch;
	const int wrong   = branch ? nodeMismatches(pair, 0, packed.root.node) : 1;
	std::printf("tree pack: %zu octree cells, %zu packed nodes, %d leaves or bits apart\n", octree.cells.size(), packed.nodes.size() + 1, wrong);
	check(wrong == 0 && packed.surfaces.size() == octree.surfaces.size() && packed.solids.size() == octree.solids.size(), "a packed subtree holds every leaf of its octree at the slot its cell stands in");
}

bool sameNode(const PackedNode& a, const PackedNode& b) {
	return a.lowerKinds == b.lowerKinds && a.upperKinds == b.upperKinds && a.firstBranch == b.firstBranch && a.firstSurface == b.firstSurface && a.firstSolid == b.firstSolid && a.coarse == b.coarse && a.busy == b.busy;
}

template <typename T, typename Same>
bool sameRows(const std::vector<T>& a, const std::vector<T>& b, Same same) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same);
}

bool sameTrees(const Tree& a, const Tree& b) {
	const bool root     = a.root.kind == b.root.kind && a.root.payload == b.root.payload;
	const bool nodes    = sameRows(a.nodes, b.nodes, sameNode);
	const bool surfaces = sameRows(a.surfaces, b.surfaces, sameSurface);
	const bool solids   = sameRows(a.solids, b.solids, [](const SolidLeaf& x, const SolidLeaf& y) {
		return x.programs == y.programs;
	});

	const bool lists = sameRows(a.lists, b.lists, [](const ProgramList& x, const ProgramList& y) {
		return x.first == y.first && x.count == y.count;
	});

	const bool references = sameRows(a.references, b.references, [](const ProgramReference& x, const ProgramReference& y) {
		return x.index == y.index && x.source == y.source;
	});

	return root && nodes && surfaces && solids && lists && references && a.runs == b.runs;
}

void checkDeterministic() {
	SceneFile file          = sceneOrExit("scenes/magic-cube.sdf");
	file.shot.render.width  = 320;
	file.shot.render.height = 200;
	const TestTree one      = testTree(file, 1);
	const TestTree eight    = testTree(file, 8);
	std::printf("tree build: %zu nodes, %zu surface leaves on 1 and on 8 threads\n", one.tree.nodes.size(), one.tree.surfaces.size());
	check(one.tree.nodes.size() > 100 && sameTrees(one.tree, eight.tree), "the tree is the same to the bit on 1 and 8 threads");
}

Plan seamPlan(const std::vector<Program>& programs, const Box& box) {
	const CellPrograms cell = allObjects(programs);

	const CellQuery query = {
		.programs  = cell,
		.box       = box,
		.tolerance = 2e-4,
		.deeper    = true,
		.faces     = false,
	};

	return classifyCell(query);
}

bool isSeamPair(const Plan& plan) {
	const RunHeader header = runHeader(plan.run.empty() ? 0 : plan.run[0]);
	return plan.kind == CellKind::Surface && !plan.patched && plan.leafKind == LeafKind::Held && header.count == 2 && header.patches == 3 && plan.fitted.stray > 2e-4;
}

void checkSeamPair() {
	const Scene apart = sceneOrExit(writeScene("apart", R"(
		function sphere(r) {
			return length(p) - r;
		}

		at([-0.8, 0, 0]) sphere(1);
		at([0.8, 0, 0]) sphere(1);
	)"))
							.scene;

	const Scene joined = sceneOrExit(writeScene("joined", R"(
		function sphere(r) {
			return length(p) - r;
		}

		cut {
			at([-0.6, 0, 0]) sphere(1);
			at([0.6, 0, 0]) sphere(1);
		}

	)"))
							 .scene;

	const Box box     = cubeAt({ -0.05, 0.55, -0.05 }, 0.1);
	const Box cutAway = cubeAt({ -0.05, 0.75, -0.05 }, 0.1);
	const bool merged = isSeamPair(seamPlan(apart.objectPrograms, box));
	const bool shell  = joined.objectPrograms.size() == 1 && isSeamPair(seamPlan(joined.objectPrograms, cutAway));
	check(merged && shell, "a cell on the crease of a union of two objects, or of a cut inside one object, is a held pair of patches where one patch strays");
}

bool sameHit(const Hit& a, const Hit& b) {
	const bool where = a.distance == b.distance && a.point.x == b.point.x && a.point.y == b.point.y && a.point.z == b.point.z;
	const bool what  = a.normal.x == b.normal.x && a.normal.y == b.normal.y && a.normal.z == b.normal.z && a.leaf == b.leaf;
	return a.kind == b.kind && (a.kind == HitKind::Missed || (where && what && a.loosened == b.loosened && a.leafKind == b.leafKind));
}

Shared sharedHits(const SceneFile& file, const Tree& plain, const Tree& shared) {
	const Lens lens = lensOf(file.shot.camera, file.shot.render);

	const Geometry one = {
		.scene = file.scene,
		.tree  = plain,
	};

	const Geometry other = {
		.scene = file.scene,
		.tree  = shared,
	};

	Shared tally = {
		.differ = 0,
		.hits   = 0,
	};

	for (int y = 0; y < lens.height; ++y) {
		for (int x = 0; x < lens.width; ++x) {
			const Ray ray = pixelRay(lens, x, y);
			const Hit a   = intersect(one, ray);
			tally.differ += sameHit(a, intersect(other, ray)) ? 0 : 1;
			tally.hits += a.kind == HitKind::Missed ? 0 : 1;
		}
	}
	return tally;
}

Tree treeWith(const Passing& passing, const std::vector<PassMember>& passes) {
	const Passing asked = {
		.scene  = passing.scene,
		.detail = passing.detail,
		.passes = passes,
		.region = passing.region,
	};

	return passedTree(asked);
}

void checkSharedPasses() {
	SceneFile file          = sceneOrExit("scenes/magic-cube.sdf");
	file.shot.render.width  = 640;
	file.shot.render.height = 400;
	const Region region     = sceneRegion(file.scene, file.shot, 4);
	const Detail detail     = testDetail(file, region, defaultDetailMembers());

	const std::vector<PassMember> none = {};
	const std::vector<PassMember> runs = { PassMember::SharedCoefficients };
	const std::vector<PassMember> both = { PassMember::SharedCoefficients, PassMember::SharedSubtrees };

	const Passing passing = {
		.scene  = file.scene,
		.detail = detail,
		.passes = none,
		.region = region,
	};

	const Tree plain    = treeWith(passing, none);
	const double whole  = treeStats(plain).megabytes;
	const double shared = treeStats(treeWith(passing, runs)).megabytes;
	const Tree folded   = treeWith(passing, both);
	const double least  = treeStats(folded).megabytes;
	const Shared tally  = sharedHits(file, plain, folded);
	std::printf("tree shared: %.3f MB, %.3f with shared runs, %.3f with shared subtrees too; %zu of %zu pixel hits differ\n", whole, shared, least, tally.differ, tally.hits);
	check(tally.hits > 10000 && tally.differ == 0 && shared < 0.5 * whole && least < shared, "shared coefficients and subtrees keep every hit to the bit in less memory");
}

void checkCutPass() {
	SceneFile file          = sceneOrExit("scenes/chess-board.sdf");
	file.shot.render.width  = 320;
	file.shot.render.height = 200;
	const Region region     = sceneRegion(file.scene, file.shot, 4);
	const Detail detail     = testDetail(file, region, defaultDetailMembers());

	const std::vector<PassMember> none = {};
	const std::vector<PassMember> cut  = { PassMember::CutLongPrograms };

	const Passing passing = {
		.scene  = file.scene,
		.detail = detail,
		.passes = none,
		.region = region,
	};

	const Tree whole   = treeWith(passing, none);
	const Tree cuts    = treeWith(passing, cut);
	const Shared tally = sharedHits(file, whole, cuts);
	std::printf("cut programs: %zu cuts in %zu entries; %zu of %zu pixel hits differ\n", cuts.cuts.size(), cuts.cutSteps.size(), tally.differ, tally.hits);
	check(tally.hits > 10000 && tally.differ == 0 && !cuts.cuts.empty(), "cut programs keep every hit of the whole programs to the bit");
}

SceneFile hiddenBumpScene() {
	return sceneOrExit(writeScene("bump", R"(
		function sphere(r) {
			return length(p) - r;
		}

		function ground(height) {
			return p.z - height;
		}

		ground(0.49);
		at([0.75, 0.75, 0.6]) sphere(0.2);
		at([0.375, 0.125, 0.49]) sphere(0.02);
	)"));
}

Detail leastDetail(const SceneFile& file, const Region& region) {
	Detail detail = testDetail(file, region, {});
	detail.least  = 1e-3;
	return detail;
}

double fieldOfLeafAt(const Subtree& subtree, const Spot& p) {
	std::uint32_t cell = 0;
	Box box            = subtree.box;
	while (subtree.cells[cell].kind == CellKind::Branch) {
		int octant = 0;
		for (int axis = 0; axis < 3; ++axis) {
			octant |= p[std::size_t(axis)] >= (box.axes[axis].lower + box.axes[axis].upper) / 2 ? 1 << axis : 0;
		}
		cell = subtree.cells[cell].payload + std::uint32_t(octant);
		box  = octantBox(box, octant);
	}
	const CellEntry entry = subtree.cells[cell];
	if (entry.kind != CellKind::Surface) {
		return entry.kind == CellKind::Filled ? -1 : 1;
	}
	double u[3];
	for (int axis = 0; axis < 3; ++axis) {
		u[axis] = (p[std::size_t(axis)] - box.axes[axis].lower) / (box.axes[axis].upper - box.axes[axis].lower);
	}
	return fittedRunValue(subtree.runs.data() + subtree.surfaces[entry.payload].run, u);
}

void checkSplitBesideFiner() {
	const SceneFile file = hiddenBumpScene();

	const Region region = {
		.lower = { 0, 0, 0 },
		.size  = { 1, 1, 1 },
	};

	const Detail detail = leastDetail(file, region);

	const GrowContext context = {
		.scene    = file.scene,
		.detail   = detail,
		.depthCap = 8,
		.faces    = false,
	};

	const Sprout root      = rootSprout(context, region);
	const Subtree grown    = growSubtree(context, root);
	const Subtree balanced = splitBesideFiner(context, grown, *root.programs);
	const Spot inside      = { 0.375, 0.125, 0.495 };
	const math::Vector at  = { inside[0], inside[1], inside[2] };
	const double field     = cellField(file.scene.objectPrograms, at);
	const double before    = fieldOfLeafAt(grown, inside);
	const double after     = fieldOfLeafAt(balanced, inside);
	std::printf("tree beside finer: inside the small sphere the field is %.4f, the tree reads %.4f before and %.4f after (%zu and %zu cells)\n", field, before, after, grown.cells.size(), balanced.cells.size());
	check(field < 0 && before > 0 && after < 0, "a coarse patch beside finer cells that misses a small sphere between its points is split until it holds it");
}

CellKind plannedKind(const Scene& scene, const Detail& detail) {
	const Region region = {
		.lower = { 0, 0, 0 },
		.size  = { 0.5, 0.5, 0.5 },
	};

	const GrowContext context = {
		.scene    = scene,
		.detail   = detail,
		.depthCap = 8,
		.faces    = false,
	};

	return planCell(context, rootSprout(context, region)).kind;
}

void checkSplitHidden() {
	const SceneFile file = hiddenBumpScene();

	const Region unit = {
		.lower = { 0, 0, 0 },
		.size  = { 1, 1, 1 },
	};

	const Detail blind = leastDetail(file, unit);
	Detail seeing      = testDetail(file, unit, { DetailMember::SplitHiddenPrimitives });
	seeing.least       = blind.least;
	const CellKind off = plannedKind(file.scene, blind);
	const CellKind on  = plannedKind(file.scene, seeing);
	check(off == CellKind::Surface && on == CellKind::Branch, "a patch that holds a plane but misses a small sphere between its points splits where split_hidden_primitives is on");
}

void checkPilotTables() {
	const SceneFile file  = sceneOrExit("scenes/chess-board.sdf");
	const Region region   = sceneRegion(file.scene, file.shot, 4);
	const Detail detail   = testDetail(file, region, defaultDetailMembers());
	const double* t       = region.lower;
	const Spot place      = { t[0] + 0.5 * region.size[0], t[1] + 0.5 * region.size[1], t[2] + 0.5 * region.size[2] };
	const math::Vector at = { place[0], place[1], place[2] };
	const double floor    = footprintAt(detail.view, at);

	const Need need = {
		.place     = { place[0], place[1], place[2] },
		.footprint = 0.1 * floor,
		.direct    = false,
	};

	const std::vector<Need> needs = { need };

	const PilotInput input = {
		.region = region,
		.needs  = needs,
		.view   = detail.view,
	};

	const Pilot pilot    = pilotOf(input);
	const double small   = 4 * floor;
	const Box near       = cubeAt({ place[0] - small / 2, place[1] - small / 2, place[2] - small / 2 }, small);
	const Box far        = cubeAt({ t[0], t[1], t[2] }, small);
	const double held    = pilotLimit(pilot, near, 0.5);
	const double nowhere = pilotLimit(pilot, far, 0.5);
	check(std::abs(held - 0.05 * floor) < 1e-6 * floor && std::isinf(nowhere) && std::isinf(pilotLimit(unmeasuredPilot(region), near, 0.5)), "the pilot caps a cell about a measured need at its share of the need and no other");
}

void checkTree() {
	checkPilotTables();
	checkSharedPasses();
	checkCutPass();
	checkSplitBesideFiner();
	checkSplitHidden();
	checkClassifySphere();
	checkClassifyBlend();
	checkClassifyCrowded();
	checkSeamPair();
	checkPackRoundTrip();
	checkDeterministic();
	checkPatchFit();
	checkPatchSlab();
	checkFaceSlabs();
	checkFocusOnView();
	checkObjectPrograms("scenes/magic-cube.sdf");
	checkObjectPrograms("scenes/chess-board.sdf");
	checkCuts();
}

}
