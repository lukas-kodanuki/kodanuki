#pragma once

#include "math/vector.h"

#include <cstdint>

namespace kodanuki::math {

/**
 * Two numbers in [0, 1), the input of a two-dimensional warp.
 */
struct UnitPair {
	/**
	 * The first number.
	 */
	double u;
	/**
	 * The second number.
	 */
	double v;
};

/**
 * Where a plain number is read: a stream, a sample and a coordinate.
 *
 * A plain number is a pure function of its key. A path's draws then
 * depend on nothing but what names them, never on the order they are
 * made in or on the thread that makes them.
 */
struct Key {
	/**
	 * The stream, the hash of a frame's seed with a pixel or a light
	 * path.
	 */
	std::uint64_t stream;
	/**
	 * The sample's place in the pixel's whole run.
	 */
	std::uint64_t index;
	/**
	 * The coordinate along the sample.
	 */
	std::uint64_t coordinate;
};

/**
 * A cone of directions around an axis.
 */
struct Cone {
	/**
	 * The frame around the cone's axis.
	 */
	Basis axis;
	/**
	 * The cosine of the cone's half angle.
	 */
	double cosine;
};

/**
 * Scramble 64 bits so that inputs differing in one bit give unrelated
 * outputs.
 *
 * This is the splitmix64 finaliser, with its increment.
 *
 * @param value The bits.
 */
std::uint64_t mixedBits(std::uint64_t value);

/**
 * Return a number in [0, 1) from 64 bits.
 *
 * Only the top 53 bits are read.
 *
 * @param bits The bits.
 */
double unitOf(std::uint64_t bits);

/**
 * Return the plain number a key names, in [0, 1).
 *
 * @param key The stream, the index and the coordinate.
 */
double hashedNumber(const Key& key);

/**
 * Return a direction drawn by the cosine about a frame's normal.
 *
 * The first number sets the squared distance from the normal's foot,
 * the second the angle around it.
 *
 * @param basis The frame.
 * @param pair The two numbers.
 */
Vector cosineHemisphere(const Basis& basis, const UnitPair& pair);

/**
 * Return the density cosineHemisphere() draws a direction with, per unit
 * of solid angle.
 *
 * It is zero below the surface.
 *
 * @param normal The unit normal.
 * @param direction The unit direction.
 */
double cosineDensity(const Vector& normal, const Vector& direction);

/**
 * Return a direction drawn evenly over the sphere.
 *
 * @param pair The two numbers.
 */
Vector uniformSphere(const UnitPair& pair);

/**
 * Return a direction drawn evenly over a cone.
 *
 * The first number sets the height along the axis, the second the angle
 * around it.
 *
 * @param cone The cone.
 * @param pair The two numbers.
 */
Vector uniformCone(const Cone& cone, const UnitPair& pair);

/**
 * Return a point drawn evenly over the unit disc, by Shirley's
 * concentric map.
 *
 * @param pair The two numbers.
 */
UnitPair concentricDisc(const UnitPair& pair);

}
