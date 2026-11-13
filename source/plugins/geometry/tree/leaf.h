#pragma once

#include "plugins/geometry/tree/tree.h"

#include <array>
#include <cstdint>

namespace kodanuki {

/**
 * How far past either end of a leg a crossing still counts, in the
 * leg's own length, and how close two crossings are a tie.
 */
constexpr float rootSlack = 1e-4f;

/**
 * How far, relative to the sum of a quartic's coefficient sizes, its
 * Bernstein coefficients over the root window must keep from 0 for its
 * root search to be skipped: more than the float evaluation of the
 * quartic and of the coefficients can err.
 */
constexpr float signSlack = 1e-5f;

/**
 * The part of a ray inside one cell, in the cell's own coordinates.
 *
 * The cell is the unit cube, and the ray is u = base + rate s for s
 * from 0 where it enters the cell to 1 where it leaves, so the numbers
 * are the size of the cell and not of the ray's journey.
 */
struct Leg {
	/**
	 * Where the ray enters, in the cell's coordinates.
	 */
	float base[3];
	/**
	 * How far it moves across the cell.
	 */
	float rate[3];
};

/**
 * A polynomial in s, one face restricted to a leg.
 */
struct Quartic {
	/**
	 * The coefficient of s to the fourth.
	 */
	float fourth;
	/**
	 * The coefficient of s cubed.
	 */
	float cube;
	/**
	 * The coefficient of s squared.
	 */
	float square;
	/**
	 * The coefficient of s.
	 */
	float linear;
	/**
	 * The constant term.
	 */
	float constant;
};

/**
 * A place along a leg where one face's inside begins or ends.
 */
struct Crossing {
	/**
	 * Where along the leg.
	 */
	float at;
	/**
	 * Which face of the run.
	 */
	std::uint32_t face;
};

/**
 * Every crossing of a leg through a leaf's faces, in order.
 */
struct Crossings {
	/**
	 * The crossings, at most four a face.
	 */
	std::array<Crossing, 32> items;
	/**
	 * How many there are.
	 */
	int count;
	/**
	 * Whether a face the table needs inside is never inside along the
	 * leg, so that the leg meets nothing and the crossings were not all
	 * found.
	 */
	bool blocked;
};

/**
 * A face run read apart once for one leaf visit.
 */
struct LeafRun {
	/**
	 * The run's first word.
	 */
	const std::uint32_t* words;
	/**
	 * The run's header.
	 */
	RunHeader header;
	/**
	 * Where each face begins, from the run's first word, and after the
	 * last face where the run ends.
	 */
	std::uint32_t offsets[9];
};

/**
 * What solving one surface leaf needs.
 */
struct LeafVisit {
	/**
	 * The leaf's face run.
	 */
	const LeafRun& run;
	/**
	 * The ray's part in the leaf's cell.
	 */
	Leg leg;
	/**
	 * Whether the ray looks for the surface where the table turns
	 * outside.
	 */
	bool inside;
};

/**
 * Every face of a leaf restricted to one leg.
 */
struct Restricted {
	/**
	 * Each face along the leg, inside where it is below 0; a face its
	 * slab keeps to one side is the constant 1 or -1 of that side.
	 */
	std::array<Quartic, 8> faces;
	/**
	 * How many faces there are.
	 */
	std::uint32_t count;
};

/**
 * The places along a leg where a ray from inside crosses faces and stays
 * inside: where it may pass from one solid into another.
 */
struct Passings {
	/**
	 * Where along the leg, in order.
	 */
	std::array<float, 32> at;
	/**
	 * How many there are.
	 */
	int count;
};

/**
 * A leaf's faces restricted to one leg and their crossings.
 */
struct SolvedFaces {
	/**
	 * The faces along the leg; only those solved where the crossings are
	 * blocked.
	 */
	Restricted restricted;
	/**
	 * The crossings in order, or blocked.
	 */
	Crossings crossings;
};

/**
 * Where a leg met a leaf's surface.
 */
struct Landing {
	/**
	 * Whether it met it at all.
	 */
	bool found;
	/**
	 * Where along the leg, from 0 to 1.
	 */
	float at;
	/**
	 * For a ray from inside, the passings before the landing, or along
	 * the whole leg where it met nothing.
	 */
	Passings passings;
	/**
	 * The face it landed on, or -1 where it started inside or another
	 * crossing lies within rootSlack of it.
	 */
	int face;
};

/**
 * The roots of a quartic in a leg's window, in order.
 */
struct WindowRoots {
	/**
	 * The roots.
	 */
	float at[4];
	/**
	 * How many there are.
	 */
	int count;
};

/**
 * Read a face run apart.
 *
 * @param words The run's first word.
 */
LeafRun leafRun(const std::uint32_t* words);

/**
 * Find where a leg first meets a leaf's surface.
 *
 * A face of a slabbed run that its slab keeps to one side along the
 * leg (plugins/geometry/tree/pass.h) is not solved and has that side's sign throughout.
 * Every other face is restricted to the leg and its crossings are
 * found: a plane's by one division, a quadric's as one quadratic in the stable
 * form, and a patch's or a ring's as the roots of its quartic between
 * those of its slope, unless the quartic's Bernstein coefficients over
 * the window all keep one sign by more than its evaluation can err, so
 * that it has no root there. A face the table needs inside that is never
 * inside along the leg ends the search at once. The signs before the first crossing are read in the widest
 * gap between crossings and unwound, so a crossing just before the cell
 * is not counted twice. The crossings are walked in order until the
 * table turns inside; of crossings within the slack, the first face
 * that turns it wins. For a ray from inside, crossings within the slack
 * of one another are a group, crossed together: the ray leaves where a
 * group turns the table outside, at the first of its crossings that
 * does, and a group that changes the signs but keeps the table inside is
 * a passing, as where two solids touch.
 *
 * @param visit The leaf and the leg.
 */
Landing landLeaf(const LeafVisit& visit);

/**
 * Restrict a leaf's faces to a leg and find their crossings, in order,
 * within a slack of the leg's ends.
 *
 * Each face is restricted in a fixed order of operations, which the
 * packet walk keeps, so that both meet the same bits; a face its slab
 * keeps to one side along the leg is left as the constant of that side.
 * For a ray from
 * outside the faces the table needs inside are solved first, and the
 * first of them that is never inside along the leg ends the search with
 * the crossings blocked; the other faces are then left unrestricted.
 *
 * @param visit The leaf and the leg.
 */
SolvedFaces solvedFaces(const LeafVisit& visit);

/**
 * Return whether a leg from outside meets a leaf's surface anywhere,
 * without finding where.
 *
 * It answers what landLeaf() finds for a ray from outside: the faces
 * are solved and the crossings put in order the same way, and the
 * crossings are walked until the table turns inside; no tie is
 * searched.
 *
 * @param visit The leaf and a leg from outside.
 */
bool reachesSurface(const LeafVisit& visit);

/**
 * Return a patch's or a ring's crossings of a leg as the leaf solve finds
 * them: the roots of its quartic in the window of the leg's ends and
 * their slack.
 *
 * @param quartic The face along the leg.
 */
WindowRoots quarticWindowRoots(const Quartic& quartic);

/**
 * Put crossings in order: by where along the leg, then by face.
 *
 * @param crossings The crossings.
 */
void sortCrossings(Crossings& crossings);

/**
 * Return the widest gap between crossings in order, gap k lying before
 * crossing k and the last after them all; of gaps as wide, the first.
 *
 * @param crossings The crossings, in order.
 */
int widestGap(const Crossings& crossings);

/**
 * Return the middle of a gap between crossings.
 *
 * @param crossings The crossings, in order.
 * @param gap The gap.
 */
float gapMiddle(const Crossings& crossings, int gap);

/**
 * Return face signs with the faces of the first crossings flipped back,
 * the signs before them.
 *
 * @param crossings The crossings, in order.
 * @param bits The signs after them.
 * @param count How many crossings to undo.
 */
std::uint32_t unwound(const Crossings& crossings, std::uint32_t bits, int count);

/**
 * Walk a leg's crossings in order from the signs before the first, for a
 * ray from outside, and land at the first crossing that turns the table
 * inside, the least face of crossings tied within the slack winning.
 *
 * @param visit The leaf and the ray's side.
 * @param crossings The crossings, in order.
 * @param bits The signs before the first crossing.
 */
Landing walkedCrossings(const LeafVisit& visit, const Crossings& crossings, std::uint32_t bits);

/**
 * Walk a leg's crossings in order from the signs before the first, for a
 * ray from inside, grouping crossings within the slack: leave at the
 * first group that turns the table outside, at the first of its
 * crossings that does, and pass where a group changes the signs but keeps
 * the table inside.
 *
 * @param visit The leaf and the ray's side.
 * @param crossings The crossings, in order.
 * @param bits The signs before the first crossing.
 */
Landing leftCrossings(const LeafVisit& visit, const Crossings& crossings, std::uint32_t bits);

/**
 * Return a landing at a place along a leg, at 0 where the place is
 * before the leg.
 *
 * @param at The place.
 */
Landing landedAt(float at, int face);

/**
 * Return whether a run's table holds a pattern of face signs inside.
 *
 * @param run The run.
 * @param bits The signs, face j's bit set where it is below 0.
 */
bool runInside(const LeafRun& run, std::uint32_t bits);

/**
 * Return a quartic's value at a place along a leg.
 *
 * @param quartic The quartic.
 * @param s The place.
 */
float quarticValue(const Quartic& quartic, float s);

}
