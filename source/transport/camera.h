#pragma once

#include "math/random.h"
#include "math/vector.h"
#include "sdf/scene.h"
#include "walk/walk.h"

namespace kodanuki {

/**
 * The camera, worked out for a frame of a given size.
 *
 * The film sits at distance one ahead of the eye. Its half height is
 * reach and its half width is reach times aspect.
 */
struct Lens {
	/**
	 * The middle of the lens.
	 */
	math::Vector eye;
	/**
	 * The unit view direction.
	 */
	math::Vector forward;
	/**
	 * The unit direction to the film's right.
	 */
	math::Vector right;
	/**
	 * The unit direction to the film's top.
	 */
	math::Vector up;
	/**
	 * The tangent of half the vertical field of view.
	 */
	double reach;
	/**
	 * The width over the height.
	 */
	double aspect;
	/**
	 * The radius of the lens, zero for a pinhole.
	 */
	double aperture;
	/**
	 * The distance in focus: the camera's, or the target's distance
	 * where the camera gives none.
	 */
	double focus;
	/**
	 * The frame's width in pixels.
	 */
	int width;
	/**
	 * The frame's height in pixels.
	 */
	int height;
};

/**
 * How a drawn frame's pixels cover the shown frame's.
 *
 * A drawn pixel covers factor x factor shown pixels. A frame aims every
 * drawn pixel's camera rays at one of them, its phase, so frames of
 * different phases fill in the shown pixels between them.
 */
struct FilmPlan {
	/**
	 * Shown pixels per drawn pixel along each side: 1 to 4.
	 */
	int factor;
	/**
	 * The shown pixel inside each drawn pixel the rays aim at, by rows
	 * from 0 to factor x factor - 1; read only where the factor is above
	 * one.
	 */
	int phase;
};

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
 * A point inside a drawn pixel, in drawn pixels from its top left.
 */
struct FilmCentre {
	/**
	 * The offset to the right.
	 */
	double x;
	/**
	 * The offset down.
	 */
	double y;
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
 * Work out the lens of a scene's camera for a frame size.
 *
 * @param camera The camera.
 * @param render The frame size.
 */
Lens lensOf(const Camera& camera, const Render& render);

/**
 * Return the film plan of a frame drawn at the shown size: factor 1,
 * each camera ray anywhere in its pixel.
 */
FilmPlan stillFilm();

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
 * Return where the middle of the shown pixel a film plan aims at lies
 * inside its drawn pixel: ((column + 0.5) / factor, (row + 0.5) /
 * factor), column and row the phase's place in the factor x factor
 * block; the drawn pixel's middle (0.5, 0.5) at a factor of 1.
 *
 * @param film The film plan.
 */
FilmCentre filmCentre(const FilmPlan& film);

/**
 * Return the camera ray through a film point from the lens's middle;
 * pixelRay() where the point is a pixel's middle.
 *
 * @param lens The lens.
 * @param x The film point's column, in pixels from the left.
 * @param y The film point's row, in pixels from the top.
 */
Ray filmRay(const Lens& lens, double x, double y);

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
