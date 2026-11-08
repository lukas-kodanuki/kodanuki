#include "transport/trace.h"

#include "transport/light_walk.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kodanuki {

constexpr double survivalCeiling = 0.95;
constexpr double infiniteReach   = std::numeric_limits<double>::infinity();
constexpr int legEvents          = 16;

struct MetLight {
	const Path& path;
	std::uint32_t light;
	double limit;
};

struct EmissionSite {
	std::uint32_t light;
	math::Vector point;
	math::Vector toward;
	double distance;
};

struct Stop {
	Hit hit;
	bool collided;
};

struct GlowSight {
	Colour glow;
	math::Vector place;
	std::uint32_t leaf;
	const RuleState& rules;
};

struct Reservoir {
	Offer chosen;
	double picked;
	double total;
	double choice;
};

double drawnNumber(const Tracing& tracing, const Path& path, Draw draw) {
	const NumberQuery query = {
		.numbers    = path.numbers,
		.coordinate = coordinateOf(path.depth, draw),
	};

	return numberAt(tracing.sampling, query);
}

math::UnitPair drawnPair(const Tracing& tracing, const Path& path, Draw first) {
	const NumberQuery query = {
		.numbers    = path.numbers,
		.coordinate = coordinateOf(path.depth, first),
	};

	return pairAt(tracing.sampling, query);
}

PathNumbers pixelNumbers(const Tracing& tracing, const PixelSample& sample) {
	const std::uint64_t place = (std::uint64_t(std::uint32_t(sample.x)) << 32) | std::uint64_t(std::uint32_t(sample.y));
	return PathNumbers {
		.stream    = math::mixedBits(tracing.seed) ^ math::mixedBits(place),
		.index     = sample.index,
		.sequenced = true,
	};
}

Wavelengths pixelWavelengths(const PathNumbers& numbers) {
	const math::Key start = {
		.stream     = numbers.stream,
		.index      = ~std::uint64_t(0),
		.coordinate = 0,
	};

	return sampledWavelengths(wavelengthShare(math::hashedNumber(start), numbers.index));
}

math::UnitPair lensPair(const Tracing& tracing, const Path& path) {
	if (!(tracing.lens.aperture > 0)) {
		return { 0.5, 0.5 };
	}
	return drawnPair(tracing, path, Draw::LensU);
}

LensDraw phasedDraw(const Tracing& tracing, const Path& path, const PixelSample& sample) {
	const FilmPlan& film = tracing.film;
	const double factor  = double(film.factor);
	const double column  = double(film.phase % film.factor);
	const double row     = double(film.phase / film.factor);
	return LensDraw {
		.x    = sample.x + (column + drawnNumber(tracing, path, Draw::PixelX)) / factor,
		.y    = sample.y + (row + drawnNumber(tracing, path, Draw::PixelY)) / factor,
		.lens = lensPair(tracing, path),
	};
}

LensDraw filmDraw(const Tracing& tracing, const Path& path, const PixelSample& sample) {
	if (tracing.film.factor != 1) {
		return phasedDraw(tracing, path, sample);
	}
	return LensDraw {
		.x    = sample.x + drawnNumber(tracing, path, Draw::PixelX),
		.y    = sample.y + drawnNumber(tracing, path, Draw::PixelY),
		.lens = lensPair(tracing, path),
	};
}

Ray startedPathIn(const Tracing& tracing, const PixelSample& sample, Path& path) {
	const PathNumbers numbers = pixelNumbers(tracing, sample);
	path                      = freshPath(numbers, pixelWavelengths(numbers));
	const LensDraw draw       = filmDraw(tracing, path, sample);
	const Ray ray             = cameraRay(tracing.lens, draw);
	path.heading              = ray.direction;
	path.drawnAt              = ray.origin;
	path.legFrom              = ray.origin;
	path.weighing             = weighingAtLens(countsOf(tracing.strategies), lensDensity(tracing.lens, math::dot(ray.direction, tracing.lens.forward)));
	return ray;
}

Onward startedPath(const Tracing& tracing, const PixelSample& sample) {
	Path path;
	const Ray ray = startedPathIn(tracing, sample, path);
	return Onward {
		.alive = true,
		.path  = path,
		.ray   = ray,
	};
}

math::Vector lampNormalAt(const Lamp& lamp, const math::Vector& point) {
	const math::Vector none = { 0, 0, 0 };
	switch (lamp.kind) {
		case LightKind::Sphere: return math::normalized(point - lamp.at);
		case LightKind::Quad: return lamp.facing;
		case LightKind::Point:
		case LightKind::Directional:
		case LightKind::Moon:
		case LightKind::Sun: return none;
	}

	return none;
}

double emissionToward(const Tracing& tracing, const EmissionSite& site) {
	const std::vector<double>& shares = tracing.strategies.lampShares;
	if (site.light >= shares.size() || !(shares[site.light] > 0)) {
		return 0;
	}
	const Lamp& lamp = tracing.lights.lamps[site.light];

	const LampPoint point = {
		.light   = site.light,
		.point   = site.point,
		.normal  = lampNormalAt(lamp, site.point),
		.density = lampPointDensity(tracing.lights, site.light),
	};

	const EmissionDensityQuery query = {
		.lights    = tracing.lights,
		.point     = point,
		.direction = site.toward,
	};

	const double direction = emissionDensity(tracing.sampling, query);
	if (lamp.kind == LightKind::Point) {
		return shares[site.light] * direction / (site.distance * site.distance);
	}
	return shares[site.light] * point.density * direction / std::max(std::abs(math::dot(point.normal, site.toward)), 1e-12);
}

Spectrum4 keptOnLeg(const Tracing& tracing, const MediumLeg& leg) {
	if (leg.medium == noMaterial) {
		return flatSpectrum(1);
	}

	const MediumQuery query = {
		.material    = leg.medium,
		.origin      = leg.origin,
		.heading     = leg.heading,
		.distance    = leg.distance,
		.kept        = flatSpectrum(1),
		.wavelengths = leg.wavelengths,
		.use         = leg.use,
		.number      = leg.number,
	};

	return mediumAlong(tracing.features, query).kept;
}

double collisionNumber(const Tracing& tracing, const Path& path) {
	const NumberQuery query = {
		.numbers    = path.numbers,
		.coordinate = coordinateOf(path.depth + legEvents * int(path.crossings), Draw::Collision),
	};

	return numberAt(tracing.sampling, query);
}

MediumLeg walkedLeg(const Tracing& tracing, const Path& path, double distance) {
	return MediumLeg {
		.medium      = path.medium,
		.origin      = path.legFrom,
		.heading     = path.heading,
		.distance    = distance,
		.wavelengths = path.wavelengths,
		.use         = MediumUse::Walking,
		.number      = mediumCollides(tracing.features, path.medium) ? collisionNumber(tracing, path) : 0,
	};
}

Spectrum4 keptAlong(const Tracing& tracing, const Path& path, double distance) {
	return keptOnLeg(tracing, walkedLeg(tracing, path, distance));
}

MediumAnswer collisionOf(const Tracing& tracing, const Path& path, const Hit& hit) {
	const MediumLeg leg = walkedLeg(tracing, path, math::length(hit.point - path.legFrom));

	const MediumQuery query = {
		.material    = leg.medium,
		.origin      = leg.origin,
		.heading     = leg.heading,
		.distance    = leg.distance,
		.kept        = flatSpectrum(1),
		.wavelengths = leg.wavelengths,
		.use         = leg.use,
		.number      = leg.number,
	};

	return mediumAlong(tracing.features, query);
}

Hit collisionHit(const Path& path, const Hit& hit, double at) {
	Hit collision      = hit;
	collision.distance = at;
	collision.point    = path.legFrom + path.heading * at;
	collision.normal   = -path.heading;
	collision.leaf     = noLeaf;
	collision.other    = noLeaf;
	collision.share    = 0;
	collision.loosened = 0;
	return collision;
}

Stop stopOf(const Tracing& tracing, const Path& path, const Hit& hit) {
	if (!mediumCollides(tracing.features, path.medium) || hit.kind == HitKind::Missed) {
		return Stop {
			.hit      = hit,
			.collided = false,
		};
	}
	const MediumAnswer answer = collisionOf(tracing, path, hit);
	return Stop {
		.hit      = answer.collided ? collisionHit(path, hit, answer.at) : hit,
		.collided = answer.collided,
	};
}

Spectrum4 featuredLight(const Tracing& tracing, const MetLight& met, const LightMeet& meet) {
	const Path& path = met.path;
	if (met.light == skyLight(tracing.lights)) {
		const SkyQuery sky = {
			.direction   = path.heading,
			.radiance    = meet.radiance,
			.wavelengths = path.wavelengths,
			.exact       = path.exact,
		};

		return skyAt(tracing.features, sky);
	}

	const LampQuery lamp = {
		.light       = met.light,
		.point       = path.drawnAt,
		.toward      = path.heading,
		.radiance    = meet.radiance,
		.wavelengths = path.wavelengths,
	};

	return lampAt(tracing.features, lamp);
}

double meetAimDensity(const Tracing& tracing, const MetLight& met, const math::Vector& point) {
	const Path& path = met.path;
	if (path.weighing.aimed != Aimed::Every || !strategyOn(tracing.strategies, StrategyMember::Aim)) {
		return 0;
	}

	const AimDensityQuery query = {
		.light    = met.light,
		.from     = path.drawnAt,
		.toward   = path.heading,
		.distance = math::length(point - path.drawnAt),
	};

	return aimDensityAt(tracing.sampling, tracing.lights, query);
}

Spectrum4 lightMet(const Tracing& tracing, const MetLight& met) {
	const Path& path = met.path;

	const MeetQuery query = {
		.light       = met.light,
		.origin      = path.legFrom,
		.direction   = path.heading,
		.limit       = met.limit,
		.wavelengths = path.wavelengths,
	};

	const LightMeet meet = meetLight(tracing.lights, query);
	if (!meet.met) {
		return flatSpectrum(0);
	}

	const EmissionSite site = {
		.light    = met.light,
		.point    = path.legFrom + path.heading * meet.distance,
		.toward   = -path.heading,
		.distance = meet.distance,
	};

	const StrategyCounts counts = countsOf(tracing.strategies);

	const MeetEnding ending = {
		.weighing = path.weighing,
		.aim      = meetAimDensity(tracing, met, site.point),
		.emission = meetReadsEmission(path.weighing, counts) ? emissionToward(tracing, site) : 0,
		.counts   = counts,
	};

	const Densities densities = meetDensities(ending);
	return path.throughput * keptAlong(tracing, path, meet.distance) * featuredLight(tracing, met, meet) * weightOf(densities, densities.meet);
}

bool endsOn(const Tracing& tracing, const RuleState& rules, std::uint32_t light) {
	const RuleEnding ending = {
		.light    = light,
		.bits     = rules.drawn,
		.standing = rules.drawnOn,
	};

	return lightEnds(tracing.features, ending);
}

void metAlong(const Tracing& tracing, const Arrival& arrival, const RuleState& rules) {
	Path& path        = arrival.path;
	const bool found  = arrival.hit.kind != HitKind::Missed;
	const bool camera = path.weighing.aimed == Aimed::Camera;
	if (camera && found) {
		return;
	}
	const bool looked  = (rules.bits & std::uint8_t(RuleBit::Looked)) != 0;
	const double limit = looked ? rules.beyond : (found ? math::length(arrival.hit.point - path.legFrom) : infiniteReach);
	for (std::uint32_t light = 0; light < lightCount(tracing.lights); ++light) {
		if ((camera && !seenByCamera(tracing.lights, light)) || !endsOn(tracing, rules, light)) {
			continue;
		}

		const MetLight asked = {
			.path  = path,
			.light = light,
			.limit = limit,
		};

		path.radiance = path.radiance + lightMet(tracing, asked);
	}
}

double glowMetDensity(const Tracing& tracing, const Path& path, const math::Vector& place) {
	if (path.weighing.aimed != Aimed::Every || !strategyOn(tracing.strategies, StrategyMember::Aim)) {
		return 0;
	}

	const GlowSeen seen = {
		.from   = path.drawnAt,
		.toward = path.heading,
		.place  = place,
	};

	return glowAimDensity(tracing.sampling, seen);
}

void glowMetAt(const Tracing& tracing, Path& path, const GlowSight& sight) {
	const Colour& glow = sight.glow;

	const GlowEnding glowing = {
		.bits = sight.rules.drawn,
		.leaf = sight.leaf,
	};

	if (!(glow.red > 0 || glow.green > 0 || glow.blue > 0) || !glowEnds(tracing.features, glowing)) {
		return;
	}

	const StrategyCounts counts = countsOf(tracing.strategies);

	const MeetEnding ending = {
		.weighing = path.weighing,
		.aim      = glowMetDensity(tracing, path, sight.place),
		.emission = 0,
		.counts   = counts,
	};

	const Densities densities = meetDensities(ending);
	path.radiance             = path.radiance + path.throughput * spectrumOf(glow, path.wavelengths) * weightOf(densities, densities.meet);
}

void metGlow(const Tracing& tracing, Path& path, const RuleState& rules) {
	const GlowSight sight = {
		.glow  = path.vertex.glow,
		.place = path.vertex.point,
		.leaf  = path.vertex.leaf,
		.rules = rules,
	};

	glowMetAt(tracing, path, sight);
}

Vertex surfaceAt(const Tracing& tracing, const Surfacing& surfacing) {
	const Hit& hit = surfacing.hit;

	const Shading shading = {
		.scene     = tracing.scene,
		.looks     = tracing.looks,
		.materials = tracing.materials,
	};

	const ShadingQuery query = {
		.hit       = hit,
		.heading   = surfacing.heading,
		.medium    = surfacing.medium,
		.registers = surfacing.registers,
	};

	const WearQuery wear = {
		.hit       = hit,
		.shaded    = bsdfAt(shading, query),
		.heading   = surfacing.heading,
		.shading   = shading,
		.outside   = math::dot(hit.normal, surfacing.heading) < 0,
		.registers = surfacing.registers,
	};

	Shaded worn = wornAt(tracing.features, wear);

	const InterfaceQuery sides = {
		.material    = worn.bsdf.material,
		.medium      = surfacing.medium,
		.sides       = worn.bsdf.sides,
		.wavelengths = surfacing.wavelengths,
	};

	worn.bsdf.sides = sidesAt(tracing.features, sides);
	return Vertex {
		.point    = hit.point,
		.surface  = math::facingAgainst(hit.normal, surfacing.heading),
		.bsdf     = worn.bsdf,
		.glow     = worn.glow,
		.lift     = tracing.limits.bias + 2 * hit.loosened,
		.leaf     = hit.leaf,
		.cell     = hit.cell,
		.sharp    = isSharp(tracing.materials, worn.bsdf.material),
		.collided = false,
	};
}

Vertex phaseVertex(const Tracing& tracing, const Path& path, const Hit& hit) {
	const Sides sides = {
		.travelling = flatSpectrum(1),
		.beyond     = flatSpectrum(1),
		.parted     = false,
		.inside     = true,
	};

	const Bsdf bsdf = {
		.material = tracing.features.scattering.phases[mediumBody(tracing.features, path.medium)],
		.albedo   = { 1, 1, 1 },
		.normal   = -path.heading,
		.sides    = sides,
	};

	return Vertex {
		.point    = hit.point,
		.surface  = -path.heading,
		.bsdf     = bsdf,
		.glow     = { 0, 0, 0 },
		.lift     = 0,
		.leaf     = noLeaf,
		.cell     = hit.cell,
		.sharp    = false,
		.collided = true,
	};
}

void collidedOn(const Tracing& tracing, const Arrival& arrival) {
	const double distance = math::length(arrival.hit.point - arrival.path.drawnAt);
	Path& path            = arrival.path;
	path.throughput       = path.throughput * keptAlong(tracing, path, math::length(arrival.hit.point - path.legFrom));
	path.vertex           = phaseVertex(tracing, path, arrival.hit);
	path.collisions       = path.collisions + 1;

	const HitFactors factors = {
		.cosine      = 1,
		.distance    = distance,
		.joinable    = false,
		.connectable = false,
	};

	path.weighing = weighingAtHit(path.weighing, factors);
}

Vertex surfacedAt(const Tracing& tracing, const Arrival& arrival, std::vector<double>& registers) {
	const Surfacing surfacing = {
		.hit         = arrival.hit,
		.heading     = arrival.path.heading,
		.medium      = arrival.path.medium,
		.wavelengths = arrival.path.wavelengths,
		.registers   = registers,
	};

	return surfaceAt(tracing, surfacing);
}

void stoodOn(const Tracing& tracing, const Arrival& arrival, const Vertex& vertex) {
	const double distance = math::length(arrival.hit.point - arrival.path.drawnAt);
	Path& path            = arrival.path;
	path.throughput       = path.throughput * keptAlong(tracing, path, math::length(arrival.hit.point - path.legFrom));
	path.vertex           = vertex;

	const HitFactors factors = {
		.cosine      = std::abs(math::dot(arrival.hit.normal, path.heading)),
		.distance    = distance,
		.joinable    = !path.vertex.sharp && path.medium == noMaterial,
		.connectable = !isExact(tracing.materials, path.vertex.bsdf.material) && path.medium == noMaterial,
	};

	path.weighing = weighingAtHit(path.weighing, factors);
}

bool bending(const Sides& sides) {
	return sides.travelling.values[0] != sides.beyond.values[0];
}

bool crossableBody(const Tracing& tracing, std::uint32_t material) {
	if (material == noMaterial || material >= tracing.looks.materials.size()) {
		return false;
	}
	return passable(tracing.looks.materials[material]);
}

bool crossable(const Tracing& tracing, const Vertex& vertex) {
	const bool dark = !(vertex.glow.red > 0 || vertex.glow.green > 0 || vertex.glow.blue > 0);
	return !vertex.collided && dark && !vertex.bsdf.sides.parted && !bending(vertex.bsdf.sides) && crossableBody(tracing, vertex.bsdf.material);
}

Ray crossingRay(const Vertex& vertex, const math::Vector& heading, std::uint32_t medium) {
	return Ray {
		.origin    = vertex.point + heading * vertex.lift,
		.direction = heading,
		.far       = infiniteReach,
		.inside    = medium != noMaterial,
		.within    = medium,
	};
}

RuleState standingAt(const RuleState& rules, std::uint32_t leaf) {
	RuleState standing = rules;
	standing.standing  = leaf;
	return standing;
}

bool aimsFromCollision(const Tracing& tracing, const Path& path) {
	return path.vertex.collided && crossableBody(tracing, mediumBody(tracing.features, path.medium));
}

bool aimable(const Tracing& tracing, const Path& path) {
	if (path.vertex.collided) {
		return aimsFromCollision(tracing, path);
	}
	return path.medium == noMaterial || (path.medium == path.vertex.bsdf.material && bending(path.vertex.bsdf.sides));
}

math::Vector openSide(const Path& path) {
	const math::Vector nowhere = { 0, 0, 0 };
	if (path.vertex.collided) {
		return nowhere;
	}
	return path.vertex.bsdf.sides.inside ? -path.vertex.bsdf.normal : path.vertex.bsdf.normal;
}

bool needsReverse(const StrategyCounts& counts) {
	return counts.join > 0 || counts.connect > 0;
}

double densityBack(const Materials& materials, const TurnBack& turn) {
	const bool other = math::dot(turn.bsdf.normal, turn.drawn) < 0;

	const BsdfPair pair = {
		.outgoing    = turn.drawn,
		.incoming    = -turn.heading,
		.wavelengths = turn.wavelengths,
	};

	return bsdfDensity(materials, other ? turnedBsdf(turn.bsdf) : turn.bsdf, pair);
}

double reverseDensity(const Tracing& tracing, const Path& path, const math::Vector& incoming) {
	const TurnBack turn = {
		.bsdf        = path.vertex.bsdf,
		.heading     = path.heading,
		.drawn       = incoming,
		.wavelengths = path.wavelengths,
	};

	return densityBack(tracing.materials, turn);
}

Offer noOffer() {
	const Shadow shadow = {
		.origin    = { 0, 0, 0 },
		.direction = { 0, 0, 1 },
		.reach     = 0,
		.cell      = noCell,
		.within    = noMaterial,
		.medium    = noMaterial,
		.passage   = Passage::ToLight,
		.crossings = 0,
	};

	return Offer {
		.radiance = flatSpectrum(0),
		.shadow   = shadow,
	};
}

Shadow shadowOf(const Path& path, const math::Vector& open, const LightAim& aim) {
	const Vertex& vertex = path.vertex;
	const bool glance    = aim.cell != noCell;
	return Shadow {
		.origin    = vertex.point + open * vertex.lift,
		.direction = aim.toward,
		.reach     = glance ? aim.distance + vertex.lift : aim.distance - vertex.lift,
		.cell      = aim.cell,
		.within    = vertex.collided ? path.medium : noMaterial,
		.medium    = vertex.collided ? path.medium : noMaterial,
		.passage   = Passage::ToLight,
		.crossings = 0,
	};
}

struct AimedLight {
	const LightAim& aim;
	std::uint32_t light;
	const BsdfAnswer& answer;
};

bool weighedBare(const StrategyCounts& counts, const Path& path) {
	return needsReverse(counts) && path.depth > 0;
}

double bareDensity(const Tracing& tracing, const Path& path, const math::Vector& incoming) {
	const BsdfPair pair = {
		.outgoing    = -path.heading,
		.incoming    = incoming,
		.wavelengths = path.wavelengths,
	};

	return bsdfDensity(tracing.materials, path.vertex.bsdf, pair);
}

double weighedDensity(const Tracing& tracing, const Standing& standing, const OwnDensity& toward) {
	if (weighedBare(standing.counts, standing.path)) {
		return toward.density;
	}

	const DirectionDensityQuery query = {
		.path     = standing.path,
		.guide    = standing.companions.guide,
		.incoming = toward.incoming,
		.own      = toward.density,
	};

	return directionDensity(tracing.sampling, query);
}

OwnDensity ownToward(const AimedLight& aimed) {
	return OwnDensity {
		.incoming = aimed.aim.toward,
		.density  = aimed.answer.density,
	};
}

double aimWeight(const Tracing& tracing, const Standing& standing, const AimedLight& aimed) {
	const Path& path     = standing.path;
	const Vertex& vertex = path.vertex;
	const LightAim& aim  = aimed.aim;

	const EmissionSite site = {
		.light    = aimed.light,
		.point    = vertex.point + aim.toward * aim.distance,
		.toward   = -aim.toward,
		.distance = aim.distance,
	};

	const AimEnding ending = {
		.weighing = path.weighing,
		.forward  = weighedDensity(tracing, standing, ownToward(aimed)),
		.reverse  = aimed.answer.reverse,
		.aim      = aim.density,
		.emission = aimReadsEmission(path.weighing, standing.counts, vertex.sharp) ? emissionToward(tracing, site) : 0,
		.cosine   = vertex.collided ? 1 : std::abs(math::dot(vertex.surface, aim.toward)),
		.sharp    = vertex.sharp,
		.capped   = atDepthCap(standingCounts(path), depthCaps(tracing.limits)),
		.counts   = standing.counts,
	};

	const Densities densities = aimDensities(ending);
	return weightOf(densities, densities.aim);
}

Spectrum4 aimedSky(const Tracing& tracing, const AimedLight& aimed, const Path& path) {
	const SkyQuery sky = {
		.direction   = aimed.aim.toward,
		.radiance    = aimed.aim.raw,
		.wavelengths = path.wavelengths,
		.exact       = false,
	};

	return skyAt(tracing.features, sky) / aimed.aim.density;
}

Spectrum4 aimedArrival(const Tracing& tracing, const AimedLight& aimed, const Standing& standing) {
	if (aimed.light == skyLight(tracing.lights)) {
		return aimedSky(tracing, aimed, standing.path);
	}

	const LampQuery lamp = {
		.light       = aimed.light,
		.point       = standing.path.vertex.point,
		.toward      = aimed.aim.toward,
		.radiance    = aimed.aim.radiance,
		.wavelengths = standing.path.wavelengths,
	};

	return lampAt(tracing.features, lamp);
}

bool aimEnds(const Tracing& tracing, const RuleState& rules, std::uint32_t light) {
	const RuleEnding ending = {
		.light    = light,
		.bits     = rules.bits,
		.standing = rules.standing,
	};

	return lightEnds(tracing.features, ending);
}

bool aimReadsReverse(const StrategyCounts& counts, const Vertex& vertex) {
	return needsReverse(counts) && !vertex.collided && (vertex.sharp || counts.connect > 0);
}

BsdfAnswer offerAnswer(const Tracing& tracing, const Standing& standing, const LightAsked& asked) {
	const bool reverses = aimReadsReverse(standing.counts, standing.path.vertex);
	const bool sameSide = math::dot(standing.path.vertex.bsdf.normal, asked.incoming) >= 0;

	const ViewedAsk ask = {
		.incoming = asked.incoming,
		.reverse  = reverses && sameSide,
	};

	BsdfAnswer answer = answerViewed(tracing.materials, asked.viewed, ask);
	answer.reverse    = reverses && !sameSide ? reverseDensity(tracing, standing.path, asked.incoming) : answer.reverse;
	return answer;
}

Offer offerOf(const Tracing& tracing, const Standing& standing, const LightAsked& asked) {
	const std::uint32_t light = asked.light;
	const Path& path          = standing.path;
	const math::Vector open   = openSide(path);
	if (!aimEnds(tracing, standing.companions.rules, light)) {
		return noOffer();
	}

	const AimQuery query = {
		.light       = light,
		.from        = path.vertex.point,
		.normal      = open,
		.pair        = asked.pair,
		.wavelengths = path.wavelengths,
	};

	const AimAsk ask = {
		.query   = query,
		.numbers = path.numbers,
		.event   = path.depth,
	};

	const LightAim aim  = aimAt(tracing.sampling, tracing.lights, ask);
	const double facing = path.vertex.collided ? 1 : math::dot(open, aim.toward);
	if (!aim.found || facing <= 0 || aim.distance <= 0) {
		return noOffer();
	}

	LightAsked toward = asked;
	toward.incoming   = aim.toward;

	const BsdfAnswer answer = offerAnswer(tracing, standing, toward);

	const AimedLight aimed = {
		.aim    = aim,
		.light  = light,
		.answer = answer,
	};

	const Spectrum4 arrive = aimedArrival(tracing, aimed, standing);

	return Offer {
		.radiance = path.throughput * answer.value * arrive * (facing * aimWeight(tracing, standing, aimed)),
		.shadow   = shadowOf(path, open, aim),
	};
}

Reservoir offered(const Reservoir& reservoir, const Offer& offer) {
	const double weight = laneMean(offer.radiance);
	if (!(weight > 0)) {
		return reservoir;
	}
	const double total = reservoir.total + weight;
	const double keep  = weight / total;
	const bool kept    = reservoir.choice < keep;
	return Reservoir {
		.chosen = kept ? offer : reservoir.chosen,
		.picked = kept ? weight : reservoir.picked,
		.total  = total,
		.choice = kept ? reservoir.choice / keep : (reservoir.choice - keep) / (1 - keep),
	};
}

Line lineOf(const Offer& offer, const Spectrum4& radiance) {
	return Line {
		.shadow   = offer.shadow,
		.radiance = radiance,
	};
}

Colour glowShadedAt(const Tracing& tracing, const Glance& glance) {
	const Shading shading = {
		.scene     = tracing.scene,
		.looks     = tracing.looks,
		.materials = tracing.materials,
	};

	const ShadingQuery query = {
		.hit       = glance.hit,
		.heading   = glance.shadow.direction,
		.medium    = noMaterial,
		.registers = glance.registers,
	};

	const WearQuery wear = {
		.hit       = glance.hit,
		.shaded    = bsdfAt(shading, query),
		.heading   = glance.shadow.direction,
		.shading   = shading,
		.outside   = math::dot(glance.hit.normal, glance.shadow.direction) < 0,
		.registers = glance.registers,
	};

	return wornAt(tracing.features, wear).glow;
}

Spectrum4 glancedGlow(const Tracing& tracing, const Glance& glance) {
	if (glance.hit.kind == HitKind::Missed) {
		return flatSpectrum(0);
	}

	const GlowEnding ending = {
		.bits = glance.rules.bits,
		.leaf = glance.hit.leaf,
	};

	const double counted = glanceCounted(tracing.sampling, glance.shadow.cell, glance.hit.point);
	if (!(counted > 0) || !glowEnds(tracing.features, ending)) {
		return flatSpectrum(0);
	}
	return spectrumOf(glowShadedAt(tracing, glance), glance.path.wavelengths) * counted;
}

void aimedFrom(const Tracing& tracing, const Standing& standing, Lines& lines) {
	const Path& path = standing.path;
	if (!aimable(tracing, path) || isExact(tracing.materials, path.vertex.bsdf.material)) {
		return;
	}

	Reservoir reservoir = {
		.chosen = noOffer(),
		.picked = 0,
		.total  = 0,
		.choice = drawnNumber(tracing, path, Draw::Pick),
	};

	const math::UnitPair pair = drawnPair(tracing, path, Draw::LightU);

	const BsdfView view = {
		.outgoing    = -path.heading,
		.wavelengths = path.wavelengths,
	};

	const ViewedBsdf viewed = viewedBsdf(tracing.materials, path.vertex.bsdf, view);
	std::uint32_t owned     = 0;
	for (std::uint32_t light = 0; light < aimableCount(tracing.sampling, tracing.lights); ++light) {
		const LightAsked asked = {
			.light    = light,
			.pair     = pair,
			.viewed   = viewed,
			.incoming = { 0, 0, 0 },
		};

		const Offer offer = offerOf(tracing, standing, asked);

		const SelectionQuery selection = {
			.light  = light,
			.depth  = path.depth,
			.owned  = owned,
			.lights = tracing.lights,
		};

		const bool own    = selectLight(tracing.sampling, selection) == Selection::Own;
		const bool bright = laneMean(offer.radiance) > 0;
		if (own && bright) {
			addLine(lines, lineOf(offer, offer.radiance));
		}
		owned += own && bright ? 1 : 0;
		reservoir = own ? reservoir : offered(reservoir, offer);
	}
	if (reservoir.picked > 0) {
		addLine(lines, lineOf(reservoir.chosen, reservoir.chosen.radiance * (reservoir.total / reservoir.picked)));
	}
}

bool below(const Vertex& vertex, const Scatter& drawn) {
	if (vertex.collided) {
		return false;
	}
	const double side = math::dot(drawn.incoming, vertex.surface);
	return drawn.through ? side >= 0 : side <= 0;
}

Aimed aimedAlong(const Tracing& tracing, const Path& path, const Scatter& drawn) {
	const bool open = path.vertex.collided || drawn.through == (path.medium != noMaterial);
	return drawn.density > 0 && open && aimable(tracing, path) ? Aimed::Every : Aimed::None;
}

Spectrum4 carriedThroughput(const Path& path, const Scatter& drawn) {
	const Spectrum4 carried = path.throughput * drawn.weight;
	const bool parting      = drawn.through && !path.dispersed && path.vertex.bsdf.sides.parted && bending(path.vertex.bsdf.sides);
	return parting ? collapsedToHero(carried) : carried;
}

BsdfDensities pastDensities(const Tracing& tracing, const Path& path, const Scatter& drawn) {
	const StrategyCounts counts = countsOf(tracing.strategies);
	const bool bare             = drawn.density > 0 && weighedBare(counts, path);
	const bool reverses         = needsReverse(counts) && !path.vertex.collided;
	if (bare && reverses && math::dot(path.vertex.bsdf.normal, drawn.incoming) >= 0) {
		const BsdfPair pair = {
			.outgoing    = -path.heading,
			.incoming    = drawn.incoming,
			.wavelengths = path.wavelengths,
		};

		return densitiesOf(tracing.materials, path.vertex.bsdf, pair);
	}
	return BsdfDensities {
		.density = bare ? bareDensity(tracing, path, drawn.incoming) : drawn.density,
		.reverse = reverses ? reverseDensity(tracing, path, drawn.incoming) : 0,
	};
}

Weighing weighingPast(const Tracing& tracing, const Path& path, const Scatter& drawn) {
	const BsdfDensities densities = pastDensities(tracing, path, drawn);

	const ScatterFactors factors = {
		.forward     = densities.density,
		.reverse     = densities.reverse,
		.cosine      = path.vertex.collided ? 1 : std::abs(math::dot(path.vertex.surface, drawn.incoming)),
		.exact       = !(drawn.density > 0),
		.sharp       = path.vertex.sharp,
		.connectable = drawn.density > 0 && !drawn.through && path.medium == noMaterial,
		.aimed       = aimedAlong(tracing, path, drawn),
	};

	return weighingAfterScatter(path.weighing, factors);
}

std::uint32_t mediumPast(const Path& path, const Scatter& drawn) {
	if (!drawn.through) {
		return path.medium;
	}
	return path.vertex.bsdf.sides.inside ? noMaterial : path.vertex.bsdf.material;
}

RuleState drawnRules(const RuleState& rules) {
	RuleState drawn = rules;
	drawn.drawn     = rules.bits;
	drawn.drawnOn   = rules.standing;
	return drawn;
}

void scatteredFrom(const Tracing& tracing, Walk& walk) {
	Path& path = walk.path;
	walk.phase = WalkPhase::Done;
	if (atDepthCap(standingCounts(path), depthCaps(tracing.limits))) {
		return;
	}

	const math::UnitPair choice = drawnPair(tracing, path, Draw::Lobe);

	const LobeNumbers numbers = {
		.pair = drawnPair(tracing, path, Draw::DirectionU),
		.lobe = choice.u,
		.side = choice.v,
	};

	const DirectionQuery query = {
		.materials = tracing.materials,
		.path      = path,
		.guide     = walk.companions.guide,
		.numbers   = numbers,
	};

	const Scatter drawn = drawDirection(tracing.sampling, query);
	if (!drawn.found || below(path.vertex, drawn)) {
		return;
	}
	const Spectrum4 carried = carriedThroughput(path, drawn);
	const double survival   = std::min(survivalCeiling, laneMaximum(carried));
	const bool roulette     = path.depth >= tracing.limits.minimumDepth;
	if (roulette && !(drawnNumber(tracing, path, Draw::Survival) < survival)) {
		return;
	}
	const bool dispersed       = path.dispersed || (drawn.through && path.vertex.bsdf.sides.parted && bending(path.vertex.bsdf.sides));
	const Weighing weighing    = weighingPast(tracing, path, drawn);
	const std::uint32_t medium = mediumPast(path, drawn);
	walk.ray                   = leavingRay(path.vertex, drawn.incoming, medium);
	walk.phase                 = WalkPhase::Flying;
	path.throughput            = roulette ? carried / survival : carried;
	path.exact                 = path.exact && !(drawn.density > 0);
	path.dispersed             = dispersed;
	path.weighing              = weighing;
	path.medium                = medium;
	path.depth                 = path.depth + 1;
	path.heading               = drawn.incoming;
	path.drawnAt               = path.vertex.point;
	path.legFrom               = path.vertex.point;
	path.crossings             = 0;
	path.sharpness             = drawn.sharpness;
	path.density               = drawn.density > 0 ? drawn.density : 0;
	walk.companions.rules      = drawnRules(walk.companions.rules);
}

Ray lineRay(const Shadow& shadow) {
	return Ray {
		.origin    = shadow.origin,
		.direction = shadow.direction,
		.far       = shadow.reach,
		.inside    = shadow.within != noMaterial,
		.within    = shadow.within,
	};
}

void visited(const Tracing& tracing, const StrategyVisit& visit) {
	for (const StrategyMember member : tracing.strategies.members) {
		strategyStep(member, tracing, visit);
	}
}

const LightSubpath noLeadKept = {
	.items = {},
	.count = 0,
	.lamp  = noLamp,
};

const LightSubpath& keptLead(const Walk& walk) {
	return walk.lead.empty() ? noLeadKept : walk.lead.front().kept;
}

void ledWalk(const Tracing& tracing, Walk& walk) {
	if (!strategyLeads(tracing.strategies)) {
		walk.lead.clear();
		walk.phase = WalkPhase::Flying;
		return;
	}
	walk.lead.assign(1, leadStarted(tracing, walk.path));
	walk.phase = walk.lead.front().phase == LightPhase::Done ? WalkPhase::Flying : WalkPhase::Leading;
}

void walkRestarted(const Tracing& tracing, const PixelSample& sample, Walk& walk) {
	const std::uint32_t pixel = std::uint32_t(sample.y) * std::uint32_t(tracing.lens.width) + std::uint32_t(sample.x);

	guideRestarted(tracing.sampling, pixel, walk.companions.guide);
	walk.companions.rules = freshRules();
	walk.ray              = startedPathIn(tracing, sample, walk.path);
	walk.lines.count      = 0;
	walk.line             = 0;
	if (tracing.recording) {
		walk.first = missedSurface();
	}
	ledWalk(tracing, walk);
}

Walk walkStarted(const Tracing& tracing, const PixelSample& sample) {
	const Onward onward = startedPath(tracing, sample);

	const Companions companions = {
		.guide = startedGuide(tracing.sampling, 0),
		.rules = freshRules(),
	};

	Walk walk = {
		.path       = onward.path,
		.companions = companions,
		.lead       = {},
		.ray        = onward.ray,
		.lines      = noLines(),
		.line       = 0,
		.phase      = WalkPhase::Done,
		.first      = missedSurface(),
	};

	walkRestarted(tracing, sample, walk);
	return walk;
}

RayKind flyingKind(const Walk& walk) {
	if (walk.path.depth == 0) {
		return RayKind::Camera;
	}
	return walk.ray.inside ? RayKind::Inside : RayKind::Scatter;
}

bool linesPassShapes(const Tracing& tracing) {
	return rulesSaid(tracing.features) || tracing.materials.passable;
}

Wanted lineWanted(const Tracing& tracing, const Shadow& shadow) {
	const bool bare = shadow.cell == noCell && !linesPassShapes(tracing);
	const Ray ray   = lineRay(shadow);
	return Wanted {
		.kind = bare ? RayKind::Shadow : (ray.inside ? RayKind::Inside : RayKind::Scatter),
		.ray  = ray,
	};
}

Wanted wantedRay(const Tracing& tracing, const Walk& walk) {
	if (walk.phase == WalkPhase::Leading) {
		return lightRayWanted(tracing, walk.lead.front());
	}
	if (walk.phase == WalkPhase::Lighting) {
		return lineWanted(tracing, walk.lines.items[walk.line].shadow);
	}
	return Wanted {
		.kind = flyingKind(walk),
		.ray  = walk.ray,
	};
}

Spectrum4 glancedLight(const Tracing& tracing, const Walk& walk, const Answer& answer) {
	const Line& line = walk.lines.items[walk.line];

	const Glance glance = {
		.path      = walk.path,
		.rules     = walk.companions.rules,
		.shadow    = line.shadow,
		.hit       = answer.hit,
		.registers = answer.registers,
	};

	return line.radiance * glancedGlow(tracing, glance);
}

Spectrum4 keptOnLine(const Tracing& tracing, const LineHit& met) {
	const Shadow& shadow = met.shadow;

	const MediumLeg leg = {
		.medium      = shadow.medium,
		.origin      = shadow.origin,
		.heading     = shadow.direction,
		.distance    = met.hit.kind == HitKind::Missed ? shadow.reach : met.hit.distance,
		.wavelengths = met.wavelengths,
		.use         = MediumUse::Crossing,
		.number      = 0,
	};

	return keptOnLeg(tracing, leg);
}

Shadow shadowPast(const Shadow& shadow, const Hit& hit, double lift) {
	Shadow passed    = shadow;
	passed.origin    = hit.point + shadow.direction * lift;
	passed.reach     = shadow.reach - (hit.distance + lift);
	passed.crossings = shadow.crossings + 1;
	return passed;
}

Shadow passedShadow(const Tracing& tracing, const Shadow& shadow, const Hit& hit) {
	const bool entering = shadow.within == noMaterial;
	Shadow passed       = shadowPast(shadow, hit, tracing.limits.bias + 2 * hit.loosened);
	passed.within       = entering ? tracing.scene.leaves[hit.leaf].material : noMaterial;
	return passed;
}

LineStep lineCrossing(const Tracing& tracing, const LineHit& met, const Spectrum4& kept) {
	const LineStep blocked = {
		.onward = false,
		.shadow = met.shadow,
		.kept   = kept,
	};

	const Hit& hit = met.hit;
	if (hit.leaf == noLeaf || !crossableBody(tracing, tracing.scene.leaves[hit.leaf].material)) {
		return blocked;
	}

	const Surfacing surfacing = {
		.hit         = hit,
		.heading     = met.shadow.direction,
		.medium      = met.shadow.medium,
		.wavelengths = met.wavelengths,
		.registers   = met.registers,
	};

	const Vertex vertex = surfaceAt(tracing, surfacing);
	if (!crossable(tracing, vertex)) {
		return blocked;
	}
	Shadow crossed = shadowPast(met.shadow, hit, vertex.lift);
	crossed.medium = vertex.bsdf.sides.inside ? noMaterial : vertex.bsdf.material;
	crossed.within = crossed.medium;
	return LineStep {
		.onward = true,
		.shadow = crossed,
		.kept   = kept,
	};
}

LineStep lineStepped(const Tracing& tracing, const LineHit& met) {
	const Spectrum4 kept = keptOnLine(tracing, met);
	if (met.hit.kind == HitKind::Missed || met.shadow.crossings >= mostCrossings) {
		return LineStep {
			.onward = false,
			.shadow = met.shadow,
			.kept   = kept,
		};
	}
	if (passes(tracing.features, met.hit.leaf, met.shadow.passage)) {
		return LineStep {
			.onward = true,
			.shadow = passedShadow(tracing, met.shadow, met.hit),
			.kept   = kept,
		};
	}
	return lineCrossing(tracing, met, kept);
}

void lineAnswered(const Tracing& tracing, Walk& walk, const Answer& answer) {
	Line& line = walk.lines.items[walk.line];

	const LineHit met = {
		.shadow      = line.shadow,
		.hit         = answer.hit,
		.wavelengths = walk.path.wavelengths,
		.registers   = answer.registers,
	};

	const LineStep step = lineStepped(tracing, met);
	line.radiance       = line.radiance * step.kept;
	if (line.shadow.cell != noCell) {
		walk.path.radiance = walk.path.radiance + glancedLight(tracing, walk, answer);
	}
	if (step.onward) {
		line.shadow = step.shadow;
		return;
	}
	if (line.shadow.cell == noCell && answer.hit.kind == HitKind::Missed) {
		walk.path.radiance = walk.path.radiance + line.radiance;
	}
	walk.line += 1;
	if (walk.line < walk.lines.count) {
		return;
	}
	scatteredFrom(tracing, walk);
}

void lighting(const Tracing& tracing, Walk& walk) {
	walk.line = 0;
	if (walk.lines.count == 0) {
		scatteredFrom(tracing, walk);
		return;
	}
	walk.phase = WalkPhase::Lighting;
}

Ray passedRay(const Tracing& tracing, const Path& path, const Hit& hit, bool entering) {
	const double lift = tracing.limits.bias + 2 * hit.loosened;
	return Ray {
		.origin    = hit.point + path.heading * lift,
		.direction = path.heading,
		.far       = infiniteReach,
		.inside    = entering,
		.within    = entering ? tracing.scene.leaves[hit.leaf].material : noMaterial,
	};
}

bool passedHidden(const Tracing& tracing, Walk& walk, const Hit& hit) {
	const bool camera = walk.path.weighing.aimed == Aimed::Camera;
	if (!camera || hit.kind == HitKind::Missed || !passes(tracing.features, hit.leaf, Passage::ToLens)) {
		return false;
	}
	RuleState& rules    = walk.companions.rules;
	const bool entering = (rules.bits & std::uint8_t(RuleBit::Passing)) == 0;
	rules.bits ^= std::uint8_t(RuleBit::Passing);
	walk.ray = passedRay(tracing, walk.path, hit, entering);
	return true;
}

RuleState rulesStepped(const Tracing& tracing, const RuleState& rules, const Vertex& vertex) {
	const RuleStep step = {
		.state = rules,
		.sharp = vertex.sharp,
		.leaf  = vertex.collided ? rules.standing : vertex.leaf,
	};

	return ruledStep(tracing.features, step);
}

bool looksPast(const Tracing& tracing, const Walk& walk, const Hit& hit) {
	const bool camera = walk.path.weighing.aimed == Aimed::Camera;
	const bool looked = (walk.companions.rules.bits & std::uint8_t(RuleBit::Looked)) != 0;
	if (!rulesSaid(tracing.features) || camera || looked || walk.path.medium != noMaterial) {
		return false;
	}
	const bool shadowless = hit.kind != HitKind::Missed && passes(tracing.features, hit.leaf, Passage::ToLight);
	return shadowless || (walk.companions.rules.bits & std::uint8_t(RuleBit::Beyond)) != 0;
}

void glowMetBeyond(const Tracing& tracing, Walk& walk, const Answer& answer) {
	Path& path = walk.path;

	const Surfacing surfacing = {
		.hit         = answer.hit,
		.heading     = path.heading,
		.medium      = noMaterial,
		.wavelengths = path.wavelengths,
		.registers   = answer.registers,
	};

	const GlowSight sight = {
		.glow  = surfaceAt(tracing, surfacing).glow,
		.place = answer.hit.point,
		.leaf  = answer.hit.leaf,
		.rules = walk.companions.rules,
	};

	glowMetAt(tracing, path, sight);
}

void lookedOn(const Tracing& tracing, Walk& walk, const Answer& answer) {
	RuleState& rules    = walk.companions.rules;
	const bool entering = (rules.bits & std::uint8_t(RuleBit::Passing)) == 0;
	if (entering && (rules.bits & std::uint8_t(RuleBit::Beyond)) != 0) {
		glowMetBeyond(tracing, walk, answer);
	}
	rules.bits = std::uint8_t((rules.bits ^ std::uint8_t(RuleBit::Passing)) | std::uint8_t(RuleBit::Beyond));
	walk.ray   = passedRay(tracing, walk.path, answer.hit, entering);
}

Ray lookedFrom(const Path& path) {
	if (path.crossings == 0) {
		return leavingRay(path.vertex, path.heading, noMaterial);
	}
	return Ray {
		.origin    = path.legFrom,
		.direction = path.heading,
		.far       = infiniteReach,
		.inside    = false,
		.within    = noMaterial,
	};
}

void lookedPast(const Tracing& tracing, Walk& walk, const Answer& answer) {
	const Hit& hit = answer.hit;
	if (hit.kind != HitKind::Missed && passes(tracing.features, hit.leaf, Passage::ToLight)) {
		lookedOn(tracing, walk, answer);
		return;
	}
	if (hit.kind != HitKind::Missed) {
		glowMetBeyond(tracing, walk, answer);
	}
	const std::uint8_t flying = std::uint8_t(RuleBit::Beyond) | std::uint8_t(RuleBit::Passing);
	RuleState& rules          = walk.companions.rules;
	rules.beyond              = hit.kind == HitKind::Missed ? infiniteReach : math::length(hit.point - walk.path.legFrom);
	rules.bits                = std::uint8_t((rules.bits & ~flying) | std::uint8_t(RuleBit::Looked));
	walk.ray                  = lookedFrom(walk.path);
}

void crossedOn(const Tracing& tracing, Walk& walk, const Arrival& arrival, const Vertex& vertex) {
	Path& path                 = walk.path;
	const std::uint32_t medium = vertex.bsdf.sides.inside ? noMaterial : vertex.bsdf.material;
	path.throughput            = path.throughput * keptAlong(tracing, path, math::length(arrival.hit.point - path.legFrom));
	walk.ray                   = crossingRay(vertex, path.heading, medium);
	path.medium                = medium;
	path.legFrom               = walk.ray.origin;
	path.crossings             = path.crossings + 1;
	walk.companions.rules      = standingAt(walk.companions.rules, vertex.leaf);
}

float roughnessOf(const Tracing& tracing, std::uint32_t material) {
	return float(std::sqrt(tracing.materials.finishes[material].width));
}

Surface vertexSurface(const Tracing& tracing, const Vertex& vertex, const math::Vector& from) {
	return Surface {
		.distance  = float(math::length(vertex.point - from)),
		.normal    = vertex.bsdf.normal,
		.leaf      = vertex.leaf,
		.material  = vertex.bsdf.material,
		.albedo    = vertex.bsdf.albedo,
		.roughness = roughnessOf(tracing, vertex.bsdf.material),
		.onward    = std::numeric_limits<float>::infinity(),
	};
}

Surface standingSurface(const Tracing& tracing, const Path& path) {
	return vertexSurface(tracing, path.vertex, path.drawnAt);
}

void recordedLeg(Walk& walk, const Hit& hit) {
	if (walk.path.depth != 1) {
		return;
	}
	const bool met    = hit.kind != HitKind::Missed;
	walk.first.onward = met ? float(math::length(hit.point - walk.path.drawnAt)) : std::numeric_limits<float>::infinity();
}

void recordedStand(const Tracing& tracing, Walk& walk) {
	if (walk.path.depth == 0) {
		walk.first = standingSurface(tracing, walk.path);
	}
}

void rayAnswered(const Tracing& tracing, Walk& walk, const Answer& answer) {
	if (passedHidden(tracing, walk, answer.hit)) {
		return;
	}
	if (looksPast(tracing, walk, answer.hit)) {
		lookedPast(tracing, walk, answer);
		return;
	}
	const Stop stop  = stopOf(tracing, walk.path, answer.hit);
	const Hit& hit   = stop.hit;
	walk.lines.count = 0;
	if (tracing.recording) {
		recordedLeg(walk, hit);
	}

	const StrategyVisit arriving = {
		.moment     = StrategyMoment::Arrival,
		.path       = walk.path,
		.companions = walk.companions,
		.lead       = keptLead(walk),
		.hit        = hit,
		.lines      = walk.lines,
	};

	visited(tracing, arriving);
	walk.lines.count = 0;
	if (hit.kind == HitKind::Missed) {
		walk.phase = WalkPhase::Done;
		return;
	}

	const Arrival arrival = {
		.path = walk.path,
		.hit  = hit,
	};

	if (stop.collided) {
		collidedOn(tracing, arrival);
	}
	if (!stop.collided) {
		const Vertex vertex = surfacedAt(tracing, arrival, answer.registers);
		if (walk.path.crossings < mostCrossings && crossable(tracing, vertex)) {
			crossedOn(tracing, walk, arrival, vertex);
			return;
		}
		stoodOn(tracing, arrival, vertex);
	}
	if (tracing.recording) {
		recordedStand(tracing, walk);
	}
	walk.companions.rules = rulesStepped(tracing, walk.companions.rules, walk.path.vertex);

	const ReachQuery reach = {
		.path   = walk.path,
		.lights = tracing.lights,
	};

	guideReached(tracing.sampling, reach, walk.companions.guide);

	const StrategyVisit standing = {
		.moment     = StrategyMoment::Vertex,
		.path       = walk.path,
		.companions = walk.companions,
		.lead       = keptLead(walk),
		.hit        = hit,
		.lines      = walk.lines,
	};

	visited(tracing, standing);
	lighting(tracing, walk);
}

void walkAnswered(const Tracing& tracing, Walk& walk, const Answer& answer) {
	if (walk.phase == WalkPhase::Leading) {
		LightWalk& lead = walk.lead.front();
		lightWalkAnswered(tracing, lead, answer);
		walk.phase = lead.phase == LightPhase::Done ? WalkPhase::Flying : WalkPhase::Leading;
		return;
	}
	if (walk.phase == WalkPhase::Lighting) {
		lineAnswered(tracing, walk, answer);
		return;
	}
	rayAnswered(tracing, walk, answer);
}

void walkedOut(const Tracing& tracing, Walk& walk) {
	std::vector<double> registers;
	while (walk.phase != WalkPhase::Done) {
		const Hit hit = intersect(tracing.geometry, wantedRay(tracing, walk).ray);

		const Answer answer = {
			.hit       = hit,
			.registers = registers,
		};

		walkAnswered(tracing, walk, answer);
	}
}

Traced tracePath(const Tracing& tracing, const PixelSample& sample) {
	Walk walk = walkStarted(tracing, sample);
	walkedOut(tracing, walk);
	return Traced {
		.wavelengths = walk.path.wavelengths,
		.radiance    = walk.path.radiance,
	};
}

Recorded recordedPath(const Tracing& tracing, const PixelSample& sample) {
	Walk walk = walkStarted(tracing, sample);
	walkedOut(tracing, walk);

	const Parts parts = {
		.total = colourOf(walk.path.wavelengths, walk.path.radiance),
	};

	return Recorded {
		.parts   = parts,
		.surface = walk.first,
	};
}

Surface missedSurface() {
	return Surface {
		.distance  = std::numeric_limits<float>::infinity(),
		.normal    = { 0, 0, 0 },
		.leaf      = noLeaf,
		.material  = noMaterial,
		.albedo    = { 0, 0, 0 },
		.roughness = 1,
		.onward    = std::numeric_limits<float>::infinity(),
	};
}

Recorded emptyRecord() {
	const Parts none = {
		.total = { 0, 0, 0 },
	};

	return Recorded {
		.parts   = none,
		.surface = missedSurface(),
	};
}

Surface cameraSurface(const Tracing& tracing, const CameraHit& met) {
	if (met.hit.kind == HitKind::Missed) {
		return missedSurface();
	}

	const Wavelengths wavelengths = spreadWavelengths(0);

	const Surfacing surfacing = {
		.hit         = met.hit,
		.heading     = met.ray.direction,
		.medium      = met.ray.within,
		.wavelengths = wavelengths,
		.registers   = met.registers,
	};

	return vertexSurface(tracing, surfaceAt(tracing, surfacing), met.ray.origin);
}

}
