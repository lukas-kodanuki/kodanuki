#include "plugins/geometry/tree/grow.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace kodanuki {

constexpr std::size_t recentLists = 8;

struct Place {
	const CellPrograms& programs;
	Box box;
	int depth;
	std::uint32_t cell;
};

RootRecord blankRecord() {
	const PackedNode node = {
		.lowerKinds   = 0,
		.upperKinds   = 0,
		.firstBranch  = 0,
		.firstSurface = 0,
		.firstSolid   = 0,
		.coarse       = 0,
		.busy         = 0,
	};

	const SurfaceLeaf surface = {
		.run      = 0,
		.programs = 0,
		.loosened = 0,
		.kind     = LeafKind::Held,
	};

	const SolidLeaf solid = {
		.programs = 0,
	};

	return RootRecord {
		.kind    = CellKind::Empty,
		.node    = node,
		.surface = surface,
		.solid   = solid,
	};
}

Subtree emptySubtree(const Sprout& sprout) {
	const CellEntry root = {
		.kind    = CellKind::Empty,
		.payload = 0,
	};

	return Subtree {
		.box         = sprout.box,
		.depth       = sprout.depth,
		.cells       = { root },
		.surfaces    = {},
		.solids      = {},
		.runs        = {},
		.lists       = {},
		.references  = {},
		.cuts        = {},
		.cutSteps    = {},
		.wholeFitted = {},
		.nodes       = {},
		.root        = blankRecord(),
	};
}

Plan planAt(const GrowContext& context, const Place& place) {
	const double tolerance = cellTolerance(context.detail, place.box);
	const bool deeper      = place.depth < context.depthCap;

	const CellQuery query = {
		.programs  = place.programs,
		.box       = place.box,
		.tolerance = tolerance,
		.deeper    = deeper,
		.faces     = context.faces,
	};

	Plan plan     = classifyCell(query);
	plan.loosened = tolerance;

	const DetailQuery asked = {
		.question  = DetailQuestion::Verdict,
		.box       = place.box,
		.plan      = plan,
		.tolerance = tolerance,
		.deeper    = deeper,
	};

	plan.kind = plan.kind != CellKind::Branch && cellVerdict(context.detail, asked) == Verdict::Split ? CellKind::Branch : plan.kind;
	plan.run  = plan.kind == CellKind::Surface ? foldedRun(plan.run) : plan.run;
	return plan;
}

Plan planCell(const GrowContext& context, const Sprout& sprout) {
	const Place place = {
		.programs = *sprout.programs,
		.box      = sprout.box,
		.depth    = sprout.depth,
		.cell     = 0,
	};

	return planAt(context, place);
}

std::array<Sprout, 8> childSprouts(const Sprout& sprout, const std::shared_ptr<const CellPrograms>& programs) {
	std::array<Sprout, 8> children;
	for (int octant = 0; octant < 8; ++octant) {
		children[std::size_t(octant)] = Sprout {
			.box      = octantBox(sprout.box, octant),
			.depth    = sprout.depth + 1,
			.programs = programs,
		};
	}
	return children;
}

bool sameObjects(const Subtree& subtree, const ProgramList& list, const std::vector<std::uint32_t>& objects) {
	if (list.count != objects.size()) {
		return false;
	}
	for (std::uint32_t k = 0; k < list.count; ++k) {
		if (subtree.references[list.first + k].index != objects[k]) {
			return false;
		}
	}
	return true;
}

std::uint32_t listOf(Subtree& subtree, const std::vector<std::uint32_t>& objects) {
	const std::size_t count = subtree.lists.size();
	for (std::size_t back = 1; back <= std::min(count, recentLists); ++back) {
		if (sameObjects(subtree, subtree.lists[count - back], objects)) {
			return std::uint32_t(count - back);
		}
	}

	const ProgramList list = {
		.first = std::uint32_t(subtree.references.size()),
		.count = std::uint32_t(objects.size()),
	};

	for (const std::uint32_t object : objects) {
		const ProgramReference reference = {
			.index  = object,
			.source = ProgramSource::Object,
		};

		subtree.references.push_back(reference);
	}
	subtree.lists.push_back(list);
	return std::uint32_t(count);
}

CellEntry entryOf(CellKind kind, std::size_t payload) {
	return CellEntry {
		.kind    = kind,
		.payload = std::uint32_t(payload),
	};
}

void writeSurface(Subtree& subtree, std::uint32_t cell, const Plan& plan) {
	const SurfaceLeaf leaf = {
		.run      = std::uint32_t(subtree.runs.size()),
		.programs = listOf(subtree, plan.programs.objects),
		.loosened = loosening(plan.loosened),
		.kind     = plan.leafKind,
	};

	subtree.cells[cell] = entryOf(CellKind::Surface, subtree.surfaces.size());
	subtree.surfaces.push_back(leaf);
	subtree.wholeFitted.push_back(plan.patched || plan.seamed);
	subtree.runs.insert(subtree.runs.end(), plan.run.begin(), plan.run.end());
}

void writeSolid(Subtree& subtree, std::uint32_t cell, const Plan& plan) {
	const SolidLeaf leaf = {
		.programs = listOf(subtree, plan.programs.objects),
	};

	subtree.cells[cell] = entryOf(CellKind::Filled, subtree.solids.size());
	subtree.solids.push_back(leaf);
}

void writeLeaf(Subtree& subtree, std::uint32_t cell, const Plan& plan) {
	switch (plan.kind) {
		case CellKind::Surface: writeSurface(subtree, cell, plan); return;
		case CellKind::Filled: writeSolid(subtree, cell, plan); return;
		case CellKind::Empty:
		case CellKind::Branch: subtree.cells[cell] = entryOf(CellKind::Empty, 0); return;
	}
}

std::uint32_t openBranch(Subtree& subtree, std::uint32_t cell) {
	const std::uint32_t first = std::uint32_t(subtree.cells.size());
	subtree.cells.resize(subtree.cells.size() + 8, entryOf(CellKind::Empty, 0));
	subtree.cells[cell] = entryOf(CellKind::Branch, first);
	return first;
}

void growCell(const GrowContext& context, const Place& place, Subtree& subtree) {
	const Plan plan = planAt(context, place);
	if (plan.kind != CellKind::Branch) {
		writeLeaf(subtree, place.cell, plan);
		return;
	}
	const std::uint32_t first = openBranch(subtree, place.cell);
	for (int octant = 0; octant < 8; ++octant) {
		const Place child = {
			.programs = plan.programs,
			.box      = octantBox(place.box, octant),
			.depth    = place.depth + 1,
			.cell     = first + std::uint32_t(octant),
		};

		growCell(context, child, subtree);
	}
}

Subtree growSubtree(const GrowContext& context, const Sprout& sprout) {
	Subtree subtree = emptySubtree(sprout);

	const Place place = {
		.programs = *sprout.programs,
		.box      = sprout.box,
		.depth    = sprout.depth,
		.cell     = 0,
	};

	growCell(context, place, subtree);
	return subtree;
}

void regrowCell(const GrowContext& context, const Regrowth& regrowth, Subtree& subtree) {
	const Place place = {
		.programs = *regrowth.sprout.programs,
		.box      = regrowth.sprout.box,
		.depth    = regrowth.sprout.depth,
		.cell     = regrowth.cell,
	};

	growCell(context, place, subtree);
}

Sprout rootSprout(const GrowContext& context, const Region& region) {
	return Sprout {
		.box      = regionBox(region),
		.depth    = 0,
		.programs = std::make_shared<const CellPrograms>(rootPrograms(context.scene)),
	};
}

}
