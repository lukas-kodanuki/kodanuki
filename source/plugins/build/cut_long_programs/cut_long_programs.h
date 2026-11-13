#pragma once

#include "plugins/geometry/tree/cut.h"
#include "plugins/geometry/tree/grow.h"

namespace kodanuki {

/**
 * Name each leaf's object programs by cuts to its cell or to a cell
 * above it, as lists of the whole programs' steps they keep.
 *
 * The grown octree is walked from its root. Each cell cuts its parent's
 * cut of each object to its box: the interval of every kept step over
 * the box is found, a join whose sides are apart over it (a minimum or
 * maximum whose intervals do not touch, a decided select) is rerouted
 * to the deciding side, and the steps nothing reads any more are left
 * out. A smooth minimum is never rerouted: beyond its blend its
 * gradient is the sum of its sides' weighted by one and zero, which
 * differs from the deciding side's in the sign of a zero. The kept
 * steps are run in the whole program's numbering, so a cut answers
 * exactly what the whole does anywhere in the cell, to the bit, and
 * holds four bytes a run of consecutive kept steps.
 *
 * A cell keeps its own cut of an object only where it keeps at most
 * cutShare() of the steps of the cut it was made from, its parent's
 * (the whole program at the root); otherwise it holds on to its
 * parent's, which is valid in every cell below. Each leaf names the cut
 * it holds of each of its objects, the whole program where nothing
 * above it cut that object. Every object is cut, however short, except
 * one of more steps than a cut can number. The leaves' program lists
 * are written anew from the objects they name.
 *
 * @param context What every cell reads, for the whole programs.
 * @param subtree The grown subtree.
 * @param programs The programs the subtree's root cuts.
 */
Subtree cutLongPrograms(const GrowContext& context, const Subtree& subtree, const CellPrograms& programs);

/**
 * Return the most a cell's cut may keep of the steps of its parent's
 * cut for the cell to hold its own.
 */
double cutShare();

}
