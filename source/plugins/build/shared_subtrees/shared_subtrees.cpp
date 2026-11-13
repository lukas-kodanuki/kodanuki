#include "plugins/build/shared_subtrees/shared_subtrees.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace kodanuki {

constexpr std::size_t leastNodeSlots = 1024;
constexpr std::uint32_t nowhere      = 0xFFFFFFFFu;

struct NodeSource {
	const Tree& tree;
	const std::vector<PackedNode>& nodes;
	const std::vector<SurfaceLeaf>& surfaces;
	const std::vector<SolidLeaf>& solids;
};

struct Dropping {
	const Subtree& subtree;
	const std::uint32_t* bases;
	std::vector<bool>& nodes;
	std::vector<bool>& surfaces;
	std::vector<bool>& solids;
};

struct Rows {
	std::vector<std::uint32_t> nodes;
	std::vector<std::uint32_t> surfaces;
	std::vector<std::uint32_t> solids;
};

SharedNodes emptySharedNodes() {
	return SharedNodes {
		.slots = std::vector<std::uint32_t>(leastNodeSlots, 0),
		.taken = 0,
	};
}

int branchCount(const PackedNode& node) {
	return std::popcount(node.lowerKinds & node.upperKinds);
}

int surfaceCount(const PackedNode& node) {
	return std::popcount(node.upperKinds & ~node.lowerKinds);
}

int solidCount(const PackedNode& node) {
	return std::popcount(node.lowerKinds & ~node.upperKinds);
}

const PackedNode& nodeAt(const NodeSource& source, std::uint32_t index) {
	const std::size_t base = source.tree.nodes.size();
	return index < base ? source.tree.nodes[index] : source.nodes[index - base];
}

const SurfaceLeaf& surfaceAt(const NodeSource& source, std::uint32_t index) {
	const std::size_t base = source.tree.surfaces.size();
	return index < base ? source.tree.surfaces[index] : source.surfaces[index - base];
}

const SolidLeaf& solidAt(const NodeSource& source, std::uint32_t index) {
	const std::size_t base = source.tree.solids.size();
	return index < base ? source.tree.solids[index] : source.solids[index - base];
}

std::uint64_t mixedWord(std::uint64_t hash, std::uint64_t word) {
	return (hash ^ word) * 0x100000001B3ull ^ (word >> 29);
}

std::uint64_t ownHash(const PackedNode& node) {
	const std::uint64_t marks = std::uint64_t(node.coarse) | std::uint64_t(node.busy) << 8;
	return mixedWord(mixedWord(mixedWord(0xCBF29CE484222325ull, node.lowerKinds), node.upperKinds), marks);
}

std::uint64_t recordHash(const NodeSource& source, const PackedNode& node) {
	std::uint64_t hash = ownHash(node);
	for (int k = 0; k < surfaceCount(node); ++k) {
		const SurfaceLeaf& leaf = surfaceAt(source, node.firstSurface + std::uint32_t(k));
		hash                    = mixedWord(hash, std::uint64_t(leaf.run) | std::uint64_t(leaf.programs) << 32);
		hash                    = mixedWord(hash, std::uint64_t(leaf.loosened) | std::uint64_t(leaf.kind) << 16);
	}
	for (int k = 0; k < solidCount(node); ++k) {
		hash = mixedWord(hash, solidAt(source, node.firstSolid + std::uint32_t(k)).programs);
	}
	for (int k = 0; k < branchCount(node); ++k) {
		hash = mixedWord(hash, ownHash(nodeAt(source, node.firstBranch + std::uint32_t(k))));
	}
	return hash;
}

bool sameOwn(const PackedNode& a, const PackedNode& b) {
	if (a.lowerKinds != b.lowerKinds || a.upperKinds != b.upperKinds || a.coarse != b.coarse || a.busy != b.busy) {
		return false;
	}
	const bool branches = branchCount(a) == 0 || a.firstBranch == b.firstBranch;
	const bool surfaces = surfaceCount(a) == 0 || a.firstSurface == b.firstSurface;
	const bool solids   = solidCount(a) == 0 || a.firstSolid == b.firstSolid;
	return branches && surfaces && solids;
}

bool sameSurfaceLeaf(const SurfaceLeaf& a, const SurfaceLeaf& b) {
	return a.run == b.run && a.programs == b.programs && a.loosened == b.loosened && a.kind == b.kind;
}

bool sameLeaves(const NodeSource& source, const PackedNode& a, const PackedNode& b) {
	for (int k = 0; k < surfaceCount(a); ++k) {
		if (!sameSurfaceLeaf(surfaceAt(source, a.firstSurface + std::uint32_t(k)), surfaceAt(source, b.firstSurface + std::uint32_t(k)))) {
			return false;
		}
	}
	for (int k = 0; k < solidCount(a); ++k) {
		if (solidAt(source, a.firstSolid + std::uint32_t(k)).programs != solidAt(source, b.firstSolid + std::uint32_t(k)).programs) {
			return false;
		}
	}
	return true;
}

bool sameChildren(const NodeSource& source, const PackedNode& a, const PackedNode& b) {
	for (int k = 0; k < branchCount(a); ++k) {
		if (!sameOwn(nodeAt(source, a.firstBranch + std::uint32_t(k)), nodeAt(source, b.firstBranch + std::uint32_t(k)))) {
			return false;
		}
	}
	return true;
}

bool sameRecord(const NodeSource& source, const PackedNode& a, const PackedNode& b) {
	if (a.lowerKinds != b.lowerKinds || a.upperKinds != b.upperKinds || a.coarse != b.coarse || a.busy != b.busy) {
		return false;
	}
	return sameLeaves(source, a, b) && sameChildren(source, a, b);
}

struct Lookup {
	const NodeSource& source;
	const SharedNodes& index;
};

std::size_t nodeSlot(const Lookup& lookup, const PackedNode& node) {
	const std::vector<std::uint32_t>& slots = lookup.index.slots;
	std::size_t slot                        = std::size_t(recordHash(lookup.source, node)) & (slots.size() - 1);
	while (slots[slot] != 0 && !sameRecord(lookup.source, nodeAt(lookup.source, slots[slot] - 1), node)) {
		slot = (slot + 1) & (slots.size() - 1);
	}
	return slot;
}

std::optional<std::uint32_t> heldNode(const Lookup& lookup, const PackedNode& node) {
	const std::uint32_t entry = lookup.index.slots[nodeSlot(lookup, node)];
	return entry != 0 ? std::optional<std::uint32_t>(entry - 1) : std::nullopt;
}

void widenedNodes(const NodeSource& source, SharedNodes& index) {
	if (index.taken * 2 < index.slots.size()) {
		return;
	}
	std::vector<std::uint32_t> slots(index.slots.size() * 2, 0);
	for (const std::uint32_t entry : index.slots) {
		if (entry != 0) {
			std::size_t slot = std::size_t(recordHash(source, nodeAt(source, entry - 1))) & (slots.size() - 1);
			while (slots[slot] != 0) {
				slot = (slot + 1) & (slots.size() - 1);
			}
			slots[slot] = entry;
		}
	}
	index.slots = slots;
}

void toldNode(const NodeSource& source, SharedNodes& index, std::uint32_t node) {
	const Lookup lookup = {
		.source = source,
		.index  = index,
	};

	const std::size_t slot = nodeSlot(lookup, nodeAt(source, node));
	if (index.slots[slot] == 0) {
		index.slots[slot] = node + 1;
		index.taken += 1;
		widenedNodes(source, index);
	}
}

void droppedRows(const Dropping& dropping, const PackedNode& node) {
	for (int k = 0; k < surfaceCount(node); ++k) {
		dropping.surfaces[node.firstSurface - dropping.bases[1] + std::uint32_t(k)] = true;
	}
	for (int k = 0; k < solidCount(node); ++k) {
		dropping.solids[node.firstSolid - dropping.bases[2] + std::uint32_t(k)] = true;
	}
	for (int k = 0; k < branchCount(node); ++k) {
		const std::uint32_t child = node.firstBranch - dropping.bases[0] + std::uint32_t(k);
		if (!dropping.nodes[child]) {
			dropping.nodes[child] = true;
			droppedRows(dropping, dropping.subtree.nodes[child]);
		}
	}
}

struct Folding {
	const NodeSource& source;
	const SharedNodes& global;
	SharedNodes& local;
	std::vector<PackedNode>& nodes;
	std::vector<bool>& copied;
};

std::optional<std::uint32_t> foundAnywhere(const Folding& folding, const PackedNode& node) {
	const Lookup inTree = {
		.source = folding.source,
		.index  = folding.global,
	};

	const Lookup inSubtree = {
		.source = folding.source,
		.index  = folding.local,
	};

	const std::optional<std::uint32_t> held = heldNode(inTree, node);
	return held ? held : heldNode(inSubtree, node);
}

void settledNode(const Folding& folding, const Dropping& dropping, std::uint32_t local) {
	const std::optional<std::uint32_t> found = foundAnywhere(folding, folding.nodes[local]);
	if (!found) {
		toldNode(folding.source, folding.local, dropping.bases[0] + local);
		return;
	}
	folding.nodes[local]  = nodeAt(folding.source, *found);
	folding.copied[local] = true;
	droppedRows(dropping, dropping.subtree.nodes[local]);
}

std::vector<std::uint32_t> keptBefore(const std::vector<bool>& dropped) {
	std::vector<std::uint32_t> before(dropped.size() + 1, 0);
	for (std::size_t k = 0; k < dropped.size(); ++k) {
		before[k + 1] = before[k] + (dropped[k] ? 0 : 1);
	}
	return before;
}

std::uint32_t movedIndex(std::uint32_t index, std::uint32_t base, const std::vector<std::uint32_t>& before) {
	if (index < base) {
		return index;
	}
	return base + before[std::min<std::size_t>(index - base, before.size() - 1)];
}

PackedNode movedNode(const PackedNode& node, const std::uint32_t* bases, const Rows& rows) {
	PackedNode moved   = node;
	moved.firstBranch  = movedIndex(node.firstBranch, bases[0], rows.nodes);
	moved.firstSurface = movedIndex(node.firstSurface, bases[1], rows.surfaces);
	moved.firstSolid   = movedIndex(node.firstSolid, bases[2], rows.solids);
	return moved;
}

template <typename T>
std::vector<T> keptOf(const std::vector<T>& items, const std::vector<bool>& dropped) {
	std::vector<T> kept;
	for (std::size_t k = 0; k < items.size(); ++k) {
		if (!dropped[k]) {
			kept.push_back(items[k]);
		}
	}
	return kept;
}

struct Fresh {
	const std::vector<std::uint32_t>& runs;
	std::uint32_t base;
	std::vector<std::uint32_t>& moved;
	std::vector<std::uint32_t>& kept;
};

std::uint32_t keptRun(const Fresh& fresh, std::uint32_t run) {
	if (run < fresh.base) {
		return run;
	}
	const std::uint32_t local = run - fresh.base;
	if (fresh.moved[local] == nowhere) {
		const std::uint32_t* words = fresh.runs.data() + local;
		fresh.moved[local]         = fresh.base + std::uint32_t(fresh.kept.size());
		fresh.kept.insert(fresh.kept.end(), words, words + faceOffsets(words).back());
	}
	return fresh.moved[local];
}

void keptRuns(const Tree& tree, Subtree& folded) {
	std::vector<std::uint32_t> moved(folded.runs.size(), nowhere);
	std::vector<std::uint32_t> kept;

	const Fresh fresh = {
		.runs  = folded.runs,
		.base  = std::uint32_t(tree.runs.size()),
		.moved = moved,
		.kept  = kept,
	};

	for (SurfaceLeaf& leaf : folded.surfaces) {
		leaf.run = keptRun(fresh, leaf.run);
	}
	if (folded.root.kind == CellKind::Surface) {
		folded.root.surface.run = keptRun(fresh, folded.root.surface.run);
	}
	folded.runs = kept;
}

struct Dropped {
	std::vector<bool> nodes;
	std::vector<bool> surfaces;
	std::vector<bool> solids;
};

Subtree closedGaps(const Tree& tree, const Subtree& subtree, const Dropped& dropped) {
	const std::uint32_t bases[3] = { std::uint32_t(tree.nodes.size()), std::uint32_t(tree.surfaces.size()), std::uint32_t(tree.solids.size()) };

	const Rows rows = {
		.nodes    = keptBefore(dropped.nodes),
		.surfaces = keptBefore(dropped.surfaces),
		.solids   = keptBefore(dropped.solids),
	};

	Subtree folded  = subtree;
	folded.nodes    = keptOf(subtree.nodes, dropped.nodes);
	folded.surfaces = keptOf(subtree.surfaces, dropped.surfaces);
	folded.solids   = keptOf(subtree.solids, dropped.solids);
	for (PackedNode& node : folded.nodes) {
		node = movedNode(node, bases, rows);
	}
	folded.root.node = folded.root.kind == CellKind::Branch ? movedNode(folded.root.node, bases, rows) : folded.root.node;
	keptRuns(tree, folded);
	return folded;
}

void toldKept(const NodeSource& source, const std::vector<bool>& copied, SharedNodes& index) {
	for (std::uint32_t k = 0; k < source.nodes.size(); ++k) {
		if (!copied[k]) {
			toldNode(source, index, std::uint32_t(source.tree.nodes.size()) + k);
		}
	}
}

struct Settling {
	const Tree& tree;
	const Subtree& subtree;
	const SharedNodes& global;
	Dropped& dropped;
	std::vector<bool>& copied;
};

Dropped droppedNone(const Subtree& subtree) {
	return Dropped {
		.nodes    = std::vector<bool>(subtree.nodes.size(), false),
		.surfaces = std::vector<bool>(subtree.surfaces.size(), false),
		.solids   = std::vector<bool>(subtree.solids.size(), false),
	};
}

void settledRoot(const Folding& folding, const Dropping& dropping, Subtree& settled) {
	if (settled.root.kind != CellKind::Branch) {
		return;
	}
	const std::optional<std::uint32_t> found = foundAnywhere(folding, settled.root.node);
	if (found) {
		droppedRows(dropping, settled.root.node);
		settled.root.node = nodeAt(folding.source, *found);
	}
}

Subtree settledSubtree(const Settling& settling) {
	const Tree& tree             = settling.tree;
	const Subtree& subtree       = settling.subtree;
	const std::uint32_t bases[3] = { std::uint32_t(tree.nodes.size()), std::uint32_t(tree.surfaces.size()), std::uint32_t(tree.solids.size()) };

	const Dropping dropping = {
		.subtree  = subtree,
		.bases    = bases,
		.nodes    = settling.dropped.nodes,
		.surfaces = settling.dropped.surfaces,
		.solids   = settling.dropped.solids,
	};

	Subtree settled   = subtree;
	SharedNodes local = emptySharedNodes();

	const NodeSource source = {
		.tree     = tree,
		.nodes    = settled.nodes,
		.surfaces = subtree.surfaces,
		.solids   = subtree.solids,
	};

	const Folding folding = {
		.source = source,
		.global = settling.global,
		.local  = local,
		.nodes  = settled.nodes,
		.copied = settling.copied,
	};

	for (std::uint32_t k = std::uint32_t(settled.nodes.size()); k-- > 0;) {
		settledNode(folding, dropping, k);
	}
	settledRoot(folding, dropping, settled);
	return settled;
}

Subtree sharedSubtrees(const Tree& tree, const Subtree& subtree, SharedNodes& nodes) {
	Dropped dropped = droppedNone(subtree);
	std::vector<bool> copied(subtree.nodes.size(), false);

	const Settling settling = {
		.tree    = tree,
		.subtree = subtree,
		.global  = nodes,
		.dropped = dropped,
		.copied  = copied,
	};

	const Subtree folded = closedGaps(tree, settledSubtree(settling), dropped);

	const NodeSource source = {
		.tree     = tree,
		.nodes    = folded.nodes,
		.surfaces = folded.surfaces,
		.solids   = folded.solids,
	};

	toldKept(source, keptOf(copied, dropped.nodes), nodes);
	return folded;
}

}
