#include "plugins/geometry/sphere_trace/sphere_trace.h"

#include "sdf/numbers.h"
#include "sdf/program.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace kodanuki {

using SpherePoint = std::array<double, 3>;

constexpr double sphereInset   = 2 * sphereTolerance;
constexpr double sphereTie     = 1e-12;
constexpr double sphereGraze   = 0.02;
constexpr double sphereMinPart = 1e-12;

struct SphereMarch {
	const std::vector<Program>& programs;
	const Scene& scene;
	const Ray& ray;
};

struct SphereSpan {
	double lower;
	double upper;
};

struct SphereField {
	double gradient[3];
	Source source;
};

Region reachRegion(const Shot& shot) {
	const math::Vector& target = shot.camera.target;
	const double reach         = shot.render.far;
	return Region {
		.lower = { target.x - reach, target.y - reach, target.z - reach },
		.size  = { 2 * reach, 2 * reach, 2 * reach },
	};
}

SpherePoint spherePoint(const Ray& ray, double t) {
	return { ray.origin.x + ray.direction.x * t, ray.origin.y + ray.direction.y * t, ray.origin.z + ray.direction.z * t };
}

double sphereSlope(double part) {
	return 1 / (std::abs(part) < sphereMinPart ? std::copysign(sphereMinPart, part) : part);
}

SphereSpan sphereSpan(const Region& region, const Ray& ray) {
	SphereSpan span = {
		.lower = 0,
		.upper = ray.far,
	};

	for (int axis = 0; axis < 3; ++axis) {
		const double slope = sphereSlope(math::component(ray.direction, axis));
		const double a     = (region.lower[axis] - math::component(ray.origin, axis)) * slope;
		const double b     = (region.lower[axis] + region.size[axis] - math::component(ray.origin, axis)) * slope;
		span.lower         = std::max(span.lower, std::min(a, b));
		span.upper         = std::min(span.upper, std::max(a, b));
	}
	return span;
}

std::vector<double> sphereValues(const SphereMarch& march, double t) {
	thread_local std::vector<double> registers;
	const SpherePoint p = spherePoint(march.ray, t);

	const Probe probe = {
		.p         = { p[0], p[1], p[2] },
		.registers = registers,
	};

	std::vector<double> values;
	for (const Program& program : march.programs) {
		values.push_back(programDistance(program, probe));
	}
	return values;
}

std::uint32_t sphereLeast(const std::vector<double>& values) {
	std::uint32_t least = 0;
	for (std::uint32_t k = 0; k < values.size(); ++k) {
		least = values[k] < values[least] ? k : least;
	}
	return least;
}

double sphereNearest(const std::vector<double>& values) {
	double nearest = values.empty() ? 0 : std::abs(values[0]);
	for (const double value : values) {
		nearest = std::min(nearest, std::abs(value));
	}
	return nearest;
}

std::uint32_t sphereWinner(const SphereMarch& march, const std::vector<double>& values, double t) {
	const SpherePoint p          = spherePoint(march.ray, t);
	const double band            = sphereTie * (1 + std::max({ std::abs(p[0]), std::abs(p[1]), std::abs(p[2]) }));
	const std::uint32_t first    = sphereLeast(values);
	const std::vector<double> on = sphereValues(march, t + sphereInset);
	std::uint32_t winner         = first;
	for (std::uint32_t k = 0; k < values.size(); ++k) {
		winner = values[k] - values[first] <= band && on[k] < on[winner] ? k : winner;
	}
	return winner;
}

SphereField sphereField(const Program& program, const SpherePoint& p) {
	thread_local std::vector<math::Dual> registers;
	const math::Dual zero = math::constantAs<math::Dual>(0);

	const Inputs<math::Dual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::Dual>(p[0], 0), math::coordinateAs<math::Dual>(p[1], 1), math::coordinateAs<math::Dual>(p[2], 2) },
		.n         = { zero, zero, zero },
	};

	const KeptSteps whole  = wholeSteps(program);
	const math::Dual value = evaluateKept(whole, inputs, registers);
	return SphereField {
		.gradient = { value.gradient[0], value.gradient[1], value.gradient[2] },
		.source   = keptSourceOf(whole, registers),
	};
}

math::Vector sphereGrazed(const Ray& ray, const math::Vector& normal, double along) {
	return math::normalized(normal - ray.direction * (along + sphereGraze));
}

math::Vector sphereNormal(const Ray& ray, const double* g) {
	const math::Vector gradient = { g[0], g[1], g[2] };
	const double length         = math::length(gradient);
	if (!(length > 0)) {
		return -ray.direction;
	}
	const math::Vector normal = gradient / length;
	const double along        = math::dot(normal, ray.direction);
	if (ray.inside) {
		return along < 0 ? -normal : normal;
	}
	return along > 0 ? sphereGrazed(ray, normal, along) : normal;
}

Hit sphereHit(const SphereMarch& march, double t, const SphereField& field) {
	const SpherePoint p = spherePoint(march.ray, t);
	return Hit {
		.kind     = HitKind::Surface,
		.distance = float(t),
		.point    = { p[0], p[1], p[2] },
		.normal   = sphereNormal(march.ray, field.gradient),
		.leaf     = field.source.leaf,
		.other    = field.source.other,
		.share    = float(field.source.share),
		.loosened = sphereTolerance,
		.leafKind = LeafKind::Held,
		.cell     = noLeaf,
	};
}

Hit sphereLanded(const SphereMarch& march, const std::vector<double>& values, double t) {
	const std::uint32_t winner = sphereWinner(march, values, t);
	return sphereHit(march, t, sphereField(march.programs[winner], spherePoint(march.ray, t)));
}

bool sphereOtherMaterial(const SphereMarch& march, const SphereField& past) {
	return past.source.leaf != noLeaf && march.scene.leaves[past.source.leaf].material != march.ray.within;
}

std::optional<Hit> sphereCrossing(const SphereMarch& march, const std::vector<double>& values, double t) {
	const std::vector<double> past = sphereValues(march, t + sphereInset);
	const std::uint32_t least      = sphereLeast(past);
	if (past[least] > 0) {
		return sphereLanded(march, values, t);
	}
	const SphereField field = sphereField(march.programs[least], spherePoint(march.ray, t + sphereInset));
	if (sphereOtherMaterial(march, field)) {
		return sphereHit(march, t, field);
	}
	return std::nullopt;
}

Hit sphereFromInside(const SphereMarch& march, const SphereSpan& span) {
	double t = span.lower;
	for (int step = 0; step < sphereSteps && t <= span.upper; ++step) {
		const std::vector<double> values = sphereValues(march, t);
		const double nearest             = sphereNearest(values);
		const std::optional<Hit> hit     = nearest < sphereTolerance ? sphereCrossing(march, values, t) : std::nullopt;
		if (hit) {
			return *hit;
		}
		t += nearest < sphereTolerance ? sphereInset : nearest;
	}
	return missedHit();
}

Hit sphereFromOutside(const SphereMarch& march, const SphereSpan& span) {
	double t = span.lower;
	for (int step = 0; step < sphereSteps && t <= span.upper; ++step) {
		const std::vector<double> values = sphereValues(march, t);
		const double least               = values[sphereLeast(values)];
		if (least < sphereTolerance) {
			return sphereLanded(march, values, t);
		}
		t += least;
	}
	return missedHit();
}

Hit sphereTraced(const Geometry& geometry, const Ray& ray) {
	const SphereMarch march = {
		.programs = *static_cast<const std::vector<Program>*>(geometry.world),
		.scene    = geometry.scene,
		.ray      = ray,
	};

	const SphereSpan span = sphereSpan(geometry.region, ray);
	if (march.programs.empty() || span.upper < span.lower) {
		return missedHit();
	}
	return ray.inside ? sphereFromInside(march, span) : sphereFromOutside(march, span);
}

Geometry sphereGeometry(const Scene& scene, const Region& region) {
	return Geometry {
		.scene  = scene,
		.region = region,
		.world  = &scene.objectPrograms,
		.kernel = sphereTraced,
	};
}

} // namespace kodanuki
