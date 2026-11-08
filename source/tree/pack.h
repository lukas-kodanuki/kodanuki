#pragma once

#include "tree/grow.h"
#include "tree/tree.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The octree a pack reads and the subtrees grafted into it.
 */
struct Packing {
	/**
	 * The octree and its leaves.
	 */
	const Subtree& octree;
	/**
	 * The records of the subtrees grafted in, for the top of the tree;
	 * empty for a subtree.
	 */
	const std::vector<RootRecord>& roots;
	/**
	 * How deep below the octree's root the grafted subtrees stand, even;
	 * there every branch's payload names its record. -1 where nothing is
	 * grafted.
	 */
	int graftDepth;
};

/**
 * What a pack writes: nodes and leaves in node order, and where in the
 * tree they will begin.
 */
struct Packed {
	/**
	 * The nodes below the root, each node's branch children in a row.
	 */
	std::vector<PackedNode> nodes;
	/**
	 * The surface leaves, in the order the nodes name them.
	 */
	std::vector<SurfaceLeaf> surfaces;
	/**
	 * The solid leaves, in the order the nodes name them.
	 */
	std::vector<SolidLeaf> solids;
	/**
	 * Where the first node, surface leaf and solid leaf written will
	 * stand in the tree: 0 for a subtree, which is moved when it is
	 * placed, and the tree's sizes for the top.
	 */
	std::uint32_t bases[3];
};

/**
 * Pack a grown subtree into 64-nodes, two octree levels a node.
 *
 * The leaves come across as they are, reordered into the order the
 * nodes name them; nothing is fitted again. The octree is dropped and
 * the root's record kept apart.
 *
 * @param subtree The grown subtree.
 */
Subtree packedSubtree(const Subtree& subtree);

/**
 * Pack an octree from its root and return the root's record.
 *
 * A branch is one node whose 64 slots are its grandchildren, a coarse
 * child that is a leaf writing its kind at the first of its eight. The
 * nodes it names are written in a row, then packed in turn, depth
 * first. A grafted branch's node is its record's, copied into the row.
 *
 * A mutable reference, as rule 46 allows for build-time accumulators.
 *
 * @param packing The octree and what is grafted into it.
 * @param packed Where the nodes and leaves are written.
 */
RootRecord packedRoot(const Packing& packing, Packed& packed);

}
