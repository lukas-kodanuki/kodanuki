#pragma once

#include "core/batch.h"
#include "sdf/scene.h"

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

} // namespace kodanuki
