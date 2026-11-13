#pragma once

#include "plugins/geometry/tree/cut.h"
#include "sdf/numbers.h"
#include "sdf/program.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace kodanuki {

/**
 * A smooth polynomial that stands in for a field over one cell.
 *
 * P = T(u, v, w) + u² A(v, w) + v² B(u, w) + w² C(u, v) in the cell's
 * unit coordinates from 0 to 1, T trilinear and A, B and C bilinear.
 * Its twenty coefficients are pinned at the cell's twenty nodes, the
 * eight corners and the twelve edge middles. The eight nodes on a face
 * settle the patch on that face alone, so two cells of one size that
 * share a face read the same surface on it, and no ray falls between
 * them.
 */
struct Patch {
	/**
	 * The coefficients: the eight of T, indexed with u in bit 0, v in
	 * bit 1 and w in bit 2, then A, B and C, four each, indexed the same
	 * way over the two axes each spans.
	 *
	 * They are what the tree keeps: whole steps of the step, each the
	 * float a whole number of steps rounds to.
	 */
	float weights[20];
	/**
	 * The step the coefficients are counted in, 0 where every
	 * coefficient is.
	 */
	float step;
	/**
	 * Whether any of A, B or C is not 0. A flat patch keeps only T.
	 */
	bool bent;
};

/**
 * A patch and how far it was seen to stray from its field.
 */
struct Fitted {
	/**
	 * The patch, as the tree keeps it.
	 */
	Patch patch;
	/**
	 * The largest gap between the patch and the field at the fifteen
	 * points away from the nodes: the middles of the six faces, of the
	 * eight octants and of the cell.
	 */
	double stray;
};

/**
 * A polynomial of at most the second degree in each of u, v and w.
 *
 * A patch is of this form, and so is a quadric face.
 */
struct Triquadratic {
	/**
	 * The coefficient of u^i v^j w^k at [i][j][k].
	 */
	std::array<std::array<std::array<double, 3>, 3>, 3> terms;
};

/**
 * The field a patch is fitted to.
 */
struct PatchQuery {
	/**
	 * The programs whose minimum is the field: a cell's programs, or
	 * one face's program.
	 */
	const std::vector<std::shared_ptr<const Program>>& programs;
	/**
	 * The cell.
	 */
	Box box;
};

/**
 * A field that is the minimum or the maximum of two others over one
 * cell: the two sides of a seam.
 */
struct SeamQuery {
	/**
	 * The whole field at the twenty nodes and the fifteen points the
	 * stray is read at, as fieldReadings reads it.
	 */
	const std::array<double, 35>& whole;
	/**
	 * The programs whose minimum is the first side.
	 */
	const std::vector<std::shared_ptr<const Program>>& left;
	/**
	 * The programs whose minimum is the second side.
	 */
	const std::vector<std::shared_ptr<const Program>>& right;
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * Whether the whole field is the larger of its sides, as an
	 * intersection or a cut is, rather than the smaller, as a union is.
	 */
	bool larger;
};

/**
 * A patch on each side of a seam and how far the pair strays.
 */
struct SeamFit {
	/**
	 * The patch fitted to the first side.
	 */
	Patch left;
	/**
	 * The patch fitted to the second side.
	 */
	Patch right;
	/**
	 * How far the pair strays: the largest gap, at the fifteen points
	 * away from the nodes, between each patch and its own side, and
	 * between the whole field and the smaller or larger of the two.
	 */
	double stray;
};

/**
 * The points a fit reads a field at over one cell.
 */
struct Readings {
	/**
	 * The twenty nodes, then the fifteen points the stray is read at,
	 * in world coordinates.
	 */
	double points[35][3];
};

/**
 * Fit a patch through a field's twenty nodes and say how far it
 * strays.
 *
 * The twenty readings turn into the twenty coefficients by one matrix
 * that depends on the unit cube alone. The patch is rounded to what the
 * tree keeps before the stray is read, so the stray is that of what
 * the walk meets.
 *
 * @param query The field and the cell.
 */
Fitted fitPatch(const PatchQuery& query);

/**
 * Read a field at the twenty nodes of a cell, then at the fifteen
 * points its stray is read at.
 *
 * @param query The field and the cell.
 */
std::array<double, 35> fieldReadings(const PatchQuery& query);

/**
 * Fit a patch to a field's readings and say how far it strays, as
 * fitPatch does.
 *
 * @param values The field at the points fieldReadings reads.
 */
Fitted fittedTo(const std::array<double, 35>& values);

/**
 * Fit one patch to each side of a seam and say how far the pair strays
 * from the whole field.
 *
 * Where two shapes meet, the field has a crease that no one patch
 * follows better than the cell's width, while each side is smooth on
 * its own. Each patch is pinned at the nodes a neighbour shares, as a
 * single patch is. Each side must hold on its own as well as in the
 * join, since the fifteen points cannot see a side that strays only
 * between them where it decides the field.
 *
 * @param query The whole field, its two sides, the cell and the join.
 */
SeamFit fitSeam(const SeamQuery& query);

/**
 * Return the points a fit reads a field at over a cell.
 *
 * @param box The cell.
 */
Readings patchReadings(const Box& box);

/**
 * Return a patch's value at a point of its cell.
 *
 * @param patch The patch.
 * @param u The point in the cell's unit coordinates, from 0 to 1.
 */
double patchValue(const Patch& patch, const double* u);

/**
 * Return a patch as a triquadratic in its cell's unit coordinates.
 *
 * @param patch The patch.
 */
Triquadratic patchTriquadratic(const Patch& patch);

/**
 * Return a range holding every value a triquadratic takes over the unit
 * cube.
 *
 * The triquadratic is written in the Bernstein basis over the cube, and
 * the range is the least and the most of its 27 coefficients.
 *
 * @param polynomial The triquadratic.
 */
math::Interval triquadraticNet(const Triquadratic& polynomial);

/**
 * Return a range holding every value a patch takes over its cell, the
 * net of its triquadratic.
 *
 * @param patch The patch.
 */
math::Interval patchNet(const Patch& patch);

/**
 * Return the words a patch face is kept in.
 *
 * The step comes first as a float whose sign bit says whether the
 * patch bends, then the coefficients as 16-bit whole steps two to a
 * word (eight, or twenty when it bends). A flat patch takes five
 * words, a bent one eleven.
 *
 * @param patch The patch.
 */
std::vector<std::uint32_t> patchWords(const Patch& patch);

/**
 * Read a patch face back from its words.
 *
 * @param words The face's first word.
 */
Patch readPatch(const std::uint32_t* words);

/**
 * Return how many words a patch face takes, read from its first word.
 *
 * @param words The face's first word.
 */
std::uint32_t patchWordCount(const std::uint32_t* words);

}
