#pragma once

#include "tree/cut.h"
#include "tree/face.h"
#include "tree/patch.h"
#include "tree/tree.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What classifying one cell is told.
 */
struct CellQuery {
	/**
	 * The parent cell's programs, which the cell cuts.
	 */
	const CellPrograms& programs;
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * How far the cell's surface may stray, from Detail.
	 */
	double tolerance;
	/**
	 * Whether the cell may still split, false at the depth cap.
	 */
	bool deeper;
	/**
	 * Whether derived faces are tried. Off, every surface cell is a
	 * patch or a seam pair of patches.
	 */
	bool faces;
};

/**
 * What one cell was found to hold, before it is written into a
 * subtree.
 */
struct Plan {
	/**
	 * What the cell holds; a branch must split.
	 */
	CellKind kind;
	/**
	 * The cell's programs, cut to it. A branch's children cut these; a
	 * filled cell keeps the object that fills it.
	 */
	CellPrograms programs;
	/**
	 * The face run of a surface cell, empty for any other.
	 */
	std::vector<std::uint32_t> run;
	/**
	 * The patch over the whole cell where the run is that patch, which
	 * Detail's verdict reads.
	 */
	Fitted fitted;
	/**
	 * Whether the run is one patch over the whole cell.
	 */
	bool patched;
	/**
	 * Whether the run is a seam pair: a patch over the whole cell for
	 * each side of a seam, under their union or intersection.
	 */
	bool seamed;
	/**
	 * Whether every face is held within the tolerance.
	 */
	LeafKind leafKind;
	/**
	 * How far the cell's surface, or a neighbouring patch of the same
	 * tolerance, may stray from the field: the cell's tolerance; 0 from
	 * classifyCell, set by the grow.
	 */
	double loosened;
};

/**
 * A face run written from a cell's faces, and whether it holds.
 */
struct FaceRun {
	/**
	 * Whether every face is held within the tolerance: a quadric
	 * proven, a patch face read within it at the fit's points.
	 */
	bool held;
	/**
	 * The run's words, as RunHeader describes.
	 */
	std::vector<std::uint32_t> words;
};

/**
 * What writing a face run from derived faces needs.
 */
struct RunQuery {
	/**
	 * The faces and their table.
	 */
	const Faces& faces;
	/**
	 * The cell's cut programs, for the words a Build pass member writes
	 * after the run's table (tree/pass.h).
	 */
	const std::vector<std::shared_ptr<const Program>>& programs;
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * How far a face may stray.
	 */
	double tolerance;
};

/**
 * Find what one cell holds.
 *
 * The cell's programs are cut to it; an empty or filled cut is a leaf
 * of that kind. Otherwise the derived faces are tried: a table that
 * says one sign is an empty or filled leaf, and at most eight faces
 * that all hold are a surface leaf of faces. A cell at the depth cap
 * whose faces are at most eight keeps them as a floor leaf. Any other
 * cell is fitted with one patch over its whole field. Where that
 * strays over the tolerance and the field is a seam, the minimum of two
 * or more objects or one object's outermost hard join, each side is
 * fitted with a patch of its own (fitSeam), and the pair, a run of two
 * patch faces whose table is their union or intersection, is kept where
 * it strays less. The patch or the pair is a surface leaf where it
 * strays within the tolerance or the cell may not split, and a branch
 * otherwise. Detail's verdict is asked by the caller.
 *
 * @param query The cell, its parent's programs and its tolerance.
 */
Plan classifyCell(const CellQuery& query);

/**
 * Write a face run from a cell's derived faces.
 *
 * A proven face whose quadric has no second order terms is a plane,
 * any other proven face a quadric. A face that is not proven is fitted
 * with a patch face through its own program's nodes, which holds where
 * it strays within the tolerance. A run of one face that is inside
 * where its face is outside has the face turned over, so that it needs
 * no table.
 *
 * @param query The faces, the cell and the tolerance.
 */
FaceRun facesRun(const RunQuery& query);

/**
 * Write the face run of one patch over a whole cell: one patch face
 * and no table.
 *
 * @param fitted The patch.
 * @param tolerance How far the patch may stray.
 */
FaceRun patchRun(const Fitted& fitted, double tolerance);

/**
 * Return a surface leaf's run with the quadric faces folded into its
 * table that keep one sign over the whole cell.
 *
 * A quadric face keeps its sign where an interval bound of its float
 * coefficients over the cell, widened past the cell by more than the
 * walk's root slack, stays off 0 by a margin far over the float error
 * of the walk's quadratic: the walk then finds no crossing of it and
 * reads the same sign wherever it asks, so the walk meets the same bits
 * without it. The face's bit is fixed in the table, the other faces
 * keep their order, and the needed faces are those of the new table. A
 * run keeps at least two faces; one that would keep fewer is returned
 * as it is.
 *
 * @param run The run's words.
 */
std::vector<std::uint32_t> foldedRun(const std::vector<std::uint32_t>& run);

/**
 * Return what a run fitted to its cell's whole field reads at a point:
 * its one patch, or the larger of a seam pair's two patches where their
 * table is the intersection and the smaller where it is the union.
 *
 * @param run The run's first word: one patch, or a seam pair.
 * @param u The point in the cell's own coordinates, 0 to 1.
 */
double fittedRunValue(const std::uint32_t* run, const double* u);

}
