#pragma once

#include "tree/grow.h"
#include "tree/patch.h"
#include "tree/tree.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * Whether the build keeps face slabs: false when the build turns
 * build_correct_face_slabs off (CMake's `-DOFF`), and the tree then
 * carries no slabs and every face is solved.
 */
#ifdef KODANUKI_OFF_BUILD_CORRECT_FACE_SLABS
constexpr bool faceSlabsBuilt = false;
#else
constexpr bool faceSlabsBuilt = true;
#endif

/**
 * How far past each side of its cell a slab holds, in the cell's unit
 * coordinates.
 */
constexpr double slabReach = 1e-3;

/**
 * A plane a face keeps to over its cell, and how far it strays.
 *
 * Wherever the face is 0 in the cell, or within slabReach of it, the
 * plane reads within the stray of 0, and where the plane reads further
 * off on one side the face has the plane's sign. The face is scaled
 * first by a power of two so that the sum of the sizes of its
 * coefficients is at most 1, which changes no sign: the slab and the
 * walk's float error both count in that scale. It is kept in half
 * floats, rounded so that what is read back is still a bound.
 */
struct Slab {
	/**
	 * How fast the plane rises along u, v and w.
	 */
	float slope[3];
	/**
	 * What the plane reads at the cell's lower corner.
	 */
	float offset;
	/**
	 * How far the face strays from the plane, at most.
	 */
	float stray;
};

/**
 * A leg through a cell as the faces' slabs are asked about it.
 */
struct SlabLeg {
	/**
	 * Where the leg is a little before it enters, at the start of the
	 * window the leaf searches crossings in.
	 */
	float entry[3];
	/**
	 * Where the leg is a little after it leaves, at the end of that
	 * window.
	 */
	float leave[3];
	/**
	 * How far, in a slab's scale, the float maths of a face along the
	 * leg and of the slab test may be out.
	 */
	float margin;
	/**
	 * Whether the window stays within the slabs' reach of the cell, so
	 * that they hold along it; the slabs are not asked where it does not.
	 */
	bool held;
};

/**
 * Give every face of a grown subtree that is not a plane its slab: the
 * Build pass member FaceSlabs.
 *
 * Each face run with a face that is not a plane is written again,
 * marked slabbed, with the slab of each such face after its words, as
 * slabWords() keeps it. The slab is found from the face's words alone,
 * as the walk reads them. A run of planes alone is kept as it is. The
 * faces are not changed, so every hit stays the same to the bit; each
 * slab costs slabWordCount words.
 *
 * @param subtree The grown subtree.
 */
Subtree faceSlabs(const Subtree& subtree);

/**
 * Return the slab of a triquadratic face over its cell widened by
 * slabReach.
 *
 * The slope is the face's at the middle of the cell, rounded to half
 * floats first; what is left once that plane is taken off is bounded by
 * its Bernstein net over the widened cell.
 *
 * @param face The face, inside where it is 0 or less.
 */
Slab triquadraticSlab(const Triquadratic& face);

/**
 * Return the slab of a quadric face, the ten floats FaceKind::Quadric
 * keeps.
 *
 * @param face The face's first float.
 */
Slab quadricSlab(const float* face);

/**
 * Return the slab of a patch face, the slab of its triquadratic.
 *
 * @param patch The patch, as the tree keeps it.
 */
Slab patchSlab(const Patch& patch);

/**
 * Return the slab of a ring face, the fourteen floats FaceKind::Ring
 * keeps.
 *
 * The plane is the ring's quartic's own at the middle of the cell; what
 * is left once it is taken off is bounded by the quartic's Hessian, an
 * interval over the widened cell.
 *
 * @param face The face's first float.
 */
Slab ringSlab(const float* face);

/**
 * Return the three words a slab is kept in: the slopes, the offset and
 * the stray in half floats, the last half 0.
 *
 * @param slab The slab, as one of the slab functions returns it.
 */
std::array<std::uint32_t, 3> slabWords(const Slab& slab);

/**
 * Read a slab back from its three words.
 *
 * @param words The slab's first word.
 */
Slab readSlab(const std::uint32_t* words);

/**
 * Return a slab that never answers: its stray is infinite.
 */
Slab neverSlab();

/**
 * Return a leg as the faces' slabs are asked about it.
 *
 * The window runs from s = -2e-4 to 1 + 2e-4, past the slack the leaf
 * searches crossings in on either side. The margin grows with the
 * fourth power of how far the window reaches from the cell's corner,
 * as the float error of a quartic along it does.
 *
 * @param base Where the leg enters, in the cell's coordinates.
 * @param rate How far it moves across the cell.
 */
SlabLeg slabLegOf(const float* base, const float* rate);

/**
 * Return a leg the slabs are never asked about.
 */
SlabLeg unheldSlabLeg();

/**
 * Return which side of 0 one face keeps to along a whole leg, by its
 * slab alone: 1 outside, -1 inside, and 0 where the slab cannot tell or
 * the leg is not held.
 *
 * Where it answers, the face as the leaf's solve restricts it has that
 * sign everywhere in the window its crossings are searched in, so it
 * has no crossing there.
 *
 * @param leg The leg, as slabLegOf() returns it.
 * @param slab The slab's first word.
 */
int slabSide(const SlabLeg& leg, const std::uint32_t* slab);

}
