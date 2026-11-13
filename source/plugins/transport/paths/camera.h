#pragma once

#include "core/camera.h"
#include "core/geometry.h"
#include "math/random.h"
#include "math/vector.h"
#include "sdf/scene.h"

namespace kodanuki {

/**
 * Where a camera ray passes through the film and the lens.
 */
struct LensDraw {
	/**
	 * The pixel's column plus its offset.
	 */
	double x;
	/**
	 * The pixel's row plus its offset.
	 */
	double y;
	/**
	 * The two numbers the point on the lens is drawn with.
	 */
	math::UnitPair lens;
};

/**
 * Where a line from the lens lands on the film.
 */
struct Raster {
	/**
	 * Whether it lands inside the frame.
	 */
	bool inside;
	/**
	 * The pixel's column.
	 */
	int x;
	/**
	 * The pixel's row.
	 */
	int y;
	/**
	 * The cosine between the line and the view direction.
	 */
	double cosine;
};

/**
 * Return the camera ray through a film point and a lens point.
 *
 * It starts on the lens, drawn evenly over its disc, and passes through
 * the point the film point focuses at. Through a pinhole it starts at
 * the eye.
 *
 * @param lens The lens.
 * @param draw The film and lens points.
 */
Ray cameraRay(const Lens& lens, const LensDraw& draw);

/**
 * Return the point on the lens two numbers draw: concentric over the
 * lens's disc, the eye itself through a pinhole.
 *
 * @param lens The lens.
 * @param pair The two numbers.
 */
math::Vector lensPoint(const Lens& lens, const math::UnitPair& pair);

/**
 * Return the camera ray through a pixel's middle from the lens's
 * middle, as the geometry checks cast it.
 *
 * @param lens The lens.
 * @param x The pixel's column.
 * @param y The pixel's row.
 */
Ray pixelRay(const Lens& lens, int x, int y);

/**
 * Return where the line from a point on the lens toward a scene point
 * lands on the film.
 *
 * @param lens The lens.
 * @param onLens The point on the lens.
 * @param point The scene point.
 */
Raster rasterOf(const Lens& lens, const math::Vector& onLens, const math::Vector& point);

/**
 * Return the solid-angle density a camera ray is drawn with, per
 * sample of a pixel, over the whole film.
 *
 * It is one over the film area times the cube of the cosine. The join
 * strategy measures itself against it.
 *
 * @param lens The lens.
 * @param cosine The cosine between the ray and the view direction.
 */
double lensDensity(const Lens& lens, double cosine);

}
