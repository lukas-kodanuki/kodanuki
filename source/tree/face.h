#pragma once

#include "sdf/program.h"
#include "tree/cut.h"
#include "tree/quadric.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kodanuki {

/**
 * The most faces a surface leaf holds.
 */
constexpr std::size_t mostFaces = 8;

/**
 * How many words a truth table of the most faces takes.
 */
constexpr std::size_t tableWords = 8;

/**
 * The owner of a face no one program of the cell gives alone.
 */
constexpr std::uint32_t noOwner = 0xFFFFFFFFu;

/**
 * A torus a face is exactly, in the cell's own coordinates.
 *
 * The ring's frame is an affine map of the cell, scaled so the major
 * radius is 1, so a ring under any placement, stretched or not, is
 * one: its surface is the image of the frame's ring.
 */
struct Ring {
	/**
	 * Where the cell's lower corner lies in the ring's frame.
	 */
	double origin[3];
	/**
	 * How far the frame moves along a whole side of the cell, for u, v
	 * and w in turn.
	 */
	double axes[3][3];
	/**
	 * The minor radius, as a share of the major one, between 0 and 1.
	 */
	double minor;
	/**
	 * Whether the face is inside outside the ring, as a ring cut away is.
	 */
	bool turned;
};

/**
 * One smooth function whose sign may change in a cell: a piece of a
 * program or the condition a - b of a crease left in it.
 */
struct Face {
	/**
	 * The piece or the condition, as a program of its own.
	 */
	Program program;
	/**
	 * Its Taylor quadric at the cell's centre, in the cell's centred
	 * unit coordinates.
	 */
	Quadric quadric;
	/**
	 * A bound on how far the quadric strays from the program over the
	 * cell, in the program's units, or infinity where it is not proven.
	 */
	double bound;
	/**
	 * How far one unit of the program is in world units at the face,
	 * which turns the tolerance into the program's units.
	 */
	double scale;
	/**
	 * Whether the bound is proven within the cell's tolerance.
	 */
	bool proven;
	/**
	 * The torus the piece is exactly, where it is the distance to one:
	 * length([length([a, b]) - R, c]) - r for affine a, b and c, scaled,
	 * negated or offset by constants, with R > r > 0. It is read from
	 * the program's steps alone.
	 */
	std::optional<Ring> ring;
	/**
	 * Whether pieces of more than one leaf, or a merged face, stand
	 * behind it, so that its leaf is not its program's alone.
	 */
	bool shared;
	/**
	 * Which of the cell's programs the face comes from, by its place in
	 * the cell's list, or noOwner where several give it.
	 */
	std::uint32_t owner;
};

/**
 * The faces of a cell and their truth table.
 */
struct Faces {
	/**
	 * The faces the table depends on.
	 */
	std::vector<Face> faces;
	/**
	 * The truth table over the faces' insides, as RunHeader describes.
	 */
	std::array<std::uint32_t, tableWords> table;
	/**
	 * How many candidate faces the gather found before the table
	 * dropped any.
	 */
	std::size_t candidates;
	/**
	 * Whether the gather gave up: more candidates or visits than it
	 * takes.
	 */
	bool crowded;
	/**
	 * Whether the floor trim fixed a face's sign to the centre's, which
	 * makes the leaf a floor leaf.
	 */
	bool trimmed;
};

/**
 * What the face derivation is told about a cell.
 */
struct Cell {
	/**
	 * The cell's programs, cut to it.
	 */
	const std::vector<std::shared_ptr<const Program>>& programs;
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * How far a face may stray, in world units.
	 */
	double tolerance;
};

/**
 * Gather a cell's faces and table without proving them.
 *
 * Every crease left in the cut programs splits the cell into smooth
 * pieces; the pieces and the conditions whose sign can change in the
 * cell are the candidates, and faces the table does not depend on, over
 * the sign patterns a point of the cell can have, are dropped.
 *
 * @param cell The cell.
 */
Faces gatheredFaces(const Cell& cell);

/**
 * Gather a cell's faces as gatheredFaces does, then merge and trim them
 * to the most a leaf holds, as a cell at the floor does.
 *
 * @param cell The cell.
 */
Faces mergedFaces(const Cell& cell);

/**
 * Prove each gathered face's quadric within the cell's tolerance.
 *
 * Every face of a cell of at most mostFaces faces is put to its proof,
 * so that each says whether it holds.
 *
 * @param faces The gathered faces.
 * @param cell The cell.
 */
Faces provedFaces(const Faces& faces, const Cell& cell);

/**
 * Gather and prove a cell's faces.
 *
 * @param cell The cell.
 */
Faces cellFaces(const Cell& cell);

/**
 * Return whether a cell is inside for one pattern of its faces'
 * insides.
 *
 * @param faces The faces and their table.
 * @param bits The pattern, face j inside where bit j is set.
 */
bool tableInside(const Faces& faces, std::uint32_t bits);

}
