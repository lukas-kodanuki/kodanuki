#pragma once

#include "plugins/geometry/tree/classify.h"
#include "plugins/geometry/tree/cut.h"

#include <cstdint>

namespace kodanuki {

/**
 * How many faces a surface leaf may hold before the member splits it.
 */
constexpr std::uint32_t crowdedFaces = 3;

/**
 * How many times its tolerance a crowded leaf's side must be for the
 * member to split it further.
 */
constexpr double crowdedReach = 64;

/**
 * One planned cell that the member is asked about.
 */
struct CrowdedQuery {
	/**
	 * The cell's plan, which holds its kind and its run.
	 */
	const Plan& plan;
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * How far the cell's surface may stray.
	 */
	double tolerance;
};

/**
 * Return whether a surface leaf holds so many faces that it is split.
 *
 * A ray through a surface leaf restricts and solves every face of its
 * run, so a leaf of many faces (where several objects or the faces of
 * one meet) costs every ray through it that much more. A surface leaf
 * of more than crowdedFaces faces whose side is still more than
 * crowdedReach times its tolerance is split, so its children hold
 * fewer faces each where the faces part; where they keep meeting, as at
 * a corner, the reach stops the splitting.
 *
 * @param query The plan, the cell and its tolerance.
 */
bool crowdsLeaf(const CrowdedQuery& query);

}
