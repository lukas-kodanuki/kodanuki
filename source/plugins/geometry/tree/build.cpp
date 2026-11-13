#include "plugins/geometry/tree/build.h"
#include "plugins/geometry/tree/cut.h"
#include "sdf/numbers.h"
#include "sdf/scene.h"
#include "shared/parallel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace kodanuki {

constexpr int regionDepth        = 6;
constexpr int sharedDepth        = 3;
constexpr int regionPasses       = 12;
constexpr double regionShrink    = 0.999;
constexpr double regionMargin    = 0.01;
constexpr std::size_t seedCount  = 128;
constexpr std::size_t seedBytes  = std::size_t(256) << 20;
constexpr double floorTolerances = 4;
constexpr std::size_t leastSlots = 1024;
constexpr std::size_t trimEvery  = 64;
constexpr int mappedBlock        = 1 << 20;
constexpr std::size_t cutChunk   = 256;

using Programs = std::vector<std::shared_ptr<const Program>>;

struct SharedCuts {
	std::map<std::tuple<int, double, double, double>, Programs> cuts;
	std::mutex lock;
};

struct Search {
	int axis;
	bool upper;
	SharedCuts& shared;
};

struct Reach {
	const std::vector<std::shared_ptr<const Program>>& programs;
	Box box;
	int depth;
};

struct Bounds {
	double lower[3];
	double upper[3];
};

struct Shoot {
	Sprout sprout;
	std::uint32_t cell;
};

struct Planned {
	Plan plan;
	std::shared_ptr<const CellPrograms> programs;
};

struct Ordered {
	std::vector<std::optional<Subtree>> finished;
	std::size_t placed;
	std::size_t done;
	std::mutex lock;
};

double nearKey(const Search& search, const Box& box) {
	const math::Interval& span = box.axes[search.axis];
	return search.upper ? -span.upper : span.lower;
}

std::shared_ptr<const Program> keptCut(const std::shared_ptr<const Program>& program, const Cut& cut) {
	if (sameProgram(cut.program, *program)) {
		return program;
	}
	return std::make_shared<const Program>(cut.program);
}

std::vector<std::shared_ptr<const Program>> occupiedCuts(const std::vector<std::shared_ptr<const Program>>& programs, const Box& box) {
	std::vector<std::shared_ptr<const Program>> kept;
	for (const std::shared_ptr<const Program>& program : programs) {
		const Cut cut = occupiedCut(*program, box);
		if (cut.occupancy != Occupancy::Empty) {
			kept.push_back(keptCut(program, cut));
		}
	}
	return kept;
}

std::array<Box, 8> octantsByKey(const Search& search, const Box& box) {
	std::array<Box, 8> octants = { octantBox(box, 0), octantBox(box, 1), octantBox(box, 2), octantBox(box, 3), octantBox(box, 4), octantBox(box, 5), octantBox(box, 6), octantBox(box, 7) };
	std::stable_sort(octants.begin(), octants.end(), [&](const Box& a, const Box& b) {
		return nearKey(search, a) < nearKey(search, b);
	});

	return octants;
}

double extremeIn(const Search& search, const Reach& reach, double best);

double extremeBelow(const Search& search, const Reach& cut, double best) {
	if (cut.depth == regionDepth) {
		return nearKey(search, cut.box);
	}
	double found = best;
	for (const Box& box : octantsByKey(search, cut.box)) {
		const Reach child = {
			.programs = cut.programs,
			.box      = box,
			.depth    = cut.depth + 1,
		};

		found = extremeIn(search, child, found);
	}
	return found;
}

std::tuple<int, double, double, double> reachKey(const Reach& reach) {
	return { reach.depth, reach.box.axes[0].lower, reach.box.axes[1].lower, reach.box.axes[2].lower };
}

const Programs* foundCuts(SharedCuts& shared, const std::tuple<int, double, double, double>& key) {
	const std::lock_guard<std::mutex> guard(shared.lock);
	const auto found = shared.cuts.find(key);
	return found == shared.cuts.end() ? nullptr : &found->second;
}

const Programs& sharedCuts(SharedCuts& shared, const Reach& reach) {
	const std::tuple<int, double, double, double> key = reachKey(reach);
	const Programs* found                             = foundCuts(shared, key);
	if (found) {
		return *found;
	}
	Programs cuts = occupiedCuts(reach.programs, reach.box);
	const std::lock_guard<std::mutex> guard(shared.lock);
	return shared.cuts.emplace(key, std::move(cuts)).first->second;
}

double extremeWithin(const Search& search, const Reach& cut, double best) {
	if (cut.programs.empty()) {
		return best;
	}
	return extremeBelow(search, cut, best);
}

double extremeIn(const Search& search, const Reach& reach, double best) {
	if (nearKey(search, reach.box) >= best) {
		return best;
	}
	const Programs own   = reach.depth <= sharedDepth ? Programs() : occupiedCuts(reach.programs, reach.box);
	const Programs& cuts = reach.depth <= sharedDepth ? sharedCuts(search.shared, reach) : own;

	const Reach cut = {
		.programs = cuts,
		.box      = reach.box,
		.depth    = reach.depth,
	};

	return extremeWithin(search, cut, best);
}

math::Interval reachSpan(const Shot& shot, int axis) {
	const double centre = math::component(shot.camera.target, axis);
	return math::Interval {
		.lower = centre - shot.render.far,
		.upper = centre + shot.render.far,
	};
}

Box reachCube(const Shot& shot) {
	return Box {
		.axes = { reachSpan(shot, 0), reachSpan(shot, 1), reachSpan(shot, 2) },
	};
}

Box boundsBox(const Bounds& bounds) {
	Box box = {
		.axes = {},
	};

	for (int axis = 0; axis < 3; ++axis) {
		box.axes[axis] = math::Interval {
			.lower = bounds.lower[axis],
			.upper = bounds.upper[axis],
		};
	}
	return box;
}

struct Lead {
	int side;
	Box parent;
	Box box;
};

struct Leading {
	const Reach& whole;
	SharedCuts& shared;
	std::array<std::atomic<double>, 6>& bests;
};

Search sideSearch(int side, SharedCuts& shared) {
	return Search {
		.axis   = side % 3,
		.upper  = side >= 3,
		.shared = shared,
	};
}

std::vector<Lead> searchLeads(const Box& box, SharedCuts& shared) {
	std::array<std::vector<Lead>, 6> sides;
	for (int side = 0; side < 6; ++side) {
		const Search search = sideSearch(side, shared);
		for (const Box& parent : octantsByKey(search, box)) {
			for (const Box& child : octantsByKey(search, parent)) {
				const Lead lead = {
					.side   = side,
					.parent = parent,
					.box    = child,
				};

				sides[std::size_t(side)].push_back(lead);
			}
		}
	}
	std::vector<Lead> leads;
	for (std::size_t rank = 0; rank < 64; ++rank) {
		for (const std::vector<Lead>& side : sides) {
			leads.push_back(side[rank]);
		}
	}
	return leads;
}

void lowered(std::atomic<double>& best, double found) {
	double held = best.load();
	bool lower  = found < held;
	while (lower) {
		lower = !best.compare_exchange_weak(held, found) && found < held;
	}
}

void searchLead(const Leading& leading, const Lead& lead) {
	const Search search       = sideSearch(lead.side, leading.shared);
	std::atomic<double>& best = leading.bests[std::size_t(lead.side)];
	if (nearKey(search, lead.parent) >= best.load()) {
		return;
	}

	const Reach parent = {
		.programs = leading.whole.programs,
		.box      = lead.parent,
		.depth    = 1,
	};

	const Programs& cuts = sharedCuts(leading.shared, parent);
	if (cuts.empty()) {
		return;
	}

	const Reach child = {
		.programs = cuts,
		.box      = lead.box,
		.depth    = 2,
	};

	lowered(best, extremeIn(search, child, best.load()));
}

Programs occupiedCutsAmong(const Programs& programs, const Box& box, int workers) {
	const std::size_t chunks = (programs.size() + cutChunk - 1) / cutChunk;
	std::vector<Programs> kept(chunks);
	inParallel(workers, chunks, [&](std::size_t k) {
		const auto first = programs.begin() + std::ptrdiff_t(k * cutChunk);
		const auto last  = programs.begin() + std::ptrdiff_t(std::min(programs.size(), (k + 1) * cutChunk));
		kept[k]          = occupiedCuts(Programs(first, last), box);
	});

	Programs cuts;
	for (const Programs& chunk : kept) {
		cuts.insert(cuts.end(), chunk.begin(), chunk.end());
	}
	return cuts;
}

void sharedOctants(const Reach& whole, SharedCuts& shared, int workers) {
	std::array<Programs, 8> octants;
	inParallel(workers, 8, [&](std::size_t k) {
		octants[k] = occupiedCuts(whole.programs, octantBox(whole.box, int(k)));
	});

	for (std::size_t k = 0; k < 8; ++k) {
		const Reach octant = {
			.programs = whole.programs,
			.box      = octantBox(whole.box, int(k)),
			.depth    = 1,
		};

		shared.cuts.emplace(reachKey(octant), std::move(octants[k]));
	}
}

Bounds boundsWithin(const std::vector<std::shared_ptr<const Program>>& programs, const Box& box, int workers) {
	const std::vector<std::shared_ptr<const Program>> cuts = occupiedCutsAmong(programs, box, workers);

	const Reach whole = {
		.programs = cuts,
		.box      = box,
		.depth    = 0,
	};

	SharedCuts shared = {
		.cuts = {},
		.lock = {},
	};

	const double none = std::numeric_limits<double>::infinity();
	std::array<std::atomic<double>, 6> bests;
	for (std::atomic<double>& best : bests) {
		best.store(none);
	}

	const Leading leading = {
		.whole  = whole,
		.shared = shared,
		.bests  = bests,
	};

	sharedOctants(whole, shared, workers);
	const std::vector<Lead> leads = cuts.empty() ? std::vector<Lead>() : searchLeads(box, shared);
	inParallel(workers, leads.size(), [&](std::size_t i) {
		searchLead(leading, leads[i]);
	});

	std::array<double, 6> extremes = {};
	for (int side = 0; side < 6; ++side) {
		const double key            = bests[std::size_t(side)].load();
		extremes[std::size_t(side)] = side >= 3 ? -key : key;
	}
	return Bounds {
		.lower = { extremes[0], extremes[1], extremes[2] },
		.upper = { extremes[3], extremes[4], extremes[5] },
	};
}

double boxVolume(const Box& box) {
	double volume = 1;
	for (const math::Interval& span : box.axes) {
		volume *= span.upper - span.lower;
	}
	return volume;
}

Bounds occupiedBounds(const Shot& shot, const std::vector<std::shared_ptr<const Program>>& programs, int workers) {
	Bounds bounds = boundsWithin(programs, reachCube(shot), workers);
	for (int pass = 1; pass < regionPasses && std::isfinite(bounds.lower[0]); ++pass) {
		const Box box        = boundsBox(bounds);
		const Bounds tighter = boundsWithin(programs, box, workers);
		const bool shrunk    = boxVolume(boundsBox(tighter)) < regionShrink * boxVolume(box);
		bounds               = tighter;
		if (!shrunk) {
			break;
		}
	}
	return bounds;
}

Region regionOf(const Shot& shot, const Bounds& bounds) {
	if (!std::isfinite(bounds.lower[0])) {
		return Region {
			.lower = { shot.camera.target.x - 0.5, shot.camera.target.y - 0.5, shot.camera.target.z - 0.5 },
			.size  = { 1, 1, 1 },
		};
	}
	const double extent = std::max({ bounds.upper[0] - bounds.lower[0], bounds.upper[1] - bounds.lower[1], bounds.upper[2] - bounds.lower[2] });
	const double size   = extent * (1 + 2 * regionMargin);

	Region region = {
		.lower = { 0, 0, 0 },
		.size  = { size, size, size },
	};

	for (int axis = 0; axis < 3; ++axis) {
		region.lower[axis] = (bounds.lower[axis] + bounds.upper[axis]) / 2 - size / 2;
	}
	return region;
}

Region sceneRegion(const Scene& scene, const Shot& shot, int workers) {
	std::vector<std::shared_ptr<const Program>> programs;
	for (const Program& program : scene.objectPrograms) {
		programs.push_back(borrowedProgram(program));
	}
	return regionOf(shot, occupiedBounds(shot, programs, std::max(1, workers)));
}

int depthCapOf(const Region& region, double least) {
	const double longest = std::max({ region.size[0], region.size[1], region.size[2] });
	return std::max(0, int(std::nearbyint(std::log2(longest / (floorTolerances * least)))));
}

Planned plannedCell(const BuildContext& context, const Sprout& sprout) {
	Plan plan = planCell(context.grow, sprout);
	if (plan.kind != CellKind::Branch) {
		plan.programs.programs = {};
		return Planned {
			.plan     = plan,
			.programs = nullptr,
		};
	}
	const std::shared_ptr<const CellPrograms> programs = std::make_shared<const CellPrograms>(std::move(plan.programs));
	plan.programs                                      = {};
	return Planned {
		.plan     = plan,
		.programs = programs,
	};
}

std::vector<Planned> plannedLevel(const BuildContext& context, const std::vector<Shoot>& level) {
	const Planned pending = {
		.plan     = pendingPlan(),
		.programs = nullptr,
	};

	std::vector<Planned> plans(level.size(), pending);
	inParallel(context.workers, level.size(), [&](std::size_t i) {
		plans[i] = plannedCell(context, level[i].sprout);
	});

	return plans;
}

std::vector<Shoot> grownLevel(BuildContext& context, const std::vector<Shoot>& level) {
	const std::vector<Planned> plans = plannedLevel(context, level);
	std::vector<Shoot> next;
	for (std::size_t i = 0; i < level.size(); ++i) {
		if (plans[i].plan.kind != CellKind::Branch) {
			writeLeaf(context.top, level[i].cell, plans[i].plan);
			continue;
		}
		const std::uint32_t first            = openBranch(context.top, level[i].cell);
		const std::array<Sprout, 8> children = childSprouts(level[i].sprout, plans[i].programs);
		for (std::uint32_t k = 0; k < 8; ++k) {
			const Shoot child = {
				.sprout = children[k],
				.cell   = first + k,
			};

			next.push_back(child);
		}
	}
	return next;
}

CellEntry cellAt(CellKind kind, std::size_t payload) {
	return CellEntry {
		.kind    = kind,
		.payload = std::uint32_t(payload),
	};
}

std::size_t programBytes(const CellPrograms& programs) {
	std::size_t bytes = 0;
	for (const std::shared_ptr<const Program>& program : programs.programs) {
		bytes += program->steps.size() * sizeof(Step);
	}
	return bytes;
}

std::size_t parentBytes(const std::vector<Shoot>& level) {
	std::size_t bytes            = 0;
	const CellPrograms* previous = nullptr;
	for (const Shoot& shoot : level) {
		const CellPrograms* parent = shoot.sprout.programs.get();
		bytes += parent != previous ? programBytes(*parent) : 0;
		previous = parent;
	}
	return bytes;
}

bool seedsReady(const std::vector<Shoot>& level, int depth) {
	if (level.empty()) {
		return true;
	}
	return depth % 2 == 0 && (level.size() >= seedCount || parentBytes(level) > seedBytes);
}

void growTop(BuildContext& context, const Sprout& root) {
	const Shoot first = {
		.sprout = root,
		.cell   = 0,
	};

	std::vector<Shoot> level = { first };
	int depth                = 0;
	for (; !seedsReady(level, depth); ++depth) {
		level = grownLevel(context, level);
	}
	for (std::size_t k = 0; k < level.size(); ++k) {
		context.top.cells[level[k].cell] = cellAt(CellKind::Branch, k);
		context.seeds.push_back(level[k].sprout);
	}
	context.seedDepth = depth;
}

ProgramTable emptyProgramTable() {
	return ProgramTable {
		.lists = std::vector<std::uint32_t>(leastSlots, 0),
		.cuts  = std::vector<std::uint32_t>(leastSlots, 0),
		.taken = { 0, 0 },
	};
}

BuildContext buildContext(const BuildInput& input) {
	const GrowContext grow = {
		.scene    = input.scene,
		.detail   = input.detail,
		.depthCap = input.depthCap,
		.faces    = input.faces,
	};

	const Sprout root = rootSprout(grow, input.region);

	BuildContext context = {
		.grow      = grow,
		.passes    = input.passes,
		.state     = emptyPassState(),
		.programs  = emptyProgramTable(),
		.top       = emptySubtree(root),
		.seeds     = {},
		.seedDepth = 0,
		.roots     = {},
		.region    = input.region,
		.workers   = std::max(1, input.workers),
	};

	growTop(context, root);
	return context;
}

struct Moment {
	const std::vector<PassMember>& passes;
	PassMoment moment;
};

Subtree passedSubtree(const Moment& moment, const PassQuery& query, PassState& state) {
	Subtree passed = query.subtree;
	for (const PassMember member : moment.passes) {
		if (passMoment(member) != moment.moment) {
			continue;
		}

		const PassQuery asked = {
			.context  = query.context,
			.subtree  = passed,
			.programs = query.programs,
			.tree     = query.tree,
		};

		passed = passSubtree(member, asked, state);
	}
	return passed;
}

Tree emptyTree(const Region& region) {
	const CellEntry root = {
		.kind    = CellKind::Empty,
		.payload = 0,
	};

	return Tree {
		.region     = region,
		.root       = root,
		.nodes      = {},
		.surfaces   = {},
		.solids     = {},
		.runs       = {},
		.lists      = {},
		.references = {},
		.cuts       = {},
		.cutSteps   = {},
	};
}

Subtree finishedSubtree(const BuildContext& context, const Sprout& seed) {
	const Subtree grown = growSubtree(context.grow, seed);
	const Tree none     = emptyTree(context.region);
	PassState unused    = emptyPassState();

	const PassQuery query = {
		.context  = context.grow,
		.subtree  = grown,
		.programs = *seed.programs,
		.tree     = none,
	};

	const Moment moment = {
		.passes = context.passes,
		.moment = PassMoment::Grown,
	};

	return packedSubtree(passedSubtree(moment, query, unused));
}

std::uint64_t mixedHash(std::uint64_t hash, std::uint64_t value) {
	return (hash ^ value) * 0x100000001B3ull;
}

std::uint64_t listHash(const std::vector<ProgramReference>& references, const ProgramList& list) {
	std::uint64_t hash = 0xCBF29CE484222325ull;
	for (std::uint32_t k = 0; k < list.count; ++k) {
		const ProgramReference& reference = references[list.first + k];
		hash                              = mixedHash(mixedHash(hash, reference.index), std::uint64_t(reference.source));
	}
	return hash;
}

struct HeldCut {
	const ProgramCut& cut;
	const std::uint16_t* entries;
};

std::uint32_t entryCount(const ProgramCut& cut) {
	return 2 * std::uint32_t(cut.ranges) + 2 * std::uint32_t(cut.rerouted);
}

std::uint64_t cutHash(const HeldCut& held) {
	std::uint64_t hash = mixedHash(mixedHash(0xCBF29CE484222325ull, held.cut.object), held.cut.result);
	hash               = mixedHash(hash, std::uint64_t(held.cut.ranges) | std::uint64_t(held.cut.rerouted) << 16);
	for (std::uint32_t k = 0; k < entryCount(held.cut); ++k) {
		hash = mixedHash(hash, held.entries[k]);
	}
	return hash;
}

bool sameCut(const HeldCut& a, const HeldCut& b) {
	const bool alike = a.cut.object == b.cut.object && a.cut.result == b.cut.result && a.cut.ranges == b.cut.ranges && a.cut.rerouted == b.cut.rerouted;
	return alike && std::equal(a.entries, a.entries + entryCount(a.cut), b.entries);
}

HeldCut treeCut(const Tree& tree, std::uint32_t cut) {
	return HeldCut {
		.cut     = tree.cuts[cut],
		.entries = tree.cutSteps.data() + tree.cuts[cut].first,
	};
}

bool sameReference(const ProgramReference& a, const ProgramReference& b) {
	return a.index == b.index && a.source == b.source;
}

struct ListKey {
	const std::vector<ProgramReference>& references;
	ProgramList list;
};

bool sameList(const Tree& tree, std::uint32_t held, const ListKey& key) {
	const ProgramList& list = tree.lists[held];
	if (list.count != key.list.count) {
		return false;
	}
	for (std::uint32_t k = 0; k < list.count; ++k) {
		if (!sameReference(tree.references[list.first + k], key.references[key.list.first + k])) {
			return false;
		}
	}
	return true;
}

std::size_t freeSlot(const std::vector<std::uint32_t>& slots, std::uint64_t hash) {
	std::size_t slot = std::size_t(hash) & (slots.size() - 1);
	while (slots[slot] != 0) {
		slot = (slot + 1) & (slots.size() - 1);
	}
	return slot;
}

void grownLists(const Tree& tree, ProgramTable& table) {
	if (table.taken[0] * 2 < table.lists.size()) {
		return;
	}
	std::vector<std::uint32_t> slots(table.lists.size() * 2, 0);
	for (std::uint32_t held = 0; held < tree.lists.size(); ++held) {
		slots[freeSlot(slots, listHash(tree.references, tree.lists[held]))] = held + 1;
	}
	table.lists = std::move(slots);
}

void grownCuts(const Tree& tree, ProgramTable& table) {
	if (table.taken[1] * 2 < table.cuts.size()) {
		return;
	}
	std::vector<std::uint32_t> slots(table.cuts.size() * 2, 0);
	for (std::uint32_t held = 0; held < tree.cuts.size(); ++held) {
		slots[freeSlot(slots, cutHash(treeCut(tree, held)))] = held + 1;
	}
	table.cuts = std::move(slots);
}

std::uint32_t storedList(Tree& tree, ProgramTable& table, const ListKey& key) {
	grownLists(tree, table);
	const std::uint64_t hash = listHash(key.references, key.list);
	std::size_t slot         = std::size_t(hash) & (table.lists.size() - 1);
	for (; table.lists[slot] != 0; slot = (slot + 1) & (table.lists.size() - 1)) {
		if (sameList(tree, table.lists[slot] - 1, key)) {
			return table.lists[slot] - 1;
		}
	}

	const ProgramList list = {
		.first = std::uint32_t(tree.references.size()),
		.count = key.list.count,
	};

	tree.references.insert(tree.references.end(), key.references.begin() + key.list.first, key.references.begin() + key.list.first + key.list.count);
	tree.lists.push_back(list);
	table.lists[slot] = std::uint32_t(tree.lists.size());
	table.taken[0] += 1;
	return std::uint32_t(tree.lists.size() - 1);
}

std::uint32_t storedCut(Tree& tree, ProgramTable& table, const HeldCut& cut) {
	grownCuts(tree, table);
	std::size_t slot = std::size_t(cutHash(cut)) & (table.cuts.size() - 1);
	for (; table.cuts[slot] != 0; slot = (slot + 1) & (table.cuts.size() - 1)) {
		if (sameCut(treeCut(tree, table.cuts[slot] - 1), cut)) {
			return table.cuts[slot] - 1;
		}
	}
	ProgramCut stored = cut.cut;
	stored.first      = std::uint32_t(tree.cutSteps.size());
	tree.cutSteps.insert(tree.cutSteps.end(), cut.entries, cut.entries + entryCount(cut.cut));
	tree.cuts.push_back(stored);
	table.cuts[slot] = std::uint32_t(tree.cuts.size());
	table.taken[1] += 1;
	return std::uint32_t(tree.cuts.size() - 1);
}

std::vector<ProgramReference> storedReferences(Tree& tree, ProgramTable& table, const Subtree& subtree) {
	std::vector<std::uint32_t> cuts;
	for (const ProgramCut& cut : subtree.cuts) {
		const HeldCut held = {
			.cut     = cut,
			.entries = subtree.cutSteps.data() + cut.first,
		};

		cuts.push_back(storedCut(tree, table, held));
	}
	std::vector<ProgramReference> references = subtree.references;
	for (ProgramReference& reference : references) {
		reference.index = reference.source == ProgramSource::Cut ? cuts[reference.index] : reference.index;
	}
	return references;
}

std::vector<std::uint32_t> storedLists(Tree& tree, ProgramTable& table, const Subtree& subtree) {
	const std::vector<ProgramReference> references = storedReferences(tree, table, subtree);
	std::vector<std::uint32_t> map;
	for (const ProgramList& list : subtree.lists) {
		const ListKey key = {
			.references = references,
			.list       = list,
		};

		map.push_back(storedList(tree, table, key));
	}
	return map;
}

struct Rebase {
	std::uint32_t run;
	std::uint32_t node;
	std::uint32_t surface;
	std::uint32_t solid;
	const std::vector<std::uint32_t>& lists;
};

SurfaceLeaf rebasedSurface(const SurfaceLeaf& leaf, const Rebase& rebase) {
	SurfaceLeaf moved = leaf;
	moved.run += rebase.run;
	moved.programs = rebase.lists[leaf.programs];
	return moved;
}

SolidLeaf rebasedSolid(const SolidLeaf& leaf, const Rebase& rebase) {
	return SolidLeaf {
		.programs = rebase.lists[leaf.programs],
	};
}

PackedNode rebasedNode(const PackedNode& node, const Rebase& rebase) {
	PackedNode moved = node;
	moved.firstBranch += rebase.node;
	moved.firstSurface += rebase.surface;
	moved.firstSolid += rebase.solid;
	return moved;
}

RootRecord rebasedRecord(const RootRecord& record, const Rebase& rebase) {
	RootRecord moved = record;
	moved.node       = record.kind == CellKind::Branch ? rebasedNode(record.node, rebase) : record.node;
	moved.surface    = record.kind == CellKind::Surface ? rebasedSurface(record.surface, rebase) : record.surface;
	moved.solid      = record.kind == CellKind::Filled ? rebasedSolid(record.solid, rebase) : record.solid;
	return moved;
}

Subtree placedSubtree(Tree& tree, const Subtree& subtree, ProgramTable& table) {
	const std::vector<std::uint32_t> lists = storedLists(tree, table, subtree);

	const Rebase rebase = {
		.run     = std::uint32_t(tree.runs.size()),
		.node    = std::uint32_t(tree.nodes.size()),
		.surface = std::uint32_t(tree.surfaces.size()),
		.solid   = std::uint32_t(tree.solids.size()),
		.lists   = lists,
	};

	Subtree placed    = subtree;
	placed.lists      = {};
	placed.references = {};
	placed.cuts       = {};
	placed.cutSteps   = {};
	placed.root       = rebasedRecord(subtree.root, rebase);
	for (PackedNode& node : placed.nodes) {
		node = rebasedNode(node, rebase);
	}
	for (SurfaceLeaf& leaf : placed.surfaces) {
		leaf = rebasedSurface(leaf, rebase);
	}
	for (SolidLeaf& leaf : placed.solids) {
		leaf = rebasedSolid(leaf, rebase);
	}
	return placed;
}

RootRecord writtenSubtree(Tree& tree, const Subtree& placed) {
	tree.runs.insert(tree.runs.end(), placed.runs.begin(), placed.runs.end());
	tree.nodes.insert(tree.nodes.end(), placed.nodes.begin(), placed.nodes.end());
	tree.surfaces.insert(tree.surfaces.end(), placed.surfaces.begin(), placed.surfaces.end());
	tree.solids.insert(tree.solids.end(), placed.solids.begin(), placed.solids.end());
	return placed.root;
}

RootRecord placedRecord(BuildContext& context, Tree& tree, const Subtree& finished) {
	const Subtree placed = placedSubtree(tree, finished, context.programs);

	const CellPrograms nothing = {
		.objects  = {},
		.programs = {},
	};

	const PassQuery query = {
		.context  = context.grow,
		.subtree  = placed,
		.programs = nothing,
		.tree     = tree,
	};

	const Moment moment = {
		.passes = context.passes,
		.moment = PassMoment::Placed,
	};

	return writtenSubtree(tree, passedSubtree(moment, query, context.state));
}

void placeReady(BuildContext& context, Tree& tree, Ordered& ordered) {
	while (ordered.placed < ordered.finished.size() && ordered.finished[ordered.placed]) {
		context.roots[ordered.placed] = placedRecord(context, tree, *ordered.finished[ordered.placed]);
		ordered.finished[ordered.placed].reset();
		ordered.placed += 1;
	}
}

void releasedFreeMemory() {
#if defined(__GLIBC__)
	malloc_trim(0);
#endif
}

void largeBlocksMapped() {
#if defined(__GLIBC__)
	mallopt(M_MMAP_THRESHOLD, mappedBlock);
#endif
}

bool placedTrimDue(BuildContext& context, Tree& tree, Ordered& ordered, std::size_t seed, Subtree finished) {
	const std::lock_guard<std::mutex> hold(ordered.lock);
	ordered.finished[seed] = std::move(finished);
	placeReady(context, tree, ordered);
	ordered.done += 1;
	return ordered.done % trimEvery == 0;
}

void placeSeeds(BuildContext& context, Tree& tree) {
	Ordered ordered = {
		.finished = std::vector<std::optional<Subtree>>(context.seeds.size()),
		.placed   = 0,
		.done     = 0,
		.lock     = {},
	};

	context.roots.assign(context.seeds.size(), context.top.root);
	inParallel(context.workers, context.seeds.size(), [&](std::size_t i) {
		Subtree finished = finishedSubtree(context, context.seeds[i]);
		context.seeds[i].programs.reset();
		if (placedTrimDue(context, tree, ordered, i, std::move(finished))) {
			releasedFreeMemory();
		}
	});
}

Subtree topLeavesPlaced(BuildContext& context, Tree& tree) {
	const std::vector<std::uint32_t> lists = storedLists(tree, context.programs, context.top);

	const Rebase rebase = {
		.run     = std::uint32_t(tree.runs.size()),
		.node    = 0,
		.surface = 0,
		.solid   = 0,
		.lists   = lists,
	};

	Subtree top = context.top;
	tree.runs.insert(tree.runs.end(), top.runs.begin(), top.runs.end());
	for (SurfaceLeaf& leaf : top.surfaces) {
		leaf = rebasedSurface(leaf, rebase);
	}
	for (SolidLeaf& leaf : top.solids) {
		leaf = rebasedSolid(leaf, rebase);
	}
	return top;
}

CellEntry rootEntry(Tree& tree, const RootRecord& record) {
	switch (record.kind) {
		case CellKind::Branch: tree.nodes.push_back(record.node); return cellAt(CellKind::Branch, tree.nodes.size() - 1);
		case CellKind::Surface: tree.surfaces.push_back(record.surface); return cellAt(CellKind::Surface, tree.surfaces.size() - 1);
		case CellKind::Filled: tree.solids.push_back(record.solid); return cellAt(CellKind::Filled, tree.solids.size() - 1);
		case CellKind::Empty: break;
	}

	return cellAt(CellKind::Empty, 0);
}

void placeTop(BuildContext& context, Tree& tree) {
	const Subtree top = topLeavesPlaced(context, tree);

	const Packing packing = {
		.octree     = top,
		.roots      = context.roots,
		.graftDepth = context.seedDepth,
	};

	Packed packed = {
		.nodes    = {},
		.surfaces = {},
		.solids   = {},
		.bases    = { std::uint32_t(tree.nodes.size()), std::uint32_t(tree.surfaces.size()), std::uint32_t(tree.solids.size()) },
	};

	const RootRecord root = packedRoot(packing, packed);
	tree.nodes.insert(tree.nodes.end(), packed.nodes.begin(), packed.nodes.end());
	tree.surfaces.insert(tree.surfaces.end(), packed.surfaces.begin(), packed.surfaces.end());
	tree.solids.insert(tree.solids.end(), packed.solids.begin(), packed.solids.end());
	tree.root = rootEntry(tree, root);
}

Tree buildTree(const BuildInput& input) {
	largeBlocksMapped();
	BuildContext context = buildContext(input);
	releasedFreeMemory();
	Tree tree = emptyTree(input.region);
	placeSeeds(context, tree);
	placeTop(context, tree);
	return tree;
}

}
