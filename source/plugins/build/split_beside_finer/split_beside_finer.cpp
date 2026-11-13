#include "plugins/build/split_beside_finer/split_beside_finer.h"
#include "plugins/geometry/tree/classify.h"
#include "plugins/geometry/tree/detail.h"
#include "plugins/geometry/tree/quadric.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace kodanuki {

constexpr int mostRounds     = 32;
constexpr double nudgeShare  = 1e-3;
constexpr int faceSteps      = 5;
constexpr int innerSteps     = 4;
constexpr std::uint8_t holds = 1;

using Unit = std::array<double, 3>;

struct Reached {
	std::uint32_t cell;
	Box box;
	int depth;
	std::vector<int> path;
};

struct Balancing {
	const GrowContext& context;
	const CellPrograms& programs;
	Subtree& split;
};

struct PathCuts {
	std::vector<int> path;
	std::vector<CellPrograms> programs;
};

struct Chosen {
	Reached leaf;
	CellPrograms programs;
};

void visitLeaves(const Subtree& subtree, const Reached& at, std::vector<Reached>& leaves) {
	const CellEntry& entry = subtree.cells[at.cell];
	if (entry.kind != CellKind::Branch) {
		leaves.push_back(at);
		return;
	}
	for (int octant = 0; octant < 8; ++octant) {
		Reached child = {
			.cell  = entry.payload + std::uint32_t(octant),
			.box   = octantBox(at.box, octant),
			.depth = at.depth + 1,
			.path  = at.path,
		};

		child.path.push_back(octant);
		visitLeaves(subtree, child, leaves);
	}
}

std::vector<Reached> subtreeLeaves(const Subtree& subtree) {
	const Reached root = {
		.cell  = 0,
		.box   = subtree.box,
		.depth = subtree.depth,
		.path  = {},
	};

	std::vector<Reached> leaves;
	visitLeaves(subtree, root, leaves);
	return leaves;
}

int octantAt(const Box& box, const double* p) {
	int octant = 0;
	for (int axis = 0; axis < 3; ++axis) {
		octant |= p[axis] >= boxCentre(box, axis) ? 1 << axis : 0;
	}
	return octant;
}

int depthAt(const Subtree& subtree, const double* p) {
	std::uint32_t cell = 0;
	Box box            = subtree.box;
	int depth          = subtree.depth;
	while (subtree.cells[cell].kind == CellKind::Branch) {
		const int octant = octantAt(box, p);
		cell             = subtree.cells[cell].payload + std::uint32_t(octant);
		box              = octantBox(box, octant);
		depth += 1;
	}
	return depth;
}

bool insideBox(const Box& box, const double* p) {
	for (int axis = 0; axis < 3; ++axis) {
		if (p[axis] < box.axes[axis].lower || p[axis] > box.axes[axis].upper) {
			return false;
		}
	}
	return true;
}

double nudgeOf(const Box& box) {
	return std::min({ boxHalf(box, 0), boxHalf(box, 1), boxHalf(box, 2) }) * nudgeShare;
}

Unit probeOf(const Box& box, int face, int offset) {
	const double offsets[5][2] = { { 0, 0 }, { -0.5, -0.5 }, { 0.5, -0.5 }, { -0.5, 0.5 }, { 0.5, 0.5 } };
	const int axis             = face / 2;
	const double side          = face % 2 == 0 ? -1 : 1;
	Unit probe                 = { boxCentre(box, 0), boxCentre(box, 1), boxCentre(box, 2) };
	probe[std::size_t(axis)] += side * (boxHalf(box, axis) + nudgeOf(box));
	probe[std::size_t((axis + 1) % 3)] += offsets[offset][0] * boxHalf(box, (axis + 1) % 3);
	probe[std::size_t((axis + 2) % 3)] += offsets[offset][1] * boxHalf(box, (axis + 2) % 3);
	return probe;
}

bool crowded(const Subtree& subtree, const Reached& leaf) {
	for (int face = 0; face < 6; ++face) {
		for (int offset = 0; offset < 5; ++offset) {
			const Unit probe = probeOf(leaf.box, face, offset);
			if (insideBox(subtree.box, probe.data()) && depthAt(subtree, probe.data()) > leaf.depth + 1) {
				return true;
			}
		}
	}
	return false;
}

std::vector<Unit> computedChecks() {
	std::vector<Unit> units;
	const double last = faceSteps - 1;
	for (int k = 0; k < faceSteps * faceSteps * faceSteps; ++k) {
		const int x     = k % faceSteps;
		const int y     = k / faceSteps % faceSteps;
		const int z     = k / (faceSteps * faceSteps);
		const Unit unit = { x / last, y / last, z / last };
		if (x == 0 || y == 0 || z == 0 || x == faceSteps - 1 || y == faceSteps - 1 || z == faceSteps - 1) {
			units.push_back(unit);
		}
	}
	for (int k = 0; k < innerSteps * innerSteps * innerSteps; ++k) {
		const Unit unit = { (k % innerSteps + 0.5) / innerSteps, (k / innerSteps % innerSteps + 0.5) / innerSteps, (k / (innerSteps * innerSteps) + 0.5) / innerSteps };
		units.push_back(unit);
	}
	return units;
}

const std::vector<Unit>& checkUnits() {
	static const std::vector<Unit> units = computedChecks();
	return units;
}

std::size_t sharedOctants(const std::vector<int>& a, const std::vector<int>& b) {
	std::size_t shared = 0;
	while (shared < a.size() && shared < b.size() && a[shared] == b[shared]) {
		shared += 1;
	}
	return shared;
}

const CellPrograms& programsAlong(const Balancing& balancing, const Reached& leaf, PathCuts& cuts) {
	Box box = balancing.split.box;
	if (cuts.programs.empty()) {
		cuts.programs.push_back(cutCell(balancing.programs, box).programs);
	}
	const std::size_t shared = sharedOctants(cuts.path, leaf.path);
	cuts.path.resize(shared);
	cuts.programs.resize(shared + 1);
	for (std::size_t k = 0; k < leaf.path.size(); ++k) {
		box = octantBox(box, leaf.path[k]);
		if (k >= shared) {
			cuts.programs.push_back(cutCell(cuts.programs.back(), box).programs);
			cuts.path.push_back(leaf.path[k]);
		}
	}
	return cuts.programs.back();
}

bool strays(const Balancing& balancing, const Reached& leaf, const CellPrograms& programs) {
	const double tolerance   = cellTolerance(balancing.context.detail, leaf.box);
	const std::uint32_t* run = balancing.split.runs.data() + balancing.split.surfaces[balancing.split.cells[leaf.cell].payload].run;
	for (const Unit& u : checkUnits()) {
		const math::Vector p = { leaf.box.axes[0].lower + 2 * boxHalf(leaf.box, 0) * u[0], leaf.box.axes[1].lower + 2 * boxHalf(leaf.box, 1) * u[1], leaf.box.axes[2].lower + 2 * boxHalf(leaf.box, 2) * u[2] };
		if (std::abs(cellProgramsField(programs, p) - fittedRunValue(run, u.data())) > tolerance) {
			return true;
		}
	}
	return false;
}

bool splittable(const Balancing& balancing, const Reached& leaf) {
	const CellEntry& entry = balancing.split.cells[leaf.cell];
	return entry.kind == CellKind::Surface && leaf.depth < balancing.context.depthCap && balancing.split.wholeFitted[entry.payload];
}

std::vector<Chosen> chosenLeaves(const Balancing& balancing, std::vector<std::uint8_t>& known) {
	known.resize(balancing.split.cells.size(), 0);
	std::vector<Chosen> chosen;

	PathCuts cuts = {
		.path     = {},
		.programs = {},
	};

	for (const Reached& leaf : subtreeLeaves(balancing.split)) {
		if (known[leaf.cell] == holds || !splittable(balancing, leaf) || !crowded(balancing.split, leaf)) {
			continue;
		}
		const CellPrograms& programs = programsAlong(balancing, leaf, cuts);
		known[leaf.cell]             = strays(balancing, leaf, programs) ? 0 : holds;
		if (known[leaf.cell] != holds) {
			const Chosen picked = {
				.leaf     = leaf,
				.programs = programs,
			};

			chosen.push_back(picked);
		}
	}
	return chosen;
}

void splitLeaf(const Balancing& balancing, const Chosen& chosen) {
	const std::uint32_t first                         = openBranch(balancing.split, chosen.leaf.cell);
	const std::shared_ptr<const CellPrograms> parents = std::make_shared<const CellPrograms>(chosen.programs);
	for (int octant = 0; octant < 8; ++octant) {
		const Sprout sprout = {
			.box      = octantBox(chosen.leaf.box, octant),
			.depth    = chosen.leaf.depth + 1,
			.programs = parents,
		};

		const Regrowth regrowth = {
			.sprout = sprout,
			.cell   = first + std::uint32_t(octant),
		};

		regrowCell(balancing.context, regrowth, balancing.split);
	}
}

void keptLiveRuns(Subtree& split) {
	std::vector<std::uint32_t> runs;
	for (const Reached& leaf : subtreeLeaves(split)) {
		const CellEntry& entry = split.cells[leaf.cell];
		if (entry.kind == CellKind::Surface) {
			SurfaceLeaf& surface       = split.surfaces[entry.payload];
			const std::uint32_t* words = split.runs.data() + surface.run;
			surface.run                = std::uint32_t(runs.size());
			runs.insert(runs.end(), words, words + faceOffsets(words).back());
		}
	}
	split.runs = runs;
}

Subtree splitBesideFiner(const GrowContext& context, const Subtree& subtree, const CellPrograms& programs) {
	Subtree split = subtree;

	const Balancing balancing = {
		.context  = context,
		.programs = programs,
		.split    = split,
	};

	std::vector<std::uint8_t> known;
	bool changed = false;
	for (int round = 0; round < mostRounds; ++round) {
		const std::vector<Chosen> chosen = chosenLeaves(balancing, known);
		if (chosen.empty()) {
			break;
		}
		for (const Chosen& leaf : chosen) {
			splitLeaf(balancing, leaf);
		}
		changed = true;
	}
	if (changed) {
		keptLiveRuns(split);
	}
	return split;
}

}
