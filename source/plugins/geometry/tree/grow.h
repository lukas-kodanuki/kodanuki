#pragma once

#include "plugins/geometry/tree/classify.h"
#include "plugins/geometry/tree/cut.h"
#include "plugins/geometry/tree/detail.h"
#include "plugins/geometry/tree/tree.h"
#include "sdf/scene.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace kodanuki {

/**
 * What every cell of a growing subtree reads, shared by all workers.
 */
struct GrowContext {
	/**
	 * The scene's geometry.
	 */
	const Scene& scene;
	/**
	 * How fine each cell must be.
	 */
	const Detail& detail;
	/**
	 * The deepest a cell may stand below the region.
	 */
	int depthCap;
	/**
	 * Whether derived faces are tried (`--use faces=off` turns them
	 * off).
	 */
	bool faces;
};

/**
 * A cell still to be grown.
 */
struct Sprout {
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * How deep it stands below the region.
	 */
	int depth;
	/**
	 * Its parent's programs, which it cuts, shared by the eight
	 * children of one branch so that a level of the top of the tree
	 * holds each parent's programs once.
	 */
	std::shared_ptr<const CellPrograms> programs;
};

/**
 * The record of a subtree's root, kept apart from its nodes.
 *
 * A packed node's children must stand in a row, and subtrees finish in
 * any order, so the top of the tree copies each root's record into its
 * own rows once every subtree is placed.
 */
struct RootRecord {
	/**
	 * What the root holds.
	 */
	CellKind kind;
	/**
	 * The root's node, read for a branch.
	 */
	PackedNode node;
	/**
	 * The root's leaf, read for a surface.
	 */
	SurfaceLeaf surface;
	/**
	 * The root's leaf, read for a filled root.
	 */
	SolidLeaf solid;
};

/**
 * A subtree grown by one worker, first as an octree, then packed.
 *
 * Its indices are its own until it is placed into the tree
 * (plugins/geometry/tree/build.h).
 */
struct Subtree {
	/**
	 * The subtree's root cell.
	 */
	Box box;
	/**
	 * How deep the root stands below the region, always even, so that a
	 * packed node covers two whole levels.
	 */
	int depth;
	/**
	 * The octree, root first, a branch's eight children in a row. It is
	 * emptied when the subtree is packed.
	 */
	std::vector<CellEntry> cells;
	/**
	 * The surface leaves, in the order they were grown and then in node
	 * order once packed.
	 */
	std::vector<SurfaceLeaf> surfaces;
	/**
	 * The solid leaves, likewise.
	 */
	std::vector<SolidLeaf> solids;
	/**
	 * The face runs of the surface leaves.
	 */
	std::vector<std::uint32_t> runs;
	/**
	 * The program lists the leaves name.
	 */
	std::vector<ProgramList> lists;
	/**
	 * The references the lists hold.
	 */
	std::vector<ProgramReference> references;
	/**
	 * The programs cut to a leaf's cell that references name.
	 */
	std::vector<ProgramCut> cuts;
	/**
	 * The entries of the cuts, end to end, as Tree::cutSteps.
	 */
	std::vector<std::uint16_t> cutSteps;
	/**
	 * Whether each surface leaf, as grown, is fitted to its cell's whole
	 * field: one patch or a seam pair, which a pass may read against the
	 * field. It is emptied when the subtree is packed.
	 */
	std::vector<bool> wholeFitted;
	/**
	 * The packed nodes below the root, empty until the subtree is
	 * packed.
	 */
	std::vector<PackedNode> nodes;
	/**
	 * The root's record once the subtree is packed.
	 */
	RootRecord root;
};

/**
 * Grow the whole octree below one cell.
 *
 * Each cell asks Detail how far it may stray, is classified, and asks
 * Detail's verdict; a leaf is written down, and a branch's eight
 * children are grown depth first in octant order, so the subtree is
 * the same however it is scheduled. Leaves with the same programs share
 * a program list.
 *
 * @param context What every cell reads.
 * @param sprout The subtree's root cell.
 */
Subtree growSubtree(const GrowContext& context, const Sprout& sprout);

/**
 * Find what one cell holds: its tolerance, its plan and Detail's
 * verdict, which turns a kept cell into a branch.
 *
 * @param context What every cell reads.
 * @param sprout The cell.
 */
Plan planCell(const GrowContext& context, const Sprout& sprout);

/**
 * Return a branch's eight children, in octant order, sharing the
 * branch's programs.
 *
 * @param sprout The branch.
 * @param programs The branch's programs, which the children cut.
 */
std::array<Sprout, 8> childSprouts(const Sprout& sprout, const std::shared_ptr<const CellPrograms>& programs);

/**
 * Return a subtree that holds only its root cell, still empty.
 *
 * @param sprout The root cell.
 */
Subtree emptySubtree(const Sprout& sprout);

/**
 * Write one planned leaf into a subtree's octree: its kind, its leaf
 * record, its run and its program list.
 *
 * A mutable reference, as rule 46 allows for build-time accumulators.
 *
 * @param subtree The subtree.
 * @param cell The cell's index in the octree.
 * @param plan The leaf's plan.
 */
void writeLeaf(Subtree& subtree, std::uint32_t cell, const Plan& plan);

/**
 * Turn one cell of a subtree's octree into a branch, with eight empty
 * children in a row at its end, and return the first child's index.
 *
 * @param subtree The subtree.
 * @param cell The cell's index in the octree.
 */
std::uint32_t openBranch(Subtree& subtree, std::uint32_t cell);

/**
 * A cell of a subtree's octree to grow again.
 */
struct Regrowth {
	/**
	 * The cell, with the programs it cuts: its parent's.
	 */
	Sprout sprout;
	/**
	 * The cell's index in the octree.
	 */
	std::uint32_t cell;
};

/**
 * Grow one cell of a subtree's octree again in place, with everything
 * below it, as growSubtree grows a cell.
 *
 * What the cell held before is left behind unread.
 *
 * A mutable reference, as rule 46 allows for build-time accumulators.
 *
 * @param context What every cell reads.
 * @param regrowth The cell and its parent's programs.
 * @param subtree The subtree.
 */
void regrowCell(const GrowContext& context, const Regrowth& regrowth, Subtree& subtree);

/**
 * Return the box of the region's root cell.
 *
 * @param context What every cell reads, for nothing but the scene.
 * @param region The region.
 */
Sprout rootSprout(const GrowContext& context, const Region& region);

}
