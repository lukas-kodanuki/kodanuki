#include "plugins/geometry/tree/hit.h"
#include "sdf/numbers.h"
#include "sdf/program.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace kodanuki {

using Normal = std::array<double, 3>;

constexpr double tieShare    = 1e-12;
constexpr double grazeMargin = 0.02;

Sourced unsourced() {
	return Sourced {
		.value    = std::numeric_limits<double>::infinity(),
		.gradient = { 0, 0, 0 },
		.leaf     = noLeaf,
		.other    = noLeaf,
		.share    = 0,
	};
}

math::Dual dualAt(const KeptSteps& program, const double* p, std::vector<math::Dual>& registers) {
	const math::Dual zero = math::constantAs<math::Dual>(0);

	const Inputs<math::Dual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::Dual>(p[0], 0), math::coordinateAs<math::Dual>(p[1], 1), math::coordinateAs<math::Dual>(p[2], 2) },
		.n         = { zero, zero, zero },
	};

	return evaluateKept(program, inputs, registers);
}

Sourced surfaceOf(const KeptSteps& program, const std::vector<math::Dual>& registers) {
	const math::Dual& value = registers[program.result];
	const Source source     = keptSourceOf(program, registers);
	return Sourced {
		.value    = value.value,
		.gradient = { value.gradient[0], value.gradient[1], value.gradient[2] },
		.leaf     = source.leaf,
		.other    = source.other,
		.share    = source.share,
	};
}

KeptSteps listedProgram(const FieldQuery& query, std::uint32_t k) {
	const Tree& tree                  = treeOf(query.geometry);
	const ProgramReference& reference = tree.references[tree.lists[query.list].first + k];
	if (query.beyond) {
		return wholeSteps(query.geometry.scene.objectPrograms[referencedObject(tree, reference)]);
	}
	return referencedSteps(query.geometry.scene, tree, reference);
}

FieldQuery beyondCell(const FieldQuery& query) {
	FieldQuery beyond = query;
	beyond.beyond     = true;
	return beyond;
}

double pointValueIn(const KeptSteps& program, const double* p, std::vector<double>& registers) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { p[0], p[1], p[2] },
		.n         = { 0, 0, 0 },
	};

	return evaluateKept(program, inputs, registers);
}

double pointValue(const KeptSteps& program, const double* p) {
	thread_local std::vector<double> registers;
	return pointValueIn(program, p, registers);
}

double tieBand(const double* p) {
	return tieShare * (1 + std::max({ std::abs(p[0]), std::abs(p[1]), std::abs(p[2]) }));
}

std::array<double, 3> insetPoint(const FieldQuery& query) {
	const double* p = query.point;
	const double t  = query.inset;
	return { p[0] + query.toward.x * t, p[1] + query.toward.y * t, p[2] + query.toward.z * t };
}

std::uint32_t tiedCount(const FieldQuery& query, const std::vector<double>& values, std::uint32_t first) {
	const double band   = tieBand(query.point);
	std::uint32_t count = 0;
	for (const double value : values) {
		count += value - values[first] > band ? 0 : 1;
	}
	return count;
}

std::uint32_t tieWinner(const FieldQuery& query, const std::vector<double>& values, std::uint32_t first) {
	if (tiedCount(query, values, first) < 2) {
		return first;
	}
	const double band                 = tieBand(query.point);
	const std::array<double, 3> inset = insetPoint(query);
	std::uint32_t winner              = first;
	double least                      = std::numeric_limits<double>::infinity();
	for (std::uint32_t k = 0; k < values.size(); ++k) {
		if (values[k] - values[first] > band) {
			continue;
		}
		const double value = pointValue(listedProgram(beyondCell(query), k), inset.data());
		winner             = value < least ? k : winner;
		least              = std::min(least, value);
	}
	return winner;
}

std::uint32_t nearestOfValues(const FieldQuery& query, const std::vector<double>& values) {
	std::uint32_t first = 0;
	for (std::uint32_t k = 0; k < values.size(); ++k) {
		first = values[k] < values[first] ? k : first;
	}
	return tieWinner(query, values, first);
}

std::uint32_t nearestProgram(const FieldQuery& query, std::uint32_t count) {
	thread_local std::vector<double> values;
	thread_local std::vector<double> registers;
	values.clear();
	for (std::uint32_t k = 0; k < count; ++k) {
		values.push_back(pointValueIn(listedProgram(query, k), query.point, registers));
	}
	return nearestOfValues(query, values);
}

Sourced winnerSurface(const FieldQuery& query, std::uint32_t winner) {
	thread_local std::vector<math::Dual> registers;
	dualAt(listedProgram(query, winner), query.point, registers);
	return surfaceOf(listedProgram(query, winner), registers);
}

Sourced surfaceAt(const FieldQuery& query) {
	const std::uint32_t count = treeOf(query.geometry).lists[query.list].count;
	if (count == 0) {
		return unsourced();
	}
	return winnerSurface(query, count == 1 ? 0 : nearestProgram(query, count));
}

std::array<double, 3> pointAlong(const HitQuery& query) {
	const double t = query.distance;
	return { query.ray.origin.x + query.ray.direction.x * t, query.ray.origin.y + query.ray.direction.y * t, query.ray.origin.z + query.ray.direction.z * t };
}

FieldQuery fieldQuery(const HitQuery& query, std::uint32_t list, const std::array<double, 3>& point) {
	return FieldQuery {
		.geometry = query.geometry,
		.list     = list,
		.point    = point.data(),
		.toward   = query.ray.direction,
		.inset    = query.inset,
		.beyond   = false,
	};
}

Normal reversedRay(const Ray& ray) {
	return { -ray.direction.x, -ray.direction.y, -ray.direction.z };
}

Normal grazedNormal(const Ray& ray, const Normal& normal, double along) {
	const double past   = along + grazeMargin;
	const Normal bent   = { normal[0] - past * ray.direction.x, normal[1] - past * ray.direction.y, normal[2] - past * ray.direction.z };
	const double length = std::sqrt(bent[0] * bent[0] + bent[1] * bent[1] + bent[2] * bent[2]);
	return { bent[0] / length, bent[1] / length, bent[2] / length };
}

Normal facedNormal(const Ray& ray, const double* g) {
	const double length = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
	if (!(length > 0)) {
		return reversedRay(ray);
	}
	const double along  = g[0] * ray.direction.x + g[1] * ray.direction.y + g[2] * ray.direction.z;
	const Normal normal = { g[0] / length, g[1] / length, g[2] / length };
	if (ray.inside) {
		return along < 0 ? Normal { -normal[0], -normal[1], -normal[2] } : normal;
	}
	return along > 0 ? grazedNormal(ray, normal, along / length) : normal;
}

Normal sideNormal(const Ray& ray, int entered) {
	if (entered < 0) {
		return reversedRay(ray);
	}
	const int axis    = entered / 2;
	const double side = (entered & 1) != 0 ? 1 : -1;
	return { axis == 0 ? side : 0, axis == 1 ? side : 0, axis == 2 ? side : 0 };
}

struct Found {
	HitKind kind;
	const Sourced& surface;
	const Normal& normal;
};

Hit hitAt(const HitQuery& query, const Found& found, const std::array<double, 3>& point) {
	return Hit {
		.kind     = found.kind,
		.distance = float(query.distance),
		.point    = { point[0], point[1], point[2] },
		.normal   = { found.normal[0], found.normal[1], found.normal[2] },
		.leaf     = found.surface.leaf,
		.other    = found.surface.other,
		.share    = float(found.surface.share),
		.loosened = 0,
		.leafKind = LeafKind::Held,
		.cell     = noLeaf,
	};
}

Hit leafHit(const HitQuery& query, const LeafField& read) {
	const SurfaceLeaf& leaf           = treeOf(query.geometry).surfaces[read.surface];
	const std::array<double, 3> point = pointAlong(query);
	const Normal normal               = facedNormal(query.ray, read.field.gradient);

	const Found found = {
		.kind    = HitKind::Surface,
		.surface = read.field,
		.normal  = normal,
	};

	Hit hit      = hitAt(query, found, point);
	hit.loosened = loosenedOf(leaf);
	hit.leafKind = leaf.kind;
	hit.cell     = read.surface;
	return hit;
}

Sourced passedInto(const HitQuery& query, std::uint32_t surface) {
	const HitQuery past = {
		.geometry = query.geometry,
		.ray      = query.ray,
		.distance = query.distance + query.inset,
		.inset    = query.inset,
	};

	const std::array<double, 3> point = pointAlong(past);
	return surfaceAt(beyondCell(fieldQuery(past, treeOf(query.geometry).surfaces[surface].programs, point)));
}

Sourced passedWithin(const HitQuery& query, std::uint32_t surface) {
	const HitQuery past = {
		.geometry = query.geometry,
		.ray      = query.ray,
		.distance = query.distance + query.inset,
		.inset    = query.inset,
	};

	const std::array<double, 3> point = pointAlong(past);
	return surfaceAt(fieldQuery(past, treeOf(query.geometry).surfaces[surface].programs, point));
}

Hit passingHit(const HitQuery& query, std::uint32_t surface, bool within) {
	const Sourced field = within ? passedWithin(query, surface) : passedInto(query, surface);

	const LeafField read = {
		.surface = surface,
		.field   = field,
	};

	return leafHit(query, read);
}

Hit nearestSurfaceHit(const HitQuery& query, const NearestProgram& nearest) {
	const SurfaceLeaf& leaf           = treeOf(query.geometry).surfaces[nearest.surface];
	const std::array<double, 3> point = pointAlong(query);
	const Sourced field               = winnerSurface(fieldQuery(query, leaf.programs, point), nearest.program);

	const LeafField read = {
		.surface = nearest.surface,
		.field   = field,
	};

	return leafHit(query, read);
}

Hit surfaceHit(const HitQuery& query, std::uint32_t surface) {
	const SurfaceLeaf& leaf           = treeOf(query.geometry).surfaces[surface];
	const std::array<double, 3> point = pointAlong(query);
	const Sourced field               = surfaceAt(fieldQuery(query, leaf.programs, point));

	const LeafField read = {
		.surface = surface,
		.field   = field,
	};

	return leafHit(query, read);
}

Hit solidHit(const HitQuery& query, std::uint32_t solid, int entered) {
	const std::array<double, 3> point = pointAlong(query);
	const Sourced field               = surfaceAt(fieldQuery(query, treeOf(query.geometry).solids[solid].programs, point));
	const Normal normal               = sideNormal(query.ray, entered);

	const Found found = {
		.kind    = HitKind::Solid,
		.surface = field,
		.normal  = normal,
	};

	return hitAt(query, found, point);
}

}
