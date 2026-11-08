#pragma once

#include "light/light.h"
#include "math/random.h"
#include "math/vector.h"
#include "spectral/spectrum.h"
#include "transport/feature.h"

#include <vector>

namespace kodanuki {

/**
 * The sky laid out to be aimed at.
 *
 * The sphere is cut into 64 rows of equal height along the up axis and
 * 128 columns of equal turn, each cell weighed by the sky's brightness at
 * its middle. A share of every draw is taken evenly over the sphere, so
 * every direction the sky lights can be drawn.
 */
struct SkyChart {
	/**
	 * Whether the sky is charted; an empty chart leaves the sky met only.
	 */
	bool charted;
	/**
	 * The axis rows are measured along.
	 */
	math::Vector up;
	/**
	 * The direction the first column starts from.
	 */
	math::Vector level;
	/**
	 * The direction a quarter turn on.
	 */
	math::Vector aside;
	/**
	 * The running share of the rows, 65 values from 0 to 1.
	 */
	std::vector<double> rows;
	/**
	 * Each row's running share of its cells, 129 values a row.
	 */
	std::vector<double> cells;
	/**
	 * The sky's mean brightness over every direction.
	 */
	double brightness;
	/**
	 * The share of draws taken evenly: 0.1.
	 */
	double even;
};

/**
 * The numbers a direction toward the sky is drawn with.
 */
struct SkyDraw {
	/**
	 * The sky.
	 */
	const GradientSky& sky;
	/**
	 * The number that picks the even share or the chart.
	 */
	double part;
	/**
	 * The numbers that pick the row and the cell, or the direction of
	 * an even draw.
	 */
	math::UnitPair pair;
	/**
	 * The numbers that place the direction inside its cell.
	 */
	math::UnitPair jitter;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * What a chart is made from.
 */
struct SkyChartQuery {
	/**
	 * The lamps and the core's sky.
	 */
	const Lights& lights;
	/**
	 * The Feature members, whose sky the cells are weighed by.
	 */
	const Features& features;
	/**
	 * The middle of the region.
	 */
	math::Vector middle;
};

/**
 * Return a chart that leaves the sky met only.
 */
SkyChart unchartedSky();

/**
 * Chart the sky, or leave it uncharted where it is dark everywhere, or
 * where the lamps outshine it: they send the region's middle more than
 * five times what the sky does, each with nothing in the way, and a
 * sun's or a moon's power thinned as the Feature members thin it
 * (lampThinning).
 *
 * Each cell is weighed by the brightness of the sky with every Feature
 * member on (skyColourAt).
 *
 * @param query The lights, the members and the region's middle.
 */
SkyChart chartedSky(const SkyChartQuery& query);

/**
 * Draw a direction toward the sky.
 *
 * A part below the even share draws evenly over the sphere by the pair.
 * Otherwise the pair picks the row and the cell, and the jitter places
 * the direction inside it. What arrives is the sky's radiance over the
 * density, at no distance's end.
 *
 * @param chart The chart, charted.
 * @param draw The sky and the numbers.
 */
LightAim chartAim(const SkyChart& chart, const SkyDraw& draw);

/**
 * Return the density chartAim() draws a direction with: the even share
 * over 4π plus the rest times its cell's share over the cell's solid
 * angle.
 *
 * @param chart The chart, charted.
 * @param toward The unit direction.
 */
double chartDensity(const SkyChart& chart, const math::Vector& toward);

}
