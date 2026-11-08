#pragma once

#include "tree/cut.h"
#include "tree/tree.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * How wide one pixel of the frame is at one place, as a pilot path
 * found it.
 */
struct Need {
	/**
	 * Where the path met a surface, or where the surface stood that
	 * stopped its shadow ray.
	 */
	math::Vector place;
	/**
	 * How wide the piece of surface there is that one pixel covers.
	 */
	double footprint;
	/**
	 * Whether the path met it straight from the eye.
	 */
	bool direct;
};

/**
 * An open addressed table from a cell of some level of the region to
 * a width.
 */
struct WidthTable {
	/**
	 * The level and place of each slot's cell, 0 where the slot is
	 * free, a power of two of them or none.
	 */
	std::vector<std::uint64_t> keys;
	/**
	 * The width each slot holds, matching the keys.
	 */
	std::vector<float> widths;
	/**
	 * How many slots are taken.
	 */
	std::size_t taken;
};

/**
 * The widths the pilot measured, each held over the cell about eight
 * pixels wide around where it was measured and the 26 beside it.
 *
 * A cell of level k is the region cut in two k times along each axis.
 */
struct Pilot {
	/**
	 * The region the cells are of.
	 */
	Region region;
	/**
	 * The finest level a width is held over.
	 */
	int depth;
	/**
	 * The narrowest width that holds over the whole of each cell.
	 */
	WidthTable held;
	/**
	 * The narrowest width that holds anywhere inside each cell: a
	 * width held over a cell is told to every cell above it.
	 */
	WidthTable inside;
	/**
	 * Which levels hold a width over a whole cell, one bit a level.
	 */
	std::uint32_t levels;
};

/**
 * What the pilot's widths are made from.
 */
struct PilotInput {
	/**
	 * The region the tree covers.
	 */
	Region region;
	/**
	 * The widths the paths measured.
	 */
	const std::vector<Need>& needs;
	/**
	 * Where the frame is seen from. A need no narrower than a pixel's
	 * footprint at its place from the eye alone is passed over.
	 */
	const View& view;
};

/**
 * Hold the measured widths over the cells about them.
 *
 * Each need is held over the cell at the coarsest level that is no
 * wider than eight footprints at its place, at most level 16, and over
 * the 26 cells beside it, keeping the narrowest width a cell is told.
 *
 * @param input The region, the needs and the view.
 */
Pilot pilotOf(const PilotInput& input);

/**
 * Return a pilot that measured nothing, whose limit is infinity
 * everywhere.
 *
 * @param region The region the tree covers.
 */
Pilot unmeasuredPilot(const Region& region);

/**
 * Return the most a cell may stray by for the pilot: the share times
 * the narrowest width measured in the cell, or infinity where none was.
 *
 * The cell is read at the level its size has along x, at most the
 * pilot's depth: the width held inside the cell at its middle there,
 * and every width held over a whole cell above it.
 *
 * @param pilot The measured widths.
 * @param box The cell.
 * @param share The share of a footprint a seen cell may stray by.
 */
double pilotLimit(const Pilot& pilot, const Box& box, double share);

}
