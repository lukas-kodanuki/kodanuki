#include "extras/strategy_correct_join/join.h"

#include <cmath>

namespace kodanuki {

struct JoinView {
	math::Vector onLens;
	math::Vector toward;
	double distance;
	double faced;
	double lit;
	double slant;
	Raster raster;
};

struct SeenJoin {
	const LightPath& path;
	const JoinView& view;
};

bool anyShare(const std::vector<double>& shares) {
	for (const double share : shares) {
		if (share > 0) {
			return true;
		}
	}
	return false;
}

Strategies withLightShares(const Strategies& strategies, const Tracing& tracing) {
	const bool relays            = strategyOn(strategies, StrategyMember::Join) && !tracing.sampling.mirrors.balls.empty();
	const bool leads             = strategyOn(strategies, StrategyMember::Connect);
	const std::vector<double> by = relays || leads ? lampShares(tracing.lights) : std::vector<double> {};
	const bool shared            = anyShare(by);
	Strategies with              = strategies;
	with.lampShares              = shared ? by : std::vector<double> {};
	with.joinShare               = shared && relays ? strategies.joinShare : 0;
	return with;
}

math::Vector joinLensPoint(const Tracing& tracing, const LightPath& path) {
	if (!(tracing.lens.aperture > 0)) {
		return tracing.lens.eye;
	}

	const NumberQuery u = {
		.numbers    = path.numbers,
		.coordinate = coordinateOf(path.depth + 1, Draw::JoinLensU),
	};

	const NumberQuery v = {
		.numbers    = path.numbers,
		.coordinate = coordinateOf(path.depth + 1, Draw::JoinLensV),
	};

	const math::UnitPair pair = {
		.u = numberAt(tracing.sampling, u),
		.v = numberAt(tracing.sampling, v),
	};

	return lensPoint(tracing.lens, pair);
}

JoinView joinView(const Tracing& tracing, const LightPath& path) {
	const Vertex& vertex       = path.vertex;
	const math::Vector onLens  = joinLensPoint(tracing, path);
	const math::Vector offset  = onLens - vertex.point;
	const double distance      = math::length(offset);
	const math::Vector toward  = offset * (1 / distance);
	const math::Vector arrived = -path.heading;
	return JoinView {
		.onLens   = onLens,
		.toward   = toward,
		.distance = distance,
		.faced    = math::dot(toward, vertex.surface),
		.lit      = math::dot(arrived, vertex.bsdf.normal),
		.slant    = math::dot(arrived, vertex.surface),
		.raster   = rasterOf(tracing.lens, onLens, vertex.point),
	};
}

bool joinable(const Vertex& vertex, const JoinView& view) {
	const bool facing = view.faced > 0 && view.slant > 0 && view.lit > 0 && math::dot(view.toward, vertex.bsdf.normal) > 0;
	return view.raster.inside && facing && !vertex.sharp && view.distance > vertex.lift;
}

std::uint32_t joinPixel(const Tracing& tracing, const JoinView& view) {
	return std::uint32_t(view.raster.y) * std::uint32_t(tracing.lens.width) + std::uint32_t(view.raster.x);
}

double eyeDensityAt(const Tracing& tracing, const SeenJoin& seen) {
	Path eye    = freshPath(seen.path.numbers, seen.path.wavelengths);
	eye.vertex  = seen.path.vertex;
	eye.heading = -seen.view.toward;
	eye.drawnAt = seen.view.onLens;

	GuideState guide = startedGuide(tracing.sampling, joinPixel(tracing, seen.view));

	const ReachQuery reach = {
		.path   = eye,
		.lights = tracing.lights,
	};

	guideReached(tracing.sampling, reach, guide);

	const BsdfPair pair = {
		.outgoing    = -eye.heading,
		.incoming    = -seen.path.heading,
		.wavelengths = eye.wavelengths,
	};

	const DirectionDensityQuery query = {
		.path     = eye,
		.guide    = guide,
		.incoming = -seen.path.heading,
		.own      = bsdfDensity(tracing.materials, eye.vertex.bsdf, pair),
	};

	return directionDensity(tracing.sampling, query);
}

double joinWeight(const Tracing& tracing, const SeenJoin& seen) {
	const JoinView& view        = seen.view;
	const StrategyCounts counts = countsOf(tracing.strategies);

	const JoinEnding ending = {
		.weighing = seen.path.weighing,
		.forward  = eyeDensityAt(tracing, seen),
		.lens     = lensDensity(tracing.lens, view.raster.cosine) * view.faced / (view.distance * view.distance),
		.capped   = seen.path.depth + 1 >= tracing.limits.maximumDepth,
		.counts   = counts,
	};

	const Densities densities = joinDensities(ending);
	return weightOf(densities, densities.join);
}

Spectrum4 joinedRadiance(const Tracing& tracing, const SeenJoin& seen) {
	const LightPath& path = seen.path;
	const JoinView& view  = seen.view;
	const double pixels   = double(tracing.lens.width) * double(tracing.lens.height);
	const double squared  = view.distance * view.distance;
	const double seeing   = pixels * lensDensity(tracing.lens, view.raster.cosine) / squared;

	const BsdfPair pair = {
		.outgoing    = view.toward,
		.incoming    = -path.heading,
		.wavelengths = path.wavelengths,
	};

	const Spectrum4 value = bsdfValue(tracing.materials, path.vertex.bsdf, pair);
	return path.throughput * value * (view.faced * view.lit / view.slant * seeing * joinWeight(tracing, seen));
}

bool insideSolid(const Tracing& tracing, const math::Vector& point, std::vector<double>& registers) {
	const Probe probe = {
		.p         = point,
		.registers = registers,
	};

	return sceneDistance(tracing.scene, probe) < 0;
}

bool joinEnds(const Tracing& tracing, const LightPath& path) {
	const RuleEnding ending = {
		.light    = path.lamp,
		.bits     = path.rules.bits,
		.standing = path.rules.standing,
	};

	return lightEnds(tracing.features, ending) && !passes(tracing.features, path.vertex.leaf, Passage::ToLens);
}

LensLine joinedLine(const Tracing& tracing, const LightStanding& standing) {
	const LightPath& path = standing.path;
	const JoinView view   = joinView(tracing, path);
	if (!joinable(path.vertex, view) || !joinEnds(tracing, path)) {
		return noLensLine();
	}

	const SeenJoin seen = {
		.path = path,
		.view = view,
	};

	const Spectrum4 radiance  = joinedRadiance(tracing, seen);
	const math::Vector origin = path.vertex.point + path.vertex.surface * path.vertex.lift;
	if (!(laneMaximum(radiance) > 0) || insideSolid(tracing, origin, standing.registers)) {
		return noLensLine();
	}

	const Shadow shadow = {
		.origin    = origin,
		.direction = view.toward,
		.reach     = view.distance - path.vertex.lift,
		.cell      = noCell,
		.within    = noMaterial,
		.medium    = noMaterial,
		.passage   = Passage::ToLens,
		.crossings = 0,
	};

	return LensLine {
		.found    = true,
		.shadow   = shadow,
		.radiance = radiance,
		.pixel    = joinPixel(tracing, view),
	};
}

}
