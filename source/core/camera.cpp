#include "core/camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kodanuki {

Lens lensOf(const Camera& camera, const Render& render) {
	const math::Vector toward  = camera.target - camera.eye;
	const math::Vector forward = math::normalized(toward);
	const math::Vector right   = math::normalized(math::cross(forward, camera.up));
	return Lens {
		.eye      = camera.eye,
		.forward  = forward,
		.right    = right,
		.up       = math::cross(right, forward),
		.reach    = std::tan(double(camera.fov) / 2),
		.aspect   = double(render.width) / double(render.height),
		.aperture = std::max(double(camera.aperture), 0.0),
		.focus    = camera.focus > 0 ? double(camera.focus) : math::length(toward),
		.width    = render.width,
		.height   = render.height,
	};
}

FilmPlan stillFilm() {
	return FilmPlan {
		.factor = 1,
		.phase  = 0,
	};
}

math::Vector filmDirection(const Lens& lens, double x, double y) {
	const double u             = 2 * x / double(lens.width) - 1;
	const double v             = 1 - 2 * y / double(lens.height);
	const math::Vector heading = lens.forward + lens.right * (u * lens.reach * lens.aspect) + lens.up * (v * lens.reach);
	return math::normalized(heading);
}

Ray rayFrom(const math::Vector& origin, const math::Vector& direction) {
	return Ray {
		.origin    = origin,
		.direction = direction,
		.far       = std::numeric_limits<float>::infinity(),
		.inside    = false,
		.within    = noMaterial,
	};
}

FilmCentre filmCentre(const FilmPlan& film) {
	const double factor = double(film.factor);
	const int column    = film.factor > 1 ? film.phase % film.factor : 0;
	const int row       = film.factor > 1 ? film.phase / film.factor : 0;
	return FilmCentre {
		.x = (double(column) + 0.5) / factor,
		.y = (double(row) + 0.5) / factor,
	};
}

Ray filmRay(const Lens& lens, double x, double y) {
	return rayFrom(lens.eye, filmDirection(lens, x, y));
}

} // namespace kodanuki
