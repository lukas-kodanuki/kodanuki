#pragma once

#include "math/colour.h"
#include "math/vector.h"
#include "sdf/scene.h"

#include <vector>

namespace kodanuki {

/**
 * Whether a ray that is not exact reads the clouds off the table: false
 * when the build leaves this plugin out (CMake's `-DOFF`), and every ray
 * then marches the clouds as an exact ray does. Read once at start.
 */
extern const bool cloudTableRead;

/**
 * The clouds over a scene, lit once and laid out over every direction
 * above the horizon.
 *
 * How thick the cloud is at a point is a value noise cut down to the
 * share of the sky it covers and eased at the layer's floor, over a
 * round planet of 6360 km. It is lit by one light along one direction,
 * scattered by a Henyey-Greenstein beam, and by the sky about it.
 */
struct Cloudscape {
	/**
	 * Whether the scene has clouds.
	 */
	bool present;
	/**
	 * The scene's cloud fields.
	 */
	Clouds clouds;
	/**
	 * The unit direction up.
	 */
	math::Vector up;
	/**
	 * A unit direction level with the horizon that turns are measured
	 * from.
	 */
	math::Vector level;
	/**
	 * The level unit direction a quarter turn from level.
	 */
	math::Vector aside;
	/**
	 * The unit direction of the light the clouds are lit by.
	 */
	math::Vector toward;
	/**
	 * What that light lands with, per channel.
	 */
	Colour light;
	/**
	 * What the sky about them lights them with, per channel.
	 */
	Colour ambient;
	/**
	 * What the clouds send along each direction of the table, by height
	 * and then by turn: 160 by 640.
	 */
	std::vector<Colour> shown;
	/**
	 * The fraction of what lies behind them they let through, matching
	 * shown one for one.
	 */
	std::vector<double> through;
	/**
	 * Whether the clouds thin each light, the suns and the moons, matching
	 * Lighting::lights one for one.
	 */
	std::vector<bool> thinned;
};

/**
 * What the clouds do to one line of sight.
 */
struct Veil {
	/**
	 * What they send along it, per channel.
	 */
	Colour shown;
	/**
	 * The fraction of what lies behind them they let through.
	 */
	double through;
};

/**
 * What lights the clouds while they are laid out.
 */
struct CloudLight {
	/**
	 * The scene's lighting: its clouds and lights.
	 */
	const Lighting& lighting;
	/**
	 * The unit direction of the light they are lit by: the first sun, or
	 * up where there is none.
	 */
	math::Vector toward;
	/**
	 * What that light lands with, per channel, as the air lets it
	 * through.
	 */
	Colour light;
	/**
	 * What the sky below them lights them with, per channel.
	 */
	Colour ambient;
	/**
	 * How many threads the table is built on; the table is the same on
	 * any number.
	 */
	int threads;
};

/**
 * Light the scene's clouds and lay them out, or return empty ones.
 *
 * The table is 160 heights by 640 turns, each a march of 28 steps of 4
 * octaves; every cell is worked out alone, so the table is the same on
 * any number of threads.
 *
 * @param light The scene, the light that lights them and the threads.
 */
Cloudscape cloudscapeOf(const CloudLight& light);

/**
 * Return what the clouds do to a line of sight leaving the scene.
 *
 * An exact line is marched in full, 72 steps of 7 octaves; any other
 * is read off the table.
 *
 * @param scape The clouds.
 * @param direction The unit direction.
 * @param exact Whether the line was drawn exactly.
 */
Veil veiled(const Cloudscape& scape, const math::Vector& direction, bool exact);

/**
 * Return the fraction of a sun's light the clouds let reach a point,
 * marched in 12 steps of 4 octaves.
 *
 * @param scape The clouds.
 * @param from The point.
 * @param toward The unit direction of the sun.
 */
double overcast(const Cloudscape& scape, const math::Vector& from, const math::Vector& toward);

}
