#include "plugins/transport/paths/camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kodanuki {

math::Vector lensPoint(const Lens& lens, const math::UnitPair& pair) {
	const math::UnitPair disc = math::concentricDisc(pair);
	return lens.eye + lens.right * (disc.u * lens.aperture) + lens.up * (disc.v * lens.aperture);
}

Ray cameraRay(const Lens& lens, const LensDraw& draw) {
	Ray ray = filmRay(lens, draw.x, draw.y);
	if (!(lens.aperture > 0)) {
		return ray;
	}
	const double ahead       = lens.focus / std::max(math::dot(ray.direction, lens.forward), 1e-9);
	const math::Vector sharp = lens.eye + ray.direction * ahead;
	ray.origin               = lensPoint(lens, draw.lens);
	ray.direction            = math::normalized(sharp - ray.origin);
	return ray;
}

Ray pixelRay(const Lens& lens, int x, int y) {
	return filmRay(lens, double(x) + 0.5, double(y) + 0.5);
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
