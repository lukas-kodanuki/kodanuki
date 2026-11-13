#pragma once

#include "plugins/geometry/tree/cut.h"
#include "sdf/program.h"

namespace kodanuki {

/**
 * A quadric in a cell's centred unit coordinates, from -1 to 1.
 */
struct Quadric {
	/**
	 * The terms of uu, vv, ww, uv, uw, vw, u, v, w and one.
	 */
	double coefficients[10];
};

/**
 * What proving a quadric against a program over a cell needs.
 */
struct Proof {
	/**
	 * The program the quadric stands in for.
	 */
	const Program& program;
	/**
	 * The quadric.
	 */
	const Quadric& quadric;
	/**
	 * The cell.
	 */
	const Box& box;
	/**
	 * The bound the proof is after; refining stops once it holds and
	 * gives up as soon as it cannot.
	 */
	double limit;
};

/**
 * Return the middle of a box along one axis.
 *
 * @param box The box.
 * @param axis The axis.
 */
double boxCentre(const Box& box, int axis);

/**
 * Return half the width of a box along one axis.
 *
 * @param box The box.
 * @param axis The axis.
 */
double boxHalf(const Box& box, int axis);

/**
 * Return a program's Taylor quadric at a box's centre, from one
 * hyperdual evaluation: value, gradient and Hessian.
 *
 * @param program The program.
 * @param box The box.
 */
Quadric taylorQuadric(const Program& program, const Box& box);

/**
 * Return a quadric's value at a point.
 *
 * @param quadric The quadric.
 * @param u The point in centred unit coordinates.
 */
double quadricValue(const Quadric& quadric, const double* u);

/**
 * Return a bound on how far a quadric strays from a program over a box,
 * refined over its octants down to the 4 x 4 x 4 boxes.
 *
 * @param program The program.
 * @param quadric The quadric.
 * @param box The box.
 */
double quadricBound(const Program& program, const Quadric& quadric, const Box& box);

/**
 * Return a bound as quadricBound does, but stop refining once it is
 * within the proof's limit and give up as soon as a part is over it.
 *
 * @param proof The program, the quadric, the box and the limit.
 */
double provenBound(const Proof& proof);

/**
 * Return a quadric written over the cell's own coordinates instead of
 * its centred ones.
 *
 * The cell's own coordinates run from 0 at its lower side to 1 at its
 * upper side, as the walk and the patches read a cell, so the answer is
 * the same function of u = (c + 1) / 2.
 *
 * @param quadric The quadric in centred unit coordinates.
 */
Quadric cornerQuadric(const Quadric& quadric);

}
