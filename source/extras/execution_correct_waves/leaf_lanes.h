#pragma once

#include "extras/execution_correct_waves/packet.h"
#include "walk/leaf.h"

#include <array>
#include <simd>

namespace kodanuki {

/**
 * One float for each lane of a packet.
 */
using Lanes = std::simd::vec<float, packetWidth>;

/**
 * The legs of a packet's lanes through one cell, as leaf.h's Leg, one
 * lane each.
 */
struct LegLanes {
	/**
	 * Where each lane enters, in the cell's coordinates.
	 */
	Lanes base[3];
	/**
	 * How far each lane moves across the cell.
	 */
	Lanes rate[3];
};

/**
 * A leaf's run and the legs of the lanes that ask it.
 */
struct LeafLanes {
	/**
	 * The leaf's face run.
	 */
	const LeafRun& run;
	/**
	 * Each lane's leg.
	 */
	const LegLanes& legs;
	/**
	 * Which lanes ask, one bit each.
	 */
	unsigned lanes;
	/**
	 * Which of them are rays from inside.
	 */
	unsigned inside;
};

/**
 * Return each lane's a divided by its b.
 *
 * @param a The dividends.
 * @param b The divisors.
 */
Lanes laneQuotient(Lanes a, Lanes b);

/**
 * What a leaf solve answers for the lanes it settles at once.
 */
struct LaneVerdicts {
	/**
	 * The lanes that meet nothing in the leaf.
	 */
	unsigned missed;
	/**
	 * The lanes that meet the leaf's surface.
	 */
	unsigned landed;
	/**
	 * Where along its leg each landed lane meets it, from 0 to 1.
	 */
	std::array<float, packetWidth> at;
	/**
	 * The face each landed lane landed on, or -1.
	 */
	std::array<int, packetWidth> face;
};

/**
 * Settle the lanes whose answer in a leaf is found eight at once, as
 * landLeaf() and reachesSurface() would answer each.
 *
 * Every face is restricted to all legs at once with the very float
 * operations of the leaf solve (walk/leaf.h), so each lane holds its
 * scalar quartic to the bit. A plane's and a quadric's roots in the
 * window are found as the leaf solve finds them; a ring's or a patch's
 * quartic counts as rootless where its Bernstein coefficients keep one
 * sign as the leaf solve tests them, and as unknown elsewhere. A lane meets nothing
 * when a face the table needs inside has no root and is not inside
 * halfway along the leg, which is where landLeaf() stops as blocked.
 * A lane with no root reads the table at the signs halfway along: inside
 * it lands at 0, outside it meets nothing. A lane with crossings puts
 * them in order (sortCrossings()), reads the signs in the middle of the
 * widest gap evaluated for all such lanes at once and unwinds them
 * (widestGap(), gapMiddle(), unwound()), and lands at 0 where the table
 * is inside before the first crossing, else where walkedCrossings()
 * lands, as landLeaf() does; for a run of one face that is plane or
 * quadric, whose one or two crossings are already in order, the widest gap
 * and the landing are found for all such lanes at once by the same
 * operations. A ring or a patch the Bernstein test leaves
 * unknown in a lane is solved for that lane alone by the leaf solve's own root
 * search (quarticWindowRoots()) on its quartic from the lanes, and ends
 * the lane as blocked where the table needs it and it is never inside.
 * A lane from inside is never blocked; it lands at 0 where the table is
 * outside before its first crossing, else where leftCrossings() lands,
 * and is left to the leaf solve where that passes from one solid into
 * another on the way.
 *
 * @param asked The run, the legs, the lanes and which are from inside.
 */
LaneVerdicts laneVerdicts(const LeafLanes& asked);

}
