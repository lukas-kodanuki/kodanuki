#include "tree/tree.h"
#include "tree/patch.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace kodanuki {

static_assert(sizeof(PackedNode) == 32, "a packed node is half a cache line");

std::uint64_t slotBit(int slot) {
	return std::uint64_t(1) << slot;
}

CellKind slotKind(const PackedNode& node, int slot) {
	const std::uint64_t low  = (node.lowerKinds >> slot) & 1;
	const std::uint64_t high = (node.upperKinds >> slot) & 1;
	return CellKind(low | (high << 1));
}

std::uint64_t kindMask(const PackedNode& node, CellKind kind) {
	switch (kind) {
		case CellKind::Empty: return ~(node.lowerKinds | node.upperKinds);
		case CellKind::Filled: return node.lowerKinds & ~node.upperKinds;
		case CellKind::Surface: return node.upperKinds & ~node.lowerKinds;
		case CellKind::Branch: return node.lowerKinds & node.upperKinds;
	}

	return 0;
}

std::uint32_t kindBase(const PackedNode& node, CellKind kind) {
	switch (kind) {
		case CellKind::Filled: return node.firstSolid;
		case CellKind::Surface: return node.firstSurface;
		case CellKind::Branch: return node.firstBranch;
		case CellKind::Empty: return 0;
	}

	return 0;
}

std::uint32_t slotPayload(const PackedNode& node, int slot) {
	const CellKind kind       = slotKind(node, slot);
	const std::uint64_t below = kindMask(node, kind) & (slotBit(slot) - 1);
	return kindBase(node, kind) + std::uint32_t(std::popcount(below));
}

std::uint32_t tableWordCount(std::uint32_t count) {
	if (count <= 1) {
		return 0;
	}
	return std::max(std::uint32_t(1), (std::uint32_t(1) << count) / 32);
}

std::uint32_t firstFaceWord(const RunHeader& header) {
	return 1 + tableWordCount(header.count) + header.passWords;
}

RunHeader runHeader(std::uint32_t word) {
	return RunHeader {
		.count     = word & 0xFu,
		.patches   = (word >> 8) & 0xFFu,
		.planes    = (word >> 16) & 0xFFu,
		.needed    = (word >> 24) & 0xFFu,
		.slabbed   = ((word >> 4) & 1u) != 0,
		.passWords = (word >> 5) & 7u,
	};
}

std::uint32_t runHeaderWord(const RunHeader& header) {
	return header.count | (header.slabbed ? 1u << 4 : 0u) | (header.passWords << 5) | (header.patches << 8) | (header.planes << 16) | (header.needed << 24);
}

FaceKind faceKind(const RunHeader& header, std::uint32_t face) {
	const bool patch = ((header.patches >> face) & 1) != 0;
	const bool plane = ((header.planes >> face) & 1) != 0;
	if (patch) {
		return plane ? FaceKind::Ring : FaceKind::Patch;
	}
	return plane ? FaceKind::Plane : FaceKind::Quadric;
}

std::uint32_t bareWordCount(FaceKind kind, const std::uint32_t* words) {
	switch (kind) {
		case FaceKind::Plane: return 4;
		case FaceKind::Quadric: return 10;
		case FaceKind::Patch: return patchWordCount(words);
		case FaceKind::Ring: return 14;
	}

	return 0;
}

std::uint32_t faceWordCount(const RunHeader& header, std::uint32_t face, const std::uint32_t* words) {
	const FaceKind kind = faceKind(header, face);
	const bool slabbed  = header.slabbed && kind != FaceKind::Plane;
	return bareWordCount(kind, words) + (slabbed ? slabWordCount : 0);
}

std::vector<std::uint32_t> faceOffsets(const std::uint32_t* run) {
	const RunHeader header = runHeader(run[0]);
	std::vector<std::uint32_t> offsets;
	std::uint32_t at = firstFaceWord(header);
	for (std::uint32_t face = 0; face < header.count; ++face) {
		offsets.push_back(at);
		at += faceWordCount(header, face, run + at);
	}
	offsets.push_back(at);
	return offsets;
}

std::uint16_t loosening(double amount) {
	if (!(amount > 0.0)) {
		return 0;
	}
	float held = float(amount);
	if (double(held) < amount) {
		held = std::nextafter(held, std::numeric_limits<float>::infinity());
	}
	const std::uint32_t bits = std::bit_cast<std::uint32_t>(held);
	return std::uint16_t(std::min<std::uint32_t>(((bits + 0x7FFFFFu) & 0x7F800000u) >> 16, 0x7F80u));
}

double loosenedOf(const SurfaceLeaf& leaf) {
	return double(std::bit_cast<float>(std::uint32_t(leaf.loosened) << 16));
}

TreeStats emptyStats() {
	return TreeStats {
		.nodes     = 0,
		.cells     = 1,
		.branches  = 0,
		.empty     = 0,
		.surfaces  = 0,
		.solids    = 0,
		.patched   = 0,
		.faces     = { 0, 0, 0, 0 },
		.byCount   = { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		.floors    = 0,
		.megabytes = 0,
	};
}

struct Counting {
	const Tree& tree;
	TreeStats& stats;
};

void countNode(const Counting& counting, const PackedNode& node);

void countCell(const Counting& counting, const PackedNode& node, int slot) {
	const CellKind kind = slotKind(node, slot);
	counting.stats.empty += kind == CellKind::Empty ? 1 : 0;
	if (kind == CellKind::Branch) {
		countNode(counting, counting.tree.nodes[slotPayload(node, slot)]);
	}
}

void countNode(const Counting& counting, const PackedNode& node) {
	counting.stats.branches += 1;
	counting.stats.cells += 8;
	for (int child = 0; child < 8; ++child) {
		if (((node.coarse >> child) & 1) != 0) {
			countCell(counting, node, child * 8);
			continue;
		}
		counting.stats.branches += 1;
		counting.stats.cells += 8;
		for (int octant = 0; octant < 8; ++octant) {
			countCell(counting, node, child * 8 + octant);
		}
	}
}

void countRun(const Tree& tree, const SurfaceLeaf& leaf, TreeStats& stats) {
	const RunHeader header = runHeader(tree.runs[leaf.run]);
	for (std::uint32_t face = 0; face < header.count; ++face) {
		stats.faces[std::size_t(faceKind(header, face))] += 1;
	}
	stats.byCount[header.count] += 1;
	stats.patched += header.count == 1 && faceKind(header, 0) == FaceKind::Patch ? 1 : 0;
	stats.floors += leaf.kind == LeafKind::Floor ? 1 : 0;
}

std::size_t cutBytes(const Tree& tree) {
	return tree.cuts.size() * sizeof(ProgramCut) + tree.cutSteps.size() * sizeof(std::uint16_t);
}

double treeMegabytes(const Tree& tree) {
	const std::size_t bytes = tree.nodes.size() * sizeof(PackedNode) + tree.surfaces.size() * sizeof(SurfaceLeaf) + tree.solids.size() * sizeof(SolidLeaf) + tree.runs.size() * sizeof(std::uint32_t) + tree.lists.size() * sizeof(ProgramList) + tree.references.size() * sizeof(ProgramReference) + cutBytes(tree);
	return double(bytes) / (1024.0 * 1024.0);
}

TreeStats treeStats(const Tree& tree) {
	TreeStats stats = emptyStats();
	stats.nodes     = tree.nodes.size();
	stats.surfaces  = tree.surfaces.size();
	stats.solids    = tree.solids.size();
	stats.megabytes = treeMegabytes(tree);
	stats.empty     = tree.root.kind == CellKind::Empty ? 1 : 0;

	const Counting counting = {
		.tree  = tree,
		.stats = stats,
	};

	if (tree.root.kind == CellKind::Branch) {
		countNode(counting, tree.nodes[tree.root.payload]);
	}
	for (const SurfaceLeaf& leaf : tree.surfaces) {
		countRun(tree, leaf, stats);
	}
	return stats;
}

PlacedLeaf childLeaf(const PlacedLeaf& parent, const CellEntry& entry, int octant) {
	return PlacedLeaf {
		.entry = entry,
		.level = parent.level + 1,
		.place = { 2 * parent.place[0] + std::uint32_t(octant & 1), 2 * parent.place[1] + std::uint32_t((octant >> 1) & 1), 2 * parent.place[2] + std::uint32_t((octant >> 2) & 1) },
	};
}

CellEntry entryAt(const PackedNode& node, int slot) {
	const CellKind kind = slotKind(node, slot);
	return CellEntry {
		.kind    = kind,
		.payload = kind == CellKind::Empty ? 0 : slotPayload(node, slot),
	};
}

void placeLeaf(const Tree& tree, const PlacedLeaf& leaf, std::vector<PlacedLeaf>& placed);

void placeNode(const Tree& tree, const PlacedLeaf& branch, std::vector<PlacedLeaf>& placed) {
	const PackedNode& node = tree.nodes[branch.entry.payload];
	for (int child = 0; child < 8; ++child) {
		const PlacedLeaf coarse = childLeaf(branch, entryAt(node, child * 8), child);
		if (((node.coarse >> child) & 1) != 0) {
			placeLeaf(tree, coarse, placed);
			continue;
		}
		for (int octant = 0; octant < 8; ++octant) {
			placeLeaf(tree, childLeaf(coarse, entryAt(node, child * 8 + octant), octant), placed);
		}
	}
}

void placeLeaf(const Tree& tree, const PlacedLeaf& leaf, std::vector<PlacedLeaf>& placed) {
	switch (leaf.entry.kind) {
		case CellKind::Branch: placeNode(tree, leaf, placed); return;
		case CellKind::Surface:
		case CellKind::Filled: placed.push_back(leaf); return;
		case CellKind::Empty: return;
	}
}

std::vector<PlacedLeaf> placedLeaves(const Tree& tree) {
	const PlacedLeaf root = {
		.entry = tree.root,
		.level = 0,
		.place = { 0, 0, 0 },
	};

	std::vector<PlacedLeaf> placed;
	placeLeaf(tree, root, placed);
	return placed;
}

KeptSteps referencedSteps(const Scene& scene, const Tree& tree, const ProgramReference& reference) {
	if (reference.source == ProgramSource::Object) {
		return wholeSteps(scene.objectPrograms[reference.index]);
	}
	const ProgramCut& cut = tree.cuts[reference.index];
	return KeptSteps {
		.program  = scene.objectPrograms[cut.object],
		.entries  = tree.cutSteps.data() + cut.first,
		.ranges   = cut.ranges,
		.rerouted = cut.rerouted,
		.result   = cut.result,
	};
}

std::uint32_t referencedObject(const Tree& tree, const ProgramReference& reference) {
	return reference.source == ProgramSource::Cut ? tree.cuts[reference.index].object : reference.index;
}

std::vector<Step> keptStepsOf(const Scene& scene, const Tree& tree, std::uint32_t cut) {
	const ProgramCut& held = tree.cuts[cut];
	const Program& whole   = scene.objectPrograms[held.object];
	std::vector<Step> steps;
	const std::uint16_t* ranges = tree.cutSteps.data() + held.first;
	for (std::uint32_t r = 0; r < held.ranges; ++r) {
		steps.insert(steps.end(), whole.steps.begin() + ranges[2 * r], whole.steps.begin() + ranges[2 * r + 1]);
	}
	return steps;
}

}
