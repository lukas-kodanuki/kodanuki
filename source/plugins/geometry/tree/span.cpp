#include "plugins/geometry/tree/span.h"
#include "plugins/geometry/tree/hit.h"
#include "plugins/geometry/tree/pass.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace kodanuki {

constexpr double insetShare = 1e-3;

float largerOf(float a, float b) {
	return b < a ? a : b;
}

float smallerOf(float a, float b) {
	return b < a ? b : a;
}

bool isUpper(int step, int axis) {
	return ((step >> axis) & 1) != 0;
}

Met missedMet() {
	return Met {
		.kind     = MetKind::Missed,
		.distance = 0,
		.payload  = 0,
		.entered  = -1,
		.side     = 0,
		.face     = -1,
		.unit     = { 0, 0, 0 },
	};
}

Middles middlesOf(const CellSpan& span) {
	return Middles {
		.at = { 0.5f * (span.near[0] + span.far[0]), 0.5f * (span.near[1] + span.far[1]), 0.5f * (span.near[2] + span.far[2]) },
	};
}

CellSpan halvedSpan(const CellSpan& span, const Middles& middles, int step) {
	CellSpan half = span;
	for (int axis = 0; axis < 3; ++axis) {
		const bool up      = isUpper(step, axis);
		const float middle = middles.at[axis];
		half.near[axis]    = up ? middle : span.near[axis];
		half.far[axis]     = up ? span.far[axis] : middle;
		half.lower         = up ? largerOf(half.lower, middle) : half.lower;
		half.upper         = up ? half.upper : smallerOf(half.upper, middle);
	}
	return half;
}

bool crosses(const CellSpan& span) {
	return span.lower < span.upper;
}

Leg legOf(const Walker& walker, const CellSpan& span) {
	Leg leg = {
		.base = { 0, 0, 0 },
		.rate = { 0, 0, 0 },
	};

	for (int axis = 0; axis < 3; ++axis) {
		const bool falling  = isUpper(walker.descending, axis);
		const float lower   = falling ? span.far[axis] : span.near[axis];
		const float upper   = falling ? span.near[axis] : span.far[axis];
		const float gap     = upper - lower;
		const float slope   = gap != 0.0f ? 1.0f / gap : 0.0f;
		const float atEntry = (span.lower - lower) * slope;
		leg.base[axis]      = atEntry;
		leg.rate[axis]      = (span.upper - lower) * slope - atEntry;
	}
	return leg;
}

int enteredSide(const Walker& walker, const CellSpan& span) {
	for (int axis = 0; axis < 3; ++axis) {
		if (span.near[axis] == span.lower) {
			return axis * 2 + (isUpper(walker.descending, axis) ? 1 : 0);
		}
	}
	return -1;
}

double reciprocalOf(double part) {
	return std::abs(part) >= leastPart ? 1.0 / part : std::copysign(1.0 / leastPart, part);
}

double smallestSide(const Walker& walker, const CellSpan& span) {
	double side = std::numeric_limits<double>::infinity();
	for (int axis = 0; axis < 3; ++axis) {
		side = std::min(side, std::abs(double(span.far[axis]) - double(span.near[axis])) / std::abs(reciprocalOf(math::component(walker.ray.direction, axis))));
	}
	return side;
}

Met solidMet(const Walker& walker, std::uint32_t solid, const CellSpan& span) {
	return Met {
		.kind     = MetKind::Solid,
		.distance = double(span.lower),
		.payload  = solid,
		.entered  = enteredSide(walker, span),
		.side     = smallestSide(walker, span),
		.face     = -1,
		.unit     = { 0, 0, 0 },
	};
}

double legDistance(const CellSpan& span, float at) {
	const double entered = double(span.lower);
	return entered + double(at) * (double(span.upper) - entered);
}

double legLength(const CellSpan& span) {
	return double(span.upper) - double(span.lower);
}

HitQuery passingQuery(const LeafPlace& place, float at) {
	return HitQuery {
		.geometry = place.walker.geometry,
		.ray      = place.walker.ray,
		.distance = legDistance(place.span, at),
		.inset    = insetShare * legLength(place.span),
	};
}

bool readsCellPast(const LeafPlace& place, const HitQuery& query) {
	return passReadsCellPast() && query.distance + 2 * query.inset < double(place.span.upper) && query.distance > double(place.span.lower);
}

bool intoOtherMaterial(const LeafPlace& place, float at) {
	const HitQuery query = passingQuery(place, at);
	const Sourced past   = readsCellPast(place, query) ? passedWithin(query, place.surface) : passedInto(query, place.surface);
	return past.leaf != noLeaf && place.walker.geometry.scene.leaves[past.leaf].material != place.walker.ray.within;
}

Met passingMet(const LeafPlace& place, float at) {
	return Met {
		.kind     = MetKind::Passing,
		.distance = legDistance(place.span, at),
		.payload  = place.surface,
		.entered  = readsCellPast(place, passingQuery(place, at)) ? 1 : -1,
		.side     = legLength(place.span),
		.face     = -1,
		.unit     = { 0, 0, 0 },
	};
}

Met landingMet(const LeafPlace& place, const Landing& landing) {
	for (int k = 0; k < landing.passings.count; ++k) {
		const float at = landing.passings.at[std::size_t(k)];
		if (intoOtherMaterial(place, at)) {
			return passingMet(place, at);
		}
	}
	if (!landing.found) {
		return missedMet();
	}
	const Leg leg = legOf(place.walker, place.span);
	return Met {
		.kind     = MetKind::Surface,
		.distance = legDistance(place.span, landing.at),
		.payload  = place.surface,
		.entered  = -1,
		.side     = smallestSide(place.walker, place.span),
		.face     = landing.face,
		.unit     = { leg.base[0] + landing.at * leg.rate[0], leg.base[1] + landing.at * leg.rate[1], leg.base[2] + landing.at * leg.rate[2] },
	};
}

LeafRun surfaceLeafRun(const Tree& tree, std::uint32_t surface) {
	return leafRun(tree.runs.data() + tree.surfaces[surface].run);
}

Met surfaceRunMet(const Walker& walker, const SurfaceRun& leaf, const CellSpan& span) {
	const LeafVisit visit = {
		.run    = leaf.run,
		.leg    = legOf(walker, span),
		.inside = walker.ray.inside,
	};

	const LeafPlace place = {
		.walker  = walker,
		.surface = leaf.surface,
		.span    = span,
	};

	return landingMet(place, landLeaf(visit));
}

bool surfaceReached(const Walker& walker, const SurfaceRun& leaf, const CellSpan& span) {
	const LeafVisit visit = {
		.run    = leaf.run,
		.leg    = legOf(walker, span),
		.inside = false,
	};

	return reachesSurface(visit);
}

Met surfaceMet(const Walker& walker, std::uint32_t surface, const CellSpan& span) {
	const LeafRun run = surfaceLeafRun(treeOf(walker.geometry), surface);

	const SurfaceRun leaf = {
		.surface = surface,
		.run     = run,
	};

	return surfaceRunMet(walker, leaf, span);
}

CellEntry slotEntry(const PackedNode& node, int slot) {
	const CellKind kind = slotKind(node, slot);
	return CellEntry {
		.kind    = kind,
		.payload = kind == CellKind::Empty ? 0 : slotPayload(node, slot),
	};
}

int descendingOf(const Ray& ray) {
	return (std::signbit(ray.direction.x) ? 1 : 0) | (std::signbit(ray.direction.y) ? 2 : 0) | (std::signbit(ray.direction.z) ? 4 : 0);
}

CellSpan rootSpan(const Walker& walker) {
	const Ray& ray = walker.ray;

	CellSpan span = {
		.near  = { 0, 0, 0 },
		.lower = 0,
		.far   = { 0, 0, 0 },
		.upper = float(ray.far),
	};

	for (int axis = 0; axis < 3; ++axis) {
		const double slope = reciprocalOf(math::component(ray.direction, axis));
		const double lower = (treeOf(walker.geometry).region.lower[axis] - math::component(ray.origin, axis)) * slope;
		const double upper = (treeOf(walker.geometry).region.lower[axis] + treeOf(walker.geometry).region.size[axis] - math::component(ray.origin, axis)) * slope;
		const bool falling = isUpper(walker.descending, axis);
		span.near[axis]    = float(falling ? upper : lower);
		span.far[axis]     = float(falling ? lower : upper);
		span.lower         = largerOf(span.lower, span.near[axis]);
		span.upper         = smallerOf(span.upper, span.far[axis]);
	}
	return span;
}

HitQuery hitQueryOf(const Walker& walker, const Met& met) {
	return HitQuery {
		.geometry = walker.geometry,
		.ray      = walker.ray,
		.distance = met.distance,
		.inset    = insetShare * met.side,
	};
}

Hit facedHit(const Walker& walker, const HitQuery& query, const Met& met, const LandedFace& landed) {
	const std::array<float, 3>& g = landed.gradient;
	const double along            = double(g[0]) * walker.ray.direction.x + double(g[1]) * walker.ray.direction.y + double(g[2]) * walker.ray.direction.z;
	const double sign             = along > 0 ? -1 : 1;

	const Sourced field = {
		.value    = 0,
		.gradient = { sign * g[0], sign * g[1], sign * g[2] },
		.leaf     = landed.leaf,
		.other    = noLeaf,
		.share    = 0,
	};

	const LeafField read = {
		.surface = met.payload,
		.field   = field,
	};

	return leafHit(query, read);
}

Hit landedHit(const Walker& walker, const HitQuery& query, const Met& met) {
	const Tree& tree        = treeOf(walker.geometry);
	const LandedFace landed = passLandedFace(tree.runs.data() + tree.surfaces[met.payload].run, met.face, met.unit);
	if (landed.exact) {
		return facedHit(walker, query, met, landed);
	}
	if (landed.owner < tree.lists[tree.surfaces[met.payload].programs].count) {
		const NearestProgram nearest = {
			.surface = met.payload,
			.program = landed.owner,
		};

		return nearestSurfaceHit(query, nearest);
	}
	return surfaceHit(query, met.payload);
}

Hit hitOf(const Walker& walker, const Met& met) {
	const HitQuery query = hitQueryOf(walker, met);
	switch (met.kind) {
		case MetKind::Missed: return missedHit();
		case MetKind::Surface: return landedHit(walker, query, met);
		case MetKind::Passing: return passingHit(query, met.payload, met.entered > 0);
		case MetKind::Solid: return solidHit(query, met.payload, met.entered);
	}

	return missedHit();
}

}
