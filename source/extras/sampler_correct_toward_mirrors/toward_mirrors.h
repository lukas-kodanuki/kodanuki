#pragma once

#include "light/light.h"
#include "material/material.h"
#include "math/random.h"
#include "math/vector.h"
#include "walk/walk.h"

#include <vector>

namespace kodanuki {

/**
 * A ball holding every point where a ray may meet one scene leaf that
 * may relay a light path.
 */
struct Ball {
	/**
	 * The middle.
	 */
	math::Vector middle;
	/**
	 * The radius.
	 */
	double radius;
};

/**
 * TowardMirrors' frame state: one ball a relaying scene leaf, none where
 * nothing may relay.
 */
struct MirrorBalls {
	/**
	 * The balls.
	 */
	std::vector<Ball> balls;
};

/**
 * A direction to draw from a point on a lamp toward the balls.
 */
struct MirrorDraw {
	/**
	 * The point on the lamp, its normal zero for a point light.
	 */
	const LampPoint& point;
	/**
	 * The number that picks the cone.
	 */
	double pick;
	/**
	 * The numbers that place the direction inside the cone.
	 */
	math::UnitPair pair;
};

/**
 * A direction from a point on a lamp whose density is asked.
 */
struct MirrorAsk {
	/**
	 * The point on the lamp, its normal zero for a point light.
	 */
	const LampPoint& point;
	/**
	 * The unit direction.
	 */
	math::Vector direction;
};

/**
 * A direction drawn toward the balls.
 */
struct MirrorDirection {
	/**
	 * Whether a ball faced the point, so a direction was drawn.
	 */
	bool found;
	/**
	 * The unit direction.
	 */
	math::Vector direction;
};

/**
 * Return no balls: nothing may relay.
 */
MirrorBalls noMirrorBalls();

/**
 * Bound every scene leaf that may wear a sharp material, by its own
 * material or a coat's, unless it refuses caustics.
 *
 * Each is bounded by the box of the tree's leaves whose programs name
 * it and where it comes within the blend radius and four strays of zero.
 * A surface leaf with no patch is taken in its eighths three times over.
 * The box is held in the ball about its middle.
 *
 * @param geometry The tree and the scene.
 * @param looks The scene's looks: its materials and coats.
 * @param materials The finishes, which say what is sharp.
 */
MirrorBalls relayingBalls(const Geometry& geometry, const Looks& looks, const Materials& materials);

/**
 * Draw a direction from a lamp point toward the balls.
 *
 * From the point each ball not wholly behind the face is a cone. One is
 * picked by its solid angle, and the direction is drawn evenly inside
 * it. Nothing is drawn where no ball faces the point.
 *
 * @param mirrors The balls.
 * @param draw The lamp point and the numbers.
 */
MirrorDirection mirrorDirection(const MirrorBalls& mirrors, const MirrorDraw& draw);

/**
 * Return the density mirrorDirection() draws a direction with: how many
 * facing cones hold it over the solid angle of every facing cone, zero
 * where none faces the point.
 *
 * @param mirrors The balls.
 * @param ask The lamp point and the direction.
 */
double mirrorDensity(const MirrorBalls& mirrors, const MirrorAsk& ask);

}
