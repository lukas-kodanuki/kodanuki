#pragma once

#include "extras/detail_correct_pilot/pilot.h"
#include "transport/trace.h"
#include "tree/cut.h"

#include <vector>

namespace kodanuki {

/**
 * The share of a pixel's footprint the coarse tree the pilot flies
 * through may stray by.
 */
constexpr double pilotCoarseShare = 2;

/**
 * What the pilot's paths are flown through.
 */
struct PilotFlight {
	/**
	 * What the transport reads, over a coarse tree.
	 */
	const Tracing& tracing;
	/**
	 * Where the frame is seen from.
	 */
	const View& view;
	/**
	 * How many threads.
	 */
	int threads;
};

/**
 * Trace the pilot's paths through a coarse tree and return the widths
 * they measured.
 *
 * Paths start at every eighth pixel each way, from the fourth, and at
 * the pixels right of and below it, all three through the pixel's
 * middle on the base pixel's numbers. They run through walkStarted() and
 * walkAnswered() with the scalar walk, and a path stops at its first
 * draw of a half angle of 0.1 or wider, or at its sixteenth surface. A
 * surface the three met by one route, every bounce before it sharp, is
 * a place, and the least distance across the ray between its hit and
 * the neighbours' is its footprint. A place met past a sharp bounce
 * that no neighbour followed needs all the detail there is (a width of
 * zero), and one met straight from the eye that way an endless width.
 * A surface that blocks a shadow line from a place needs what the place
 * needs, or a pixel's footprint there from the eye where that is
 * endless. The widths come back in the order of the paths.
 *
 * @param flight What the transport reads over the coarse tree, the view and the threads.
 */
std::vector<Need> pilotNeeds(const PilotFlight& flight);

}
