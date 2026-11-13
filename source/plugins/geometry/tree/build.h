#pragma once

#include "plugins/geometry/tree/detail.h"
#include "plugins/geometry/tree/grow.h"
#include "plugins/geometry/tree/pack.h"
#include "plugins/geometry/tree/pass.h"
#include "plugins/geometry/tree/tree.h"
#include "sdf/scene.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The index over the program lists and cut programs a tree holds, so
 * that one alike to one already there is not stored twice.
 *
 * Slots are open addressed, a power of two of them, and hold one more
 * than the index they name, so that 0 is free.
 */
struct ProgramTable {
	/**
	 * The slots over Tree::lists.
	 */
	std::vector<std::uint32_t> lists;
	/**
	 * The slots over Tree::cuts.
	 */
	std::vector<std::uint32_t> cuts;
	/**
	 * How many slots of each are taken, lists first.
	 */
	std::size_t taken[2];
};

/**
 * What a build is asked for.
 */
struct BuildInput {
	/**
	 * The scene's geometry.
	 */
	const Scene& scene;
	/**
	 * How fine each cell must be.
	 */
	const Detail& detail;
	/**
	 * The Build pass members that are on.
	 */
	const std::vector<PassMember>& passes;
	/**
	 * The box to build over.
	 */
	Region region;
	/**
	 * The deepest a cell may stand below the region.
	 */
	int depthCap;
	/**
	 * How many threads build, at least one.
	 */
	int workers;
	/**
	 * Whether derived faces are tried.
	 */
	bool faces;
};

/**
 * Everything a build holds that the finished tree does not: the top
 * of the tree, the index over program lists and the subtrees' roots.
 *
 * It is dropped when the tree is handed back.
 */
struct BuildContext {
	/**
	 * What every cell reads.
	 */
	GrowContext grow;
	/**
	 * The Build pass members that are on.
	 */
	const std::vector<PassMember>& passes;
	/**
	 * The state of the passes that keep it.
	 */
	PassState state;
	/**
	 * The index over the program lists and cuts.
	 */
	ProgramTable programs;
	/**
	 * The top of the tree as an octree, grown before the workers start.
	 * A cell that is a subtree's root is a branch whose payload names
	 * its seed.
	 */
	Subtree top;
	/**
	 * The subtrees' roots, in the order they are handed out.
	 */
	std::vector<Sprout> seeds;
	/**
	 * How deep the seeds stand, even.
	 */
	int seedDepth;
	/**
	 * Each placed subtree's root record, matching the seeds.
	 */
	std::vector<RootRecord> roots;
	/**
	 * The box the tree covers.
	 */
	Region region;
	/**
	 * How many threads build.
	 */
	int workers;
};

/**
 * Build a scene's tree, streaming: each subtree is packed and written
 * as it finishes.
 *
 * The top is grown level by level until an even level holds at least
 * 128 seeds, however many workers there are. Workers take seeds in
 * order through an atomic counter; each grows its seed, runs the grown
 * passes over it and packs it. Under one lock, in the order the seeds
 * were handed out, each finished subtree is placed, put to the placed
 * passes and written at the tree's end, and its root record kept. Last
 * the top is packed, copying the root records into its rows. The tree
 * is the same whatever the number of workers. Blocks of 1 MB and more
 * are mapped on their own (largeBlocksMapped()), and the heap's free
 * memory is handed back (releasedFreeMemory()) once the top is grown and
 * after every 64 finished subtrees, outside the lock.
 *
 * @param input The scene, the Detail, the passes and the region.
 */
Tree buildTree(const BuildInput& input);

/**
 * Hand the memory the heap holds free back to the system: malloc_trim
 * where glibc is the C library, nothing elsewhere.
 *
 * A build frees far more than it keeps, spread over its workers' heaps,
 * and glibc keeps those pages; what is allocated after it would come on
 * top of them.
 */
void releasedFreeMemory();

/**
 * Have every block of 1 MB and more mapped on its own, and unmapped as
 * soon as it is freed, where glibc is the C library (mallopt with
 * M_MMAP_THRESHOLD, which also stops glibc from raising the threshold as
 * large blocks are freed); nothing elsewhere.
 *
 * The build's large blocks (the tree's growing arrays, a subtree's
 * arrays while it is packed) would otherwise sit in the workers' arenas
 * between long-lived ones, and the arenas could not shrink after them.
 */
void largeBlocksMapped();

/**
 * Make a build's context and grow the top of its tree, each level's
 * cells planned by the workers at once.
 *
 * @param input What the build is asked for.
 */
BuildContext buildContext(const BuildInput& input);

/**
 * Grow one seed, run the grown passes over it and pack it.
 *
 * @param context The build's context.
 * @param seed The subtree's root cell.
 */
Subtree finishedSubtree(const BuildContext& context, const Sprout& seed);

/**
 * Turn a packed subtree's indices into the tree's, as though it were
 * written at the tree's end, and store each of its cuts and program
 * lists in the tree once.
 *
 * A cut or a list alike to one the tree holds names that one. The
 * subtree that comes back names the tree's lists and holds none of its
 * own; its nodes, leaves and runs are still to be written.
 *
 * Mutable references, as rule 46 allows for build-time accumulators.
 *
 * @param tree The tree so far.
 * @param subtree The packed subtree.
 * @param table The index over the tree's program lists and cuts.
 */
Subtree placedSubtree(Tree& tree, const Subtree& subtree, ProgramTable& table);

/**
 * Write a placed subtree's nodes, leaves and runs at the tree's end and
 * return its root record.
 *
 * A mutable reference, as rule 46 allows for build-time accumulators.
 *
 * @param tree The tree so far.
 * @param placed The placed subtree.
 */
RootRecord writtenSubtree(Tree& tree, const Subtree& placed);

/**
 * Find the cube a scene's geometry lies in, out to the render's reach.
 *
 * It is searched with interval bounds in shrinking passes from a cube
 * of twice the reach about the camera's target, and widened by a
 * hundredth. Each pass hands the six sides' searches, split at the
 * second level, to the workers; a side's searches share their best,
 * which is the least over the side's occupied cells whatever the order.
 * The pass's cube and its eight octants are cut by the workers first,
 * so that no two searches cut the same octant at once.
 *
 * @param scene The scene's geometry.
 * @param shot The scene's shot: the camera's target and the render's
 *        reach.
 * @param workers How many threads search.
 */
Region sceneRegion(const Scene& scene, const Shot& shot, int workers);

/**
 * Return the deepest a cell may stand, from the least tolerance: the
 * depth at which a cell is four least
 * tolerances wide along the region's longest side, to the nearest.
 *
 * @param region The box the tree covers.
 * @param least The least tolerance.
 */
int depthCapOf(const Region& region, double least);

}
