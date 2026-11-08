#include "tree/pack.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

struct Slot {
	std::uint32_t cell;
	int depth;
};

struct Filling {
	PackedNode node;
	std::vector<Slot> deeper;
};

bool isGraft(const Packing& packing, const Slot& slot) {
	return slot.depth == packing.graftDepth && packing.octree.cells[slot.cell].kind == CellKind::Branch;
}

const RootRecord& graftOf(const Packing& packing, const Slot& slot) {
	return packing.roots[packing.octree.cells[slot.cell].payload];
}

CellKind kindAt(const Packing& packing, const Slot& slot) {
	return isGraft(packing, slot) ? graftOf(packing, slot).kind : packing.octree.cells[slot.cell].kind;
}

SurfaceLeaf slotSurface(const Packing& packing, const Slot& slot) {
	return isGraft(packing, slot) ? graftOf(packing, slot).surface : packing.octree.surfaces[packing.octree.cells[slot.cell].payload];
}

SolidLeaf slotSolid(const Packing& packing, const Slot& slot) {
	return isGraft(packing, slot) ? graftOf(packing, slot).solid : packing.octree.solids[packing.octree.cells[slot.cell].payload];
}

void markSlot(PackedNode& node, int index, CellKind kind) {
	const std::uint64_t bit = std::uint64_t(1) << index;
	node.lowerKinds |= (int(kind) & 1) != 0 ? bit : 0;
	node.upperKinds |= (int(kind) & 2) != 0 ? bit : 0;
}

struct Adopting {
	const Packing& packing;
	Packed& packed;
	Filling& filling;
};

void adoptSlot(const Adopting& adopting, int index, const Slot& slot) {
	const CellKind kind = kindAt(adopting.packing, slot);
	markSlot(adopting.filling.node, index, kind);
	switch (kind) {
		case CellKind::Surface: adopting.packed.surfaces.push_back(slotSurface(adopting.packing, slot)); return;
		case CellKind::Filled: adopting.packed.solids.push_back(slotSolid(adopting.packing, slot)); return;
		case CellKind::Branch: adopting.filling.deeper.push_back(slot); return;
		case CellKind::Empty: return;
	}
}

Slot childSlot(const Packing& packing, const Slot& slot, int octant) {
	return Slot {
		.cell  = packing.octree.cells[slot.cell].payload + std::uint32_t(octant),
		.depth = slot.depth + 1,
	};
}

void adoptChild(const Adopting& adopting, const Slot& child, int octant) {
	if (kindAt(adopting.packing, child) != CellKind::Branch) {
		adopting.filling.node.coarse |= std::uint8_t(1u << octant);
		adoptSlot(adopting, octant * 8, child);
		return;
	}
	for (int sub = 0; sub < 8; ++sub) {
		adoptSlot(adopting, octant * 8 + sub, childSlot(adopting.packing, child, sub));
	}
}

std::uint8_t busyChildren(const PackedNode& node) {
	const std::uint64_t occupied = node.lowerKinds | node.upperKinds;
	std::uint8_t busy            = 0;
	for (int octant = 0; octant < 8; ++octant) {
		busy |= ((occupied >> (octant * 8)) & 0xFF) != 0 ? std::uint8_t(1u << octant) : 0;
	}
	return busy;
}

Filling blankFilling(const Packed& packed) {
	const PackedNode node = {
		.lowerKinds   = 0,
		.upperKinds   = 0,
		.firstBranch  = 0,
		.firstSurface = packed.bases[1] + std::uint32_t(packed.surfaces.size()),
		.firstSolid   = packed.bases[2] + std::uint32_t(packed.solids.size()),
		.coarse       = 0,
		.busy         = 0,
	};

	return Filling {
		.node   = node,
		.deeper = {},
	};
}

PackedNode packedNode(const Packing& packing, Packed& packed, const Slot& slot) {
	Filling filling = blankFilling(packed);

	const Adopting adopting = {
		.packing = packing,
		.packed  = packed,
		.filling = filling,
	};

	for (int octant = 0; octant < 8; ++octant) {
		adoptChild(adopting, childSlot(packing, slot, octant), octant);
	}
	filling.node.busy        = busyChildren(filling.node);
	const std::size_t start  = packed.nodes.size();
	filling.node.firstBranch = packed.bases[0] + std::uint32_t(start);
	packed.nodes.resize(start + filling.deeper.size());
	for (std::size_t k = 0; k < filling.deeper.size(); ++k) {
		const Slot& deeper      = filling.deeper[k];
		const PackedNode row    = isGraft(packing, deeper) ? graftOf(packing, deeper).node : packedNode(packing, packed, deeper);
		packed.nodes[start + k] = row;
	}
	return filling.node;
}

RootRecord packedRoot(const Packing& packing, Packed& packed) {
	const Slot root = {
		.cell  = 0,
		.depth = 0,
	};

	RootRecord record = packing.octree.root;
	record.kind       = kindAt(packing, root);
	switch (record.kind) {
		case CellKind::Surface: record.surface = slotSurface(packing, root); break;
		case CellKind::Filled: record.solid = slotSolid(packing, root); break;
		case CellKind::Branch: record.node = packedNode(packing, packed, root); break;
		case CellKind::Empty: break;
	}

	return record;
}

Subtree packedSubtree(const Subtree& subtree) {
	const std::vector<RootRecord> none;

	const Packing packing = {
		.octree     = subtree,
		.roots      = none,
		.graftDepth = -1,
	};

	Packed packed = {
		.nodes    = {},
		.surfaces = {},
		.solids   = {},
		.bases    = { 0, 0, 0 },
	};

	const RootRecord root = packedRoot(packing, packed);
	return Subtree {
		.box         = subtree.box,
		.depth       = subtree.depth,
		.cells       = {},
		.surfaces    = packed.surfaces,
		.solids      = packed.solids,
		.runs        = subtree.runs,
		.lists       = subtree.lists,
		.references  = subtree.references,
		.cuts        = subtree.cuts,
		.cutSteps    = subtree.cutSteps,
		.wholeFitted = {},
		.nodes       = packed.nodes,
		.root        = root,
	};
}

}
