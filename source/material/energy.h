#pragma once

#include "material/microfacet.h"

#include <array>

namespace kodanuki {

/**
 * How many cosines a single-bounce table holds.
 */
constexpr int energyViews = 32;

/**
 * How many index ratios a boundary table holds.
 */
constexpr int boundaryRatios = 17;

/**
 * The largest ratio of the denser index over the rarer that a boundary
 * table holds. A larger ratio is read as this one.
 */
constexpr double densestRatio = 3.0;

/**
 * How much light one bounce off a microfacet surface returns.
 *
 * One minus it is what the single-scattering lobe loses, and the
 * compensation lobe gives that back.
 */
struct Escape {
	/**
	 * What one bounce returns at cosines spread evenly from grazing to
	 * face-on, each entry at the middle of its step.
	 */
	std::array<double, energyViews> table;
	/**
	 * The same, averaged over the hemisphere and weighted by the cosine.
	 */
	double average;
};

/**
 * How much light one bounce off or through the microfacets of a clear
 * boundary returns, from either side.
 *
 * A row is one ratio of indices, spread evenly from 1 to densestRatio.
 * A column is one cosine, as in Escape.
 */
struct Boundary {
	/**
	 * What one bounce returns seen from the rarer side, a row per ratio.
	 */
	std::array<double, boundaryRatios * energyViews> rarer;
	/**
	 * What one bounce returns seen from the denser side.
	 */
	std::array<double, boundaryRatios * energyViews> denser;
	/**
	 * The cosine-weighted average from the rarer side, one per ratio.
	 */
	std::array<double, boundaryRatios> rarerAverage;
	/**
	 * The cosine-weighted average from the denser side, one per ratio.
	 */
	std::array<double, boundaryRatios> denserAverage;
	/**
	 * The share of what the rarer side loses that comes back on the far
	 * side, one per ratio.
	 */
	std::array<double, boundaryRatios> rarerThrough;
};

/**
 * What one bounce at a boundary loses for one ratio seen from one side,
 * and how the loss is given back.
 *
 * The loss comes back as two broad lobes, one on each side. Each is the
 * product of the losses toward the two directions it joins.
 */
struct Loss {
	/**
	 * The average returned on the near side.
	 */
	double nearAverage;
	/**
	 * The average returned on the far side.
	 */
	double farAverage;
	/**
	 * The scale of the lobe back to the near side.
	 */
	double back;
	/**
	 * The scale of the lobe through to the far side.
	 */
	double through;
	/**
	 * The share of the lost light the lobe through carries.
	 */
	double share;
};

/**
 * The two indices on either side of a boundary.
 */
struct Indices {
	/**
	 * The index on the side the boundary is seen from.
	 */
	double near;
	/**
	 * The index on the other side.
	 */
	double far;
};

/**
 * One reading of a boundary table.
 */
struct BoundaryReading {
	/**
	 * The two indices.
	 */
	Indices indices;
	/**
	 * The cosine on the near side.
	 */
	double cosine;
};

/**
 * Work out what one bounce off a microfacet surface returns.
 *
 * The integral is taken by quadrature over a fixed grid of facets, so
 * one width always gives one table.
 *
 * @param width The GGX alpha.
 */
Escape escapeOf(double width);

/**
 * Read an escape table at one cosine, between its two nearest entries.
 *
 * @param escape The table.
 * @param cosine The cosine, clamped to [0, 1].
 */
double escapeAt(const Escape& escape, double cosine);

/**
 * Return the compensation lobe of a single-bounce loss.
 *
 * It is the product of the losses toward the two directions over pi
 * times one minus the average. It is symmetric in its two cosines, so
 * either end of a path that asks gets the same value. It is zero where
 * one bounce loses nothing.
 *
 * @param escape The table.
 * @param cosines The two cosines.
 */
double compensationLobe(const Escape& escape, const Cosines& cosines);

/**
 * Work out what one bounce at a clear boundary returns, for every ratio
 * and from both sides.
 *
 * @param width The GGX alpha.
 */
Boundary boundaryOf(double width);

/**
 * Return what one bounce at a boundary loses for one pair of indices,
 * with the ratio clamped to the table.
 *
 * @param boundary The table.
 * @param indices The two indices.
 */
Loss lossAt(const Boundary& boundary, const Indices& indices);

/**
 * Read a boundary table at one ratio and one cosine.
 *
 * @param boundary The table.
 * @param reading The indices and the cosine.
 */
double boundaryAt(const Boundary& boundary, const BoundaryReading& reading);

}
