#pragma once

#include "tree/grow.h"
#include "tree/tree.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The index over a tree's packed nodes, found again from what a node
 * holds.
 *
 * Slots are open addressed, a power of two of them, and hold one more
 * than a node's index in Tree::nodes, so that 0 is free. Only the first
 * node of any contents is in it. Two nodes hold alike where their slots
 * have the same kinds, their leaves the same records and their branch
 * children the same records, which name the same rows once the
 * children are alike: so alike nodes stand for alike subtrees.
 */
struct SharedNodes {
	/**
	 * The slots.
	 */
	std::vector<std::uint32_t> slots;
	/**
	 * How many slots are taken.
	 */
	std::size_t taken;
};

/**
 * Return an index over no nodes.
 */
SharedNodes emptySharedNodes();

/**
 * Turn each node of a placed subtree that holds alike to one the tree
 * or the subtree already holds into a copy of that one, and drop what
 * stood below it.
 *
 * The subtree's indices are the tree's, its nodes, leaves and runs to
 * be written at the tree's end. Its nodes are read from the deepest
 * up, so a node's children are settled before it is. A copy keeps its
 * place in its parent's row and names the rows of the node it copies;
 * the nodes, leaves and runs only it named are dropped, and the rest
 * move up to close the gaps. The root's record is read last. Every
 * node that is no copy is told to the index. Every hit reads back the
 * same.
 *
 * A mutable reference, as rule 46 allows for build-time accumulators.
 *
 * @param tree The tree so far.
 * @param subtree The placed subtree.
 * @param nodes The index over the tree's nodes.
 */
Subtree sharedSubtrees(const Tree& tree, const Subtree& subtree, SharedNodes& nodes);

}
