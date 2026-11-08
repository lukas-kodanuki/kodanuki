#pragma once

#include "light/light.h"
#include "math/vector.h"
#include "sdf/program.h"

#include <array>
#include <cstdint>

namespace kodanuki {

/**
 * A path from a point off one surface, by one bounce off a shiny leaf,
 * to a lamp.
 */
struct BounceChain {
	/**
	 * Where the path starts.
	 */
	math::Vector from;
	/**
	 * The two directions across the start's surface along which the
	 * motion of the bounce is measured.
	 */
	std::array<math::Vector, 2> sideways;
	/**
	 * The shiny leaf alone, compiled over world space.
	 */
	const Program& leaf;
	/**
	 * The leaf's facet width, the GGX alpha of its sharpest lobe.
	 */
	double width;
	/**
	 * The lamp.
	 */
	const Lamp& lamp;
};

/**
 * Where a chain's bounce settled, and how the highlight spreads and
 * moves there.
 */
struct SettledChain {
	/**
	 * Whether the walk settled. Nothing else is meaningful where it did
	 * not.
	 */
	bool found;
	/**
	 * The point of the bounce.
	 */
	math::Vector point;
	/**
	 * The unit direction from the start to the point.
	 */
	math::Vector axis;
	/**
	 * The first unit direction across the axis.
	 */
	math::Vector across;
	/**
	 * The second.
	 */
	math::Vector up;
	/**
	 * The spread of the highlight over the plane one unit along the
	 * axis, the upper triangle by rows.
	 */
	std::array<double, 3> spread;
	/**
	 * How far the point moves per unit the start moves along each of the
	 * chain's sideways directions.
	 */
	std::array<math::Vector, 2> motion;
};

/**
 * Walk a point over its leaf until the leaf's normal halves the
 * directions to the start and to the lamp.
 *
 * It is a damped Newton walk in the tangent plane, with the Jacobian
 * from the leaf's gradient and Hessian. The seed and each step are put
 * back on the surface by two Newton steps; a step is at most a tenth of
 * the distance to the start and is halved, up to six times, while the
 * mismatch grows. The walk settles where the mismatch is under 1e-6
 * within 16 steps, with the start and the lamp on the same side and the
 * point on the surface within 1e-6, outside a sphere lamp. The spread is
 * the facet width's alpha squared over two and the lamp's own spread,
 * carried back through the inverse Jacobian onto the plane one unit
 * along the axis; the motion is the start's push carried the same way.
 *
 * @param chain The start, the leaf, its width and the lamp.
 * @param seed Where the point starts.
 */
SettledChain walkedChain(const BounceChain& chain, const math::Vector& seed);

}
