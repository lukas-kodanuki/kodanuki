#include "transport/camera.h"
#include "material/material.h"

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
		.far       = std::numeric_limits<double>::infinity(),
		.inside    = false,
		.within    = noMaterial,
	};
}

math::Vector lensPoint(const Lens& lens, const math::UnitPair& pair) {
	const math::UnitPair disc = math::concentricDisc(pair);
	return lens.eye + lens.right * (disc.u * lens.aperture) + lens.up * (disc.v * lens.aperture);
}

Ray cameraRay(const Lens& lens, const LensDraw& draw) {
	const math::Vector straight = filmDirection(lens, draw.x, draw.y);
	if (!(lens.aperture > 0)) {
		return rayFrom(lens.eye, straight);
	}
	const double ahead        = lens.focus / std::max(math::dot(straight, lens.forward), 1e-9);
	const math::Vector sharp  = lens.eye + straight * ahead;
	const math::Vector origin = lensPoint(lens, draw.lens);
	return rayFrom(origin, math::normalized(sharp - origin));
}

Ray pixelRay(const Lens& lens, int x, int y) {
	return rayFrom(lens.eye, filmDirection(lens, double(x) + 0.5, double(y) + 0.5));
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

Raster outsideRaster() {
	return Raster {
		.inside = false,
		.x      = 0,
		.y      = 0,
		.cosine = 0,
	};
}

Raster rasterOf(const Lens& lens, const math::Vector& onLens, const math::Vector& point) {
	const math::Vector looking = math::normalized(point - onLens);
	const double ahead         = math::dot(looking, lens.forward);
	if (!(ahead > 0)) {
		return outsideRaster();
	}
	const math::Vector sharp = onLens + looking * (lens.focus / ahead) - lens.eye;
	const double u           = math::dot(sharp, lens.right) / (lens.focus * lens.reach * lens.aspect);
	const double v           = math::dot(sharp, lens.up) / (lens.focus * lens.reach);
	const double x           = (u + 1) * 0.5 * double(lens.width);
	const double y           = (1 - v) * 0.5 * double(lens.height);
	if (!(x >= 0 && x < double(lens.width) && y >= 0 && y < double(lens.height))) {
		return outsideRaster();
	}
	return Raster {
		.inside = true,
		.x      = int(x),
		.y      = int(y),
		.cosine = ahead,
	};
}

double lensDensity(const Lens& lens, double cosine) {
	return 1 / (4 * lens.reach * lens.reach * lens.aspect * cosine * cosine * cosine);
}

}
