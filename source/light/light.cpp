#include "light/light.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kodanuki {

constexpr double belowMargin = 1e-6;
constexpr double infinity    = std::numeric_limits<double>::infinity();

struct RectangleView {
	math::Vector origin;
	math::Vector x;
	math::Vector y;
	math::Vector z;
	double z0;
	double x0;
	double x1;
	double y0;
	double y1;
	double b0;
	double b1;
	double k;
	double angle;
};

double sunHalfAngle(const Light& light) {
	return 0.5 * double(light.angle);
}

Colour radianceOf(const Light& light) {
	switch (light.kind) {
		case LightKind::Sphere: return light.power * (1 / (math::pi * double(light.radius) * double(light.radius)));
		case LightKind::Quad: return light.power * (1 / math::parallelogramArea(light.along, light.across));
		case LightKind::Moon:
		case LightKind::Sun: return light.power * (1 / (math::pi * std::sin(sunHalfAngle(light)) * std::sin(sunHalfAngle(light))));
		case LightKind::Point:
		case LightKind::Directional: return light.power;
	}

	return light.power;
}

double emittedOf(const Light& light) {
	const double mean = std::max(channelMean(light.power), 0.0);
	return (light.kind == LightKind::Quad ? math::pi : 4 * math::pi) * mean;
}

double areaOf(const Light& light) {
	switch (light.kind) {
		case LightKind::Sphere: return 4 * math::pi * double(light.radius) * double(light.radius);
		case LightKind::Quad: return math::parallelogramArea(light.along, light.across);
		case LightKind::Point:
		case LightKind::Directional:
		case LightKind::Moon:
		case LightKind::Sun: return 0;
	}

	return 0;
}

QuadEdges quadEdgesOf(const math::Vector& along, const math::Vector& across) {
	const double alongLength  = math::length(along);
	const double acrossLength = math::length(across);
	const math::Vector x      = along / alongLength;
	const math::Vector y      = across / acrossLength;
	return QuadEdges {
		.alongLength  = alongLength,
		.acrossLength = acrossLength,
		.x            = x,
		.y            = y,
		.raw          = math::cross(x, y),
	};
}

Lamp lampOf(const Light& light) {
	const math::Vector across = light.across;
	return Lamp {
		.kind      = light.kind,
		.at        = light.at,
		.direction = math::normalized(light.direction),
		.along     = light.along,
		.across    = across,
		.facing    = math::normalized(math::cross(light.along, across)),
		.radius    = double(light.radius),
		.cosine    = std::cos(sunHalfAngle(light)),
		.power     = light.power,
		.radiance  = radianceOf(light),
		.area      = areaOf(light),
		.emitted   = emittedOf(light),
		.edges     = quadEdgesOf(light.along, across),
	};
}

Lights lightsOf(const Lighting& lighting) {
	const GradientSky sky = {
		.up      = { 0, 0, 1 },
		.zenith  = lighting.sky.zenith,
		.horizon = lighting.sky.horizon,
		.ground  = lighting.sky.ground,
	};

	Lights lights = {
		.lamps = {},
		.sky   = sky,
	};

	for (const Light& light : lighting.lights) {
		lights.lamps.push_back(lampOf(light));
	}
	return lights;
}

std::uint32_t lightCount(const Lights& lights) {
	return std::uint32_t(lights.lamps.size()) + 1;
}

std::uint32_t skyLight(const Lights& lights) {
	return std::uint32_t(lights.lamps.size());
}

bool isLamp(const Lights& lights, std::uint32_t light) {
	return light < lights.lamps.size();
}

bool isSingular(const Lights& lights, std::uint32_t light) {
	if (!isLamp(lights, light)) {
		return false;
	}
	const LightKind kind = lights.lamps[light].kind;
	return kind == LightKind::Point || kind == LightKind::Directional;
}

bool seenByCamera(const Lights& lights, std::uint32_t light) {
	return light == skyLight(lights);
}

LightAim noAim() {
	return LightAim {
		.found    = false,
		.toward   = { 0, 0, 1 },
		.distance = 0,
		.radiance = flatSpectrum(0),
		.raw      = flatSpectrum(0),
		.density  = 0,
		.cell     = noCell,
	};
}

LightAim aimOf(const math::Vector& toward, double distance, const Spectrum4& radiance, double density) {
	return LightAim {
		.found    = true,
		.toward   = toward,
		.distance = distance,
		.radiance = radiance,
		.raw      = radiance,
		.density  = density,
		.cell     = noCell,
	};
}

bool squareQuad(const Lamp& lamp) {
	return std::abs(math::dot(lamp.along, lamp.across)) < 1e-9 * lamp.edges.alongLength * lamp.edges.acrossLength;
}

bool cornerAbove(const math::Vector& corner, const AimQuery& query) {
	const math::Vector offset = corner - query.from;
	return math::dot(query.normal, offset) >= -belowMargin * math::length(offset);
}

bool quadBeneath(const Lamp& lamp, const AimQuery& query) {
	if (lamp.area < 1e-12 || squareQuad(lamp) || math::dot(lamp.facing, query.from - lamp.at) <= 0) {
		return false;
	}
	const math::Vector corners[4] = { lamp.at, lamp.at + lamp.along, lamp.at + lamp.across, lamp.at + lamp.along + lamp.across };
	for (const math::Vector& corner : corners) {
		if (cornerAbove(corner, query)) {
			return false;
		}
	}
	return true;
}

bool roundBeneath(const Lamp& lamp, const AimQuery& query) {
	const math::Vector offset = lamp.at - query.from;
	const double distance     = math::length(offset);
	if (distance < 1e-9 || (lamp.kind == LightKind::Sphere && distance <= lamp.radius)) {
		return false;
	}
	const double reach = lamp.kind == LightKind::Sphere ? lamp.radius : 0;
	return math::dot(query.normal, offset) < -(reach + belowMargin * distance);
}

bool lampBeneath(const Lamp& lamp, const AimQuery& query) {
	if (query.normal.x == 0 && query.normal.y == 0 && query.normal.z == 0) {
		return false;
	}
	switch (lamp.kind) {
		case LightKind::Directional: return math::dot(query.normal, lamp.direction) <= 0;
		case LightKind::Moon:
		case LightKind::Sun: return math::dot(query.normal, lamp.direction) < -(std::sqrt(std::max(1 - lamp.cosine * lamp.cosine, 0.0)) + belowMargin);
		case LightKind::Quad: return quadBeneath(lamp, query);
		case LightKind::Point:
		case LightKind::Sphere: return roundBeneath(lamp, query);
	}

	return false;
}

RectangleView rectangleView(const Lamp& lamp, const math::Vector& origin) {
	const math::Vector offset = lamp.at - origin;
	const bool turned         = math::dot(offset, lamp.edges.raw) > 0;
	const math::Vector z      = turned ? -lamp.edges.raw : lamp.edges.raw;
	const double z0           = math::dot(offset, z);
	const double x0           = math::dot(offset, lamp.edges.x);
	const double y0           = math::dot(offset, lamp.edges.y);
	const double x1           = x0 + lamp.edges.alongLength;
	const double y1           = y0 + lamp.edges.acrossLength;
	const double sides[4]     = { z0 * x1 - x0 * z0, y0 * z0 - z0 * y1, z0 * x0 - x1 * z0, y1 * z0 - z0 * y0 };
	const double heights[4]   = { x0 * y0 - y0 * x1, x1 * y1 - y0 * x1, x1 * y1 - y1 * x0, x0 * y0 - y1 * x0 };
	double upright[4];
	for (int side = 0; side < 4; ++side) {
		const double size = std::sqrt(sides[side] * sides[side] + heights[side] * heights[side]);
		upright[side]     = size > 0 ? heights[side] / size : heights[side];
	}
	const double g0 = std::acos(std::clamp(-(upright[0] * upright[1]), -1.0, 1.0));
	const double g1 = std::acos(std::clamp(-(upright[1] * upright[2]), -1.0, 1.0));
	const double g2 = std::acos(std::clamp(-(upright[2] * upright[3]), -1.0, 1.0));
	const double g3 = std::acos(std::clamp(-(upright[3] * upright[0]), -1.0, 1.0));
	const double k  = 2 * math::pi - g2 - g3;
	return RectangleView {
		.origin = origin,
		.x      = lamp.edges.x,
		.y      = lamp.edges.y,
		.z      = z,
		.z0     = z0,
		.x0     = x0,
		.x1     = x1,
		.y0     = y0,
		.y1     = y1,
		.b0     = upright[0],
		.b1     = upright[2],
		.k      = k,
		.angle  = g0 + g1 - k,
	};
}

math::Vector rectanglePoint(const RectangleView& view, const math::UnitPair& pair) {
	const double au  = pair.u * view.angle + view.k;
	const double fu  = (std::cos(au) * view.b0 - view.b1) / std::sin(au);
	const double cu  = std::clamp(std::copysign(1 / std::sqrt(fu * fu + view.b0 * view.b0), fu), -1.0, 1.0);
	const double xu  = std::clamp(-(cu * view.z0) / std::sqrt(std::max(1 - cu * cu, 1e-14)), view.x0, view.x1);
	const double d   = std::sqrt(xu * xu + view.z0 * view.z0);
	const double h0  = view.y0 / std::sqrt(d * d + view.y0 * view.y0);
	const double h1  = view.y1 / std::sqrt(d * d + view.y1 * view.y1);
	const double hv  = h0 + pair.v * (h1 - h0);
	const double hv2 = hv * hv;
	const double yv  = hv2 < 1 - 1e-6 ? (hv * d) / std::sqrt(1 - hv2) : view.y1;
	return view.origin + view.x * xu + view.y * yv + view.z * view.z0;
}

LightAim aimAtPoint(const AimQuery& query, const math::Vector& spot, const Colour& radiance, double density) {
	const math::Vector offset = spot - query.from;
	const double distance     = math::length(offset);
	if (distance < 1e-9) {
		return noAim();
	}
	return aimOf(offset / distance, distance, spectrumOf(radiance, query.wavelengths), density);
}

LightAim aimSquareQuad(const Lamp& lamp, const AimQuery& query) {
	const RectangleView view = rectangleView(lamp, query.from);
	if (!(view.angle > 1e-7)) {
		return noAim();
	}
	return aimAtPoint(query, rectanglePoint(view, query.pair), lamp.radiance * view.angle, 1 / view.angle);
}

LightAim aimSkewQuad(const Lamp& lamp, const AimQuery& query) {
	const math::Vector spot   = lamp.at + lamp.along * query.pair.u + lamp.across * query.pair.v;
	const math::Vector offset = spot - query.from;
	const double distance     = math::length(offset);
	if (distance < 1e-9) {
		return noAim();
	}
	const math::Vector toward = offset / distance;
	const double tilt         = -math::dot(lamp.facing, toward);
	if (tilt <= 0) {
		return aimOf(toward, distance, flatSpectrum(0), 0);
	}
	const Colour arriving = lamp.radiance * (tilt * lamp.area / (distance * distance));
	return aimOf(toward, distance, spectrumOf(arriving, query.wavelengths), distance * distance / (tilt * lamp.area));
}

LightAim aimQuad(const Lamp& lamp, const AimQuery& query) {
	if (lamp.area < 1e-12 || math::dot(lamp.facing, query.from - lamp.at) <= 0) {
		return noAim();
	}
	return squareQuad(lamp) ? aimSquareQuad(lamp, query) : aimSkewQuad(lamp, query);
}

double coneCosine(double radius, double distance) {
	const double sideways = radius / distance;
	return std::sqrt(std::max(1 - sideways * sideways, 0.0));
}

double coneDensity(double cosine) {
	const double cone = 2 * math::pi * (1 - cosine);
	return cone > 1e-12 ? 1 / cone : 0;
}

LightAim aimSphere(const Lamp& lamp, const AimQuery& query) {
	const math::Vector offset = lamp.at - query.from;
	const double distance     = math::length(offset);
	if (distance < 1e-9 || distance <= lamp.radius) {
		return noAim();
	}
	const math::Vector centre = offset / distance;

	const math::Cone cone = {
		.axis   = math::basisAround(centre),
		.cosine = coneCosine(lamp.radius, distance),
	};

	const math::Vector toward = math::uniformCone(cone, query.pair);
	const double height       = math::dot(toward, centre);
	const double ring         = std::sqrt(std::max(1 - height * height, 0.0));
	const double reach        = distance * height - std::sqrt(std::max(lamp.radius * lamp.radius - distance * distance * ring * ring, 0.0));
	const Colour arriving     = lamp.power * (2 * (1 - cone.cosine) / (lamp.radius * lamp.radius));
	return aimOf(toward, std::max(reach, 0.0), spectrumOf(arriving, query.wavelengths), coneDensity(cone.cosine));
}

LightAim aimSun(const Lamp& lamp, const AimQuery& query) {
	if (!(lamp.cosine < 1)) {
		return noAim();
	}

	const math::Cone cone = {
		.axis   = math::basisAround(lamp.direction),
		.cosine = lamp.cosine,
	};

	const double density  = coneDensity(lamp.cosine);
	const Colour arriving = lamp.radiance * (1 / density);
	return aimOf(math::uniformCone(cone, query.pair), infinity, spectrumOf(arriving, query.wavelengths), density);
}

LightAim aimPoint(const Lamp& lamp, const AimQuery& query) {
	const math::Vector offset = lamp.at - query.from;
	const double distance     = math::length(offset);
	if (distance < 1e-9) {
		return noAim();
	}
	const Colour arriving = lamp.power * (1 / (distance * distance));
	return aimOf(offset / distance, distance, spectrumOf(arriving, query.wavelengths), 0);
}

LightAim aimLamp(const Lamp& lamp, const AimQuery& query) {
	switch (lamp.kind) {
		case LightKind::Point: return aimPoint(lamp, query);
		case LightKind::Directional: return aimOf(lamp.direction, infinity, spectrumOf(lamp.power, query.wavelengths), 0);
		case LightKind::Sphere: return aimSphere(lamp, query);
		case LightKind::Quad: return aimQuad(lamp, query);
		case LightKind::Moon:
		case LightKind::Sun: return aimSun(lamp, query);
	}

	return noAim();
}

LightAim aimLight(const Lights& lights, const AimQuery& query) {
	if (!isLamp(lights, query.light)) {
		return noAim();
	}
	const Lamp& lamp = lights.lamps[query.light];
	if (lampBeneath(lamp, query)) {
		return noAim();
	}
	return aimLamp(lamp, query);
}

double quadDensity(const Lamp& lamp, const AimDensityQuery& query) {
	if (lamp.area < 1e-12 || math::dot(lamp.facing, query.from - lamp.at) <= 0) {
		return 0;
	}
	if (squareQuad(lamp)) {
		const RectangleView view = rectangleView(lamp, query.from);
		return view.angle > 1e-7 ? 1 / view.angle : 0;
	}
	const double tilt = -math::dot(lamp.facing, query.toward);
	if (tilt <= 0 || query.distance <= 0) {
		return 0;
	}
	return query.distance * query.distance / (tilt * lamp.area);
}

double sphereDensity(const Lamp& lamp, const AimDensityQuery& query) {
	const double away = math::length(lamp.at - query.from);
	if (away <= lamp.radius || lamp.radius <= 0) {
		return 0;
	}
	return coneDensity(coneCosine(lamp.radius, away));
}

double aimDensity(const Lights& lights, const AimDensityQuery& query) {
	if (!isLamp(lights, query.light)) {
		return 0;
	}
	const Lamp& lamp = lights.lamps[query.light];
	switch (lamp.kind) {
		case LightKind::Point:
		case LightKind::Directional: return 0;
		case LightKind::Sphere: return sphereDensity(lamp, query);
		case LightKind::Quad: return quadDensity(lamp, query);
		case LightKind::Moon:
		case LightKind::Sun: return lamp.cosine < 1 && math::dot(query.toward, lamp.direction) >= lamp.cosine ? coneDensity(lamp.cosine) : 0;
	}

	return 0;
}

LightMeet noMeet() {
	return LightMeet {
		.met      = false,
		.distance = 0,
		.radiance = flatSpectrum(0),
	};
}

LightMeet meetOf(double distance, const Colour& radiance, const Wavelengths& wavelengths) {
	return LightMeet {
		.met      = true,
		.distance = distance,
		.radiance = spectrumOf(radiance, wavelengths),
	};
}

LightMeet meetSphere(const Lamp& lamp, const MeetQuery& query) {
	if (lamp.radius <= 0) {
		return noMeet();
	}
	const math::Vector offset = query.origin - lamp.at;
	const double along        = math::dot(offset, query.direction);
	const double under        = along * along - (math::dot(offset, offset) - lamp.radius * lamp.radius);
	if (under <= 0) {
		return noMeet();
	}
	const double root  = std::sqrt(under);
	const double first = -along - root;
	const double reach = first <= 1e-9 ? -along + root : first;
	if (reach <= 1e-9 || reach >= query.limit) {
		return noMeet();
	}
	return meetOf(reach, lamp.radiance, query.wavelengths);
}

LightMeet meetQuad(const Lamp& lamp, const MeetQuery& query) {
	const math::Vector facing = math::cross(lamp.along, lamp.across);
	const double leaning      = math::dot(facing, query.direction);
	if (lamp.area < 1e-12 || leaning >= -1e-12) {
		return noMeet();
	}
	const double reach = math::dot(facing, lamp.at - query.origin) / leaning;
	if (reach <= 1e-9 || reach >= query.limit) {
		return noMeet();
	}
	const math::Vector spot = query.origin + query.direction * reach - lamp.at;
	const double squared    = lamp.area * lamp.area;
	const double u          = math::dot(math::cross(spot, lamp.across), facing) / squared;
	const double v          = math::dot(math::cross(lamp.along, spot), facing) / squared;
	if (u < 0 || u > 1 || v < 0 || v > 1) {
		return noMeet();
	}
	return meetOf(reach, lamp.radiance, query.wavelengths);
}

LightMeet meetSun(const Lamp& lamp, const MeetQuery& query) {
	if (!(lamp.cosine < 1) || query.limit < infinity || math::dot(query.direction, lamp.direction) < lamp.cosine) {
		return noMeet();
	}
	return meetOf(infinity, lamp.radiance, query.wavelengths);
}

LightMeet meetLamp(const Lamp& lamp, const MeetQuery& query) {
	switch (lamp.kind) {
		case LightKind::Point:
		case LightKind::Directional: return noMeet();
		case LightKind::Sphere: return meetSphere(lamp, query);
		case LightKind::Quad: return meetQuad(lamp, query);
		case LightKind::Moon:
		case LightKind::Sun: return meetSun(lamp, query);
	}

	return noMeet();
}

LightMeet meetLight(const Lights& lights, const MeetQuery& query) {
	if (isLamp(lights, query.light)) {
		return meetLamp(lights.lamps[query.light], query);
	}
	if (query.limit < infinity) {
		return noMeet();
	}
	return LightMeet {
		.met      = true,
		.distance = infinity,
		.radiance = skyRadiance(lights.sky, query.direction, query.wavelengths),
	};
}

bool startsPaths(const Lights& lights, std::uint32_t light) {
	if (!isLamp(lights, light)) {
		return false;
	}
	const Lamp& lamp = lights.lamps[light];
	switch (lamp.kind) {
		case LightKind::Point: return true;
		case LightKind::Sphere: return lamp.radius > 0;
		case LightKind::Quad: return lamp.area > 1e-12;
		case LightKind::Directional:
		case LightKind::Moon:
		case LightKind::Sun: return false;
	}

	return false;
}

double lampPointDensity(const Lights& lights, std::uint32_t light) {
	const Lamp& lamp = lights.lamps[light];
	return lamp.area > 0 ? 1 / lamp.area : 1;
}

LampPoint lampPointAt(const LampDraw& draw, const math::Vector& point, const math::Vector& normal) {
	return LampPoint {
		.light   = draw.light,
		.point   = point,
		.normal  = normal,
		.density = 0,
	};
}

LampPoint drawnLampPoint(const Lamp& lamp, const LampDraw& draw) {
	const math::Vector round = math::uniformSphere(draw.pair);
	const math::Vector none  = { 0, 0, 0 };
	switch (lamp.kind) {
		case LightKind::Sphere: return lampPointAt(draw, lamp.at + round * lamp.radius, round);
		case LightKind::Quad: return lampPointAt(draw, lamp.at + lamp.along * draw.pair.u + lamp.across * draw.pair.v, lamp.facing);
		case LightKind::Point:
		case LightKind::Directional:
		case LightKind::Moon:
		case LightKind::Sun: return lampPointAt(draw, lamp.at, none);
	}

	return lampPointAt(draw, lamp.at, none);
}

LampPoint pointOnLamp(const Lights& lights, const LampDraw& draw) {
	LampPoint point = drawnLampPoint(lights.lamps[draw.light], draw);
	point.density   = lampPointDensity(lights, draw.light);
	return point;
}

Spectrum4 lampEmission(const Lights& lights, const LampPoint& point, const Wavelengths& wavelengths) {
	const Lamp& lamp = lights.lamps[point.light];
	return spectrumOf(lamp.kind == LightKind::Point ? lamp.power : lamp.radiance, wavelengths);
}

std::vector<double> lampShares(const Lights& lights) {
	std::vector<double> shares(lights.lamps.size(), 0.0);
	double total = 0;
	for (std::uint32_t light = 0; light < lights.lamps.size(); ++light) {
		shares[light] = startsPaths(lights, light) ? lights.lamps[light].emitted : 0;
		total += shares[light];
	}
	for (double& share : shares) {
		share = total > 0 ? share / total : 0;
	}
	return shares;
}

Colour gradientColour(const GradientSky& sky, const math::Vector& direction) {
	const double height = math::dot(sky.up, direction);
	const Colour far    = height >= 0 ? sky.zenith : sky.ground;
	const double share  = std::sqrt(std::abs(height));
	return sky.horizon * (1 - share) + far * share;
}

Spectrum4 skyRadiance(const GradientSky& sky, const math::Vector& direction, const Wavelengths& wavelengths) {
	return spectrumOf(gradientColour(sky, direction), wavelengths);
}

}
