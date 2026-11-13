#pragma once

#include "plugins/geometry/tree/cut.h"
#include "plugins/geometry/tree/grow.h"

namespace kodanuki {

/**
 * Split every leaf of a grown subtree that is fitted to its whole field,
 * has a neighbour more than one level finer, and strays from the field
 * between the points its fit reads.
 *
 * A leaf is fitted to its whole field where it is one patch or a seam
 * pair. Its neighbours are found by probing a thousandth of its half
 * width past the middle and four quarter points of each face; a probe
 * outside the subtree is not read, so the subtree's border is left as
 * grown. Such a leaf splits where the field and the fit differ by more
 * than the cell's tolerance at a point of a five by five grid over each
 * face or of a four by four by four grid inside it, 162 points in all.
 * Its eight children are grown as the grow grows any cell, from the
 * programs the grow cut the leaf to. A split may leave the leaves
 * around it with a finer neighbour in turn, so the leaves are looked at
 * again, at most 32 rounds. A leaf at the depth cap is left alone.
 *
 * @param context What every cell reads.
 * @param subtree The grown subtree.
 * @param programs The programs the subtree's root cuts.
 */
Subtree splitBesideFiner(const GrowContext& context, const Subtree& subtree, const CellPrograms& programs);

}
