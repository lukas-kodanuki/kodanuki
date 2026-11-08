#pragma once

#include "tree/grow.h"
#include "tree/tree.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The index over a tree's face runs, found again from what a run
 * reads as.
 *
 * Slots are open addressed, a power of two of them, and hold one more
 * than the first word of a run, so that 0 is free. Only the first run
 * of any contents is in it. Face runs are written in the cell's own
 * coordinates, so a surface that sits alike in two cells of one size
 * reads the same words.
 */
struct SharedRuns {
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
 * Return an index over no runs.
 */
SharedRuns emptySharedRuns();

/**
 * Point each face run of a placed subtree at an equal run the tree or
 * the subtree already holds.
 *
 * The subtree's indices are the tree's, its runs to be written at the
 * tree's end. A run equal word for word to one in the index is dropped
 * and its leaves name the one already held; every other run is kept,
 * in the order the leaves name them, and told to the index. Nothing is
 * rounded, so every leaf reads back the same.
 *
 * A mutable reference, as rule 46 allows for build-time accumulators.
 *
 * @param tree The tree so far.
 * @param subtree The placed subtree.
 * @param runs The index over the tree's runs.
 */
Subtree sharedCoefficients(const Tree& tree, const Subtree& subtree, SharedRuns& runs);

}
