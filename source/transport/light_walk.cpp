#include "transport/light_walk.h"

#include "math/random.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kodanuki {

constexpr double lightSurvivalCeiling = 0.95;
constexpr std::uint64_t lightPassTag  = 0x6c69676874706173u;
constexpr std::uint64_t leadTag       = 0x6c65616473756270u;

struct Launch {
	std::uint32_t lamp;
	const LampPoint& point;
	const EmissionDraw& drawn;
	double cosine;
};

double lightNumber(const Tracing& tracing, const LightPath& path, const Coordinate& coordinate) {
	const NumberQuery query = {
		.numbers    = path.numbers,
		.coordinate = coordinate,
	};

	return numberAt(tracing.sampling, query);
}

math::UnitPair lightPair(const Tracing& tracing, const LightPath& path, const Coordinate& first) {
	const NumberQuery query = {
		.numbers    = path.numbers,
		.coordinate = first,
	};

	return pairAt(tracing.sampling, query);
}

std::uint32_t pickedLamp(const std::vector<double>& shares, double choice) {
	std::uint32_t picked = noLamp;
	double left          = choice;
	for (std::uint32_t lamp = 0; lamp < shares.size(); ++lamp) {
		if (!(shares[lamp] > 0)) {
			continue;
		}
		picked = lamp;
		if (left < shares[lamp]) {
			return lamp;
		}
		left -= shares[lamp];
	}
	return picked;
}

LightPath unlaunchedPath(const LightStart& start) {
	const LightWeighing weighing = {
		.meet        = 0,
		.aim         = 0,
		.connections = 0,
		.cut         = 0,
		.fresh       = true,
	};

	return LightPath {
		.wavelengths = start.wavelengths,
		.throughput  = flatSpectrum(0),
		.start       = 0,
		.dispersed   = false,
		.vertex      = blankVertex(),
		.heading     = { 0, 0, 1 },
		.drawnAt     = { 0, 0, 0 },
		.legFrom     = { 0, 0, 0 },
		.crossings   = 0,
		.depth       = 0,
		.medium      = noMaterial,
		.lamp        = noLamp,
		.relayed     = false,
		.rules       = freshRules(),
		.weighing    = weighing,
		.numbers     = start.numbers,
	};
}

Ray openRay(const math::Vector& origin, const math::Vector& direction) {
	return Ray {
		.origin    = origin,
		.direction = direction,
		.far       = std::numeric_limits<double>::infinity(),
		.inside    = false,
		.within    = noMaterial,
	};
}

LightWalk idleLightWalk(const LightStart& start) {
	FilmLanding landing;
	landing.count = 0;
	LightSubpath kept;
	kept.count = 0;
	kept.lamp  = noLamp;

	return LightWalk {
		.use     = start.use,
		.path    = unlaunchedPath(start),
		.ray     = openRay({ 0, 0, 0 }, { 0, 0, 1 }),
		.line    = noLensLine(),
		.landing = landing,
		.kept    = kept,
		.phase   = LightPhase::Done,
	};
}

double lampCosine(const LampPoint& point, const math::Vector& direction) {
	const bool round = point.normal.x == 0 && point.normal.y == 0 && point.normal.z == 0;
	return round ? 1 : math::dot(point.normal, direction);
}

void launchedLight(const Tracing& tracing, LightWalk& walk, const Launch& launch) {
	const StrategyCounts counts = countsOf(tracing.strategies);
	const double share          = tracing.strategies.lampShares[launch.lamp];
	const double emission       = share * launch.point.density * launch.drawn.density;
	LightPath& path             = walk.path;

	const LampStart start = {
		.emission = emission,
		.cosine   = launch.cosine,
		.singular = isSingular(tracing.lights, launch.lamp),
	};

	path.throughput = lampEmission(tracing.lights, launch.point, path.wavelengths) * (launch.cosine / emission);
	path.start      = laneMaximum(path.throughput);
	path.heading    = launch.drawn.direction;
	path.drawnAt    = launch.point.point;
	path.legFrom    = launch.point.point;
	path.lamp       = launch.lamp;
	path.weighing   = weighingAtLamp(start, counts);
	walk.kept.lamp  = launch.lamp;
	walk.ray        = openRay(launch.point.point, launch.drawn.direction);
	walk.phase      = path.start > 0 ? LightPhase::Flying : LightPhase::Done;
}

LightWalk lightWalkStarted(const Tracing& tracing, const LightStart& start) {
	LightWalk walk           = idleLightWalk(start);
	const LightPath& path    = walk.path;
	const std::uint32_t lamp = pickedLamp(tracing.strategies.lampShares, lightNumber(tracing, path, coordinateOf(0, Draw::Pick)));
	if (lamp == noLamp) {
		return walk;
	}

	const LampDraw draw = {
		.light = lamp,
		.pair  = lightPair(tracing, path, coordinateOf(0, Draw::LightU)),
	};

	const LampPoint point = pointOnLamp(tracing.lights, draw);

	const EmissionQuery query = {
		.lights = tracing.lights,
		.point  = point,
		.pick   = lightNumber(tracing, path, coordinateOf(0, Draw::MixturePart)),
		.pair   = lightPair(tracing, path, coordinateOf(0, Draw::DirectionU)),
	};

	const EmissionDraw drawn = drawEmission(tracing.sampling, query);
	const double cosine      = lampCosine(point, drawn.direction);
	if (!drawn.found || !(drawn.density > 0) || !(cosine > 0)) {
		return walk;
	}

	const Launch launch = {
		.lamp   = lamp,
		.point  = point,
		.drawn  = drawn,
		.cosine = cosine,
	};

	launchedLight(tracing, walk, launch);
	return walk;
}

LightWalk passWalkStarted(const Tracing& tracing, std::uint32_t index) {
	const PathNumbers numbers = {
		.stream    = math::mixedBits(tracing.seed) ^ math::mixedBits(lightPassTag),
		.index     = index,
		.sequenced = false,
	};

	const NumberQuery hue = {
		.numbers    = numbers,
		.coordinate = coordinateOf(0, Draw::LightHue),
	};

	const LightStart start = {
		.use         = LightUse::Joining,
		.numbers     = numbers,
		.wavelengths = sampledWavelengths(numberAt(tracing.sampling, hue)),
	};

	return lightWalkStarted(tracing, start);
}

LightWalk leadStarted(const Tracing& tracing, const Path& path) {
	const PathNumbers numbers = {
		.stream    = math::mixedBits(path.numbers.stream ^ leadTag),
		.index     = path.numbers.index,
		.sequenced = false,
	};

	const LightStart start = {
		.use         = LightUse::Leading,
		.numbers     = numbers,
		.wavelengths = path.wavelengths,
	};

	return lightWalkStarted(tracing, start);
}

LightWalk idleLead() {
	const PathNumbers numbers = {
		.stream    = 0,
		.index     = 0,
		.sequenced = false,
	};

	const LightStart start = {
		.use         = LightUse::Leading,
		.numbers     = numbers,
		.wavelengths = sampledWavelengths(0.5),
	};

	return idleLightWalk(start);
}

Wanted lightRayWanted(const Tracing& tracing, const LightWalk& walk) {
	if (walk.phase == LightPhase::Joining) {
		return lineWanted(tracing, walk.line.shadow);
	}
	return Wanted {
		.kind = walk.ray.inside ? RayKind::Inside : RayKind::Scatter,
		.ray  = walk.ray,
	};
}

double shadingShift(const Vertex& vertex, const math::Vector& from, const math::Vector& going) {
	const math::Vector& surface = vertex.surface;
	const math::Vector& normal  = vertex.bsdf.normal;
	if (surface.x == normal.x && surface.y == normal.y && surface.z == normal.z) {
		return 1;
	}
	const double under = std::abs(math::dot(from, surface)) * std::abs(math::dot(going, normal));
	if (!(under > 0)) {
		return 0;
	}
	return std::abs(math::dot(from, normal)) * std::abs(math::dot(going, surface)) / under;
}

Spectrum4 lightCarried(const LightPath& path, const Scatter& drawn) {
	const Spectrum4 carried = path.throughput * drawn.weight * shadingShift(path.vertex, -path.heading, drawn.incoming);
	const bool parting      = drawn.through && !path.dispersed && path.vertex.bsdf.sides.parted && bending(path.vertex.bsdf.sides);
	return parting ? collapsedToHero(carried) : carried;
}

LightWeighing lightWeighingPast(const Tracing& tracing, const LightPath& path, const Scatter& drawn) {
	const TurnBack turn = {
		.bsdf        = path.vertex.bsdf,
		.heading     = path.heading,
		.drawn       = drawn.incoming,
		.wavelengths = path.wavelengths,
	};

	const ScatterFactors factors = {
		.forward     = drawn.density,
		.reverse     = densityBack(tracing.materials, turn),
		.cosine      = std::abs(math::dot(path.vertex.surface, drawn.incoming)),
		.exact       = !(drawn.density > 0),
		.sharp       = path.vertex.sharp,
		.connectable = drawn.density > 0 && !drawn.through && path.medium == noMaterial,
		.aimed       = Aimed::None,
	};

	return lightWeighingAfterScatter(path.weighing, factors);
}

std::uint32_t lightMediumPast(const LightPath& path, const Scatter& drawn) {
	if (!drawn.through) {
		return path.medium;
	}
	return path.vertex.bsdf.sides.inside ? noMaterial : path.vertex.bsdf.material;
}

Scatter lightDrawn(const Tracing& tracing, const LightPath& path) {
	const int event = path.depth + 1;

	const math::UnitPair choice = lightPair(tracing, path, coordinateOf(event, Draw::Lobe));

	const LobeNumbers numbers = {
		.pair = lightPair(tracing, path, coordinateOf(event, Draw::DirectionU)),
		.lobe = choice.u,
		.side = choice.v,
	};

	const BsdfDraw draw = {
		.outgoing    = -path.heading,
		.travel      = Travel::FromLight,
		.numbers     = numbers,
		.wavelengths = path.wavelengths,
	};

	return sampleBsdf(tracing.materials, path.vertex.bsdf, draw);
}

void lightScattered(const Tracing& tracing, LightWalk& walk) {
	LightPath& path = walk.path;
	walk.phase      = LightPhase::Done;
	if (path.depth + 1 >= tracing.limits.maximumDepth) {
		return;
	}
	const Scatter drawn = lightDrawn(tracing, path);
	if (!drawn.found || below(path.vertex, drawn)) {
		return;
	}
	const Spectrum4 carried = lightCarried(path, drawn);
	const double survival   = std::min(lightSurvivalCeiling, laneMaximum(carried) / path.start);
	const bool roulette     = path.depth >= tracing.limits.minimumDepth;
	if (roulette && !(lightNumber(tracing, path, coordinateOf(path.depth + 1, Draw::Survival)) < survival)) {
		return;
	}
	const bool dispersed       = path.dispersed || (drawn.through && path.vertex.bsdf.sides.parted && bending(path.vertex.bsdf.sides));
	const LightWeighing past   = lightWeighingPast(tracing, path, drawn);
	const std::uint32_t medium = lightMediumPast(path, drawn);
	walk.ray                   = leavingRay(path.vertex, drawn.incoming, medium);
	walk.phase                 = LightPhase::Flying;
	path.throughput            = roulette ? carried / survival : carried;
	path.dispersed             = dispersed;
	path.weighing              = past;
	path.medium                = medium;
	path.depth                 = path.depth + 1;
	path.heading               = drawn.incoming;
	path.drawnAt               = path.vertex.point;
	path.legFrom               = path.vertex.point;
	path.crossings             = 0;
}

double firstAim(const Tracing& tracing, const LightPath& path, double distance) {
	if (path.medium != noMaterial || isExact(tracing.materials, path.vertex.bsdf.material)) {
		return 0;
	}
	if (isSingular(tracing.lights, path.lamp)) {
		return distance * distance;
	}

	const AimDensityQuery query = {
		.light    = path.lamp,
		.from     = path.vertex.point,
		.toward   = -path.heading,
		.distance = distance,
	};

	return aimDensityAt(tracing.sampling, tracing.lights, query);
}

bool connectableSurface(const Tracing& tracing, const LightPath& path) {
	return !isExact(tracing.materials, path.vertex.bsdf.material) && path.medium == noMaterial;
}

bool joinsHere(const LightWalk& walk) {
	return walk.use == LightUse::Joining && walk.path.depth >= 1 && !walk.path.vertex.sharp && walk.path.medium == noMaterial;
}

LensLine lineToLens(const Tracing& tracing, const LightStanding& standing) {
	for (const StrategyMember member : tracing.strategies.members) {
		const LensLine line = lensLineFrom(member, tracing, standing);
		if (line.found) {
			return line;
		}
	}
	return noLensLine();
}

RuleState lightRulesStepped(const Tracing& tracing, const LightPath& path) {
	const RuleStep step = {
		.state = path.rules,
		.sharp = path.vertex.sharp,
		.leaf  = path.vertex.leaf,
	};

	return ruledLightStep(tracing.features, step);
}

bool passedFirst(const Tracing& tracing, LightWalk& walk, const Hit& hit) {
	LightPath& path  = walk.path;
	const bool first = path.depth == 0 && path.medium == noMaterial;
	if (!first || !passes(tracing.features, hit.leaf, Passage::ToLight)) {
		return false;
	}
	const bool entering = (path.rules.bits & std::uint8_t(RuleBit::Passing)) == 0;
	const double lift   = tracing.limits.bias + 2 * hit.loosened;
	path.rules.bits ^= std::uint8_t(RuleBit::Passing);
	walk.ray = Ray {
		.origin    = hit.point + path.heading * lift,
		.direction = path.heading,
		.far       = std::numeric_limits<double>::infinity(),
		.inside    = entering,
		.within    = entering ? tracing.scene.leaves[hit.leaf].material : noMaterial,
	};

	walk.phase = LightPhase::Flying;
	return true;
}

Spectrum4 keptToHit(const Tracing& tracing, const LightPath& path, const Hit& hit) {
	const MediumLeg leg = {
		.medium      = path.medium,
		.origin      = path.legFrom,
		.heading     = path.heading,
		.distance    = math::length(hit.point - path.legFrom),
		.wavelengths = path.wavelengths,
		.use         = MediumUse::Crossing,
		.number      = 0,
	};

	return keptOnLeg(tracing, leg);
}

Vertex lightSurfaced(const Tracing& tracing, const LightPath& path, const Answer& answer) {
	const Surfacing surfacing = {
		.hit         = answer.hit,
		.heading     = path.heading,
		.medium      = path.medium,
		.wavelengths = path.wavelengths,
		.registers   = answer.registers,
	};

	return surfaceAt(tracing, surfacing);
}

void lightCrossed(LightWalk& walk, const Spectrum4& kept, const Vertex& vertex) {
	LightPath& path            = walk.path;
	const std::uint32_t medium = vertex.bsdf.sides.inside ? noMaterial : vertex.bsdf.material;
	walk.ray                   = Ray {
		.origin    = vertex.point + path.heading * vertex.lift,
		.direction = path.heading,
		.far       = std::numeric_limits<double>::infinity(),
		.inside    = medium != noMaterial,
		.within    = medium,
	};

	walk.phase      = LightPhase::Flying;
	path.throughput = path.throughput * kept;
	path.medium     = medium;
	path.legFrom    = walk.ray.origin;
	path.crossings  = path.crossings + 1;
}

void stoodLightOn(const Tracing& tracing, LightWalk& walk, const Answer& answer, const Vertex& vertex) {
	LightPath& path       = walk.path;
	const double distance = math::length(answer.hit.point - path.drawnAt);
	path.throughput       = path.throughput * keptToHit(tracing, path, answer.hit);
	path.vertex           = vertex;
	path.relayed          = path.depth == 0 ? path.vertex.sharp : path.relayed;
	path.rules            = lightRulesStepped(tracing, path);

	const LightHitFactors factors = {
		.cosine      = std::abs(math::dot(answer.hit.normal, path.heading)),
		.distance    = distance,
		.aim         = path.depth == 0 ? firstAim(tracing, path, distance) : 0,
		.connectable = connectableSurface(tracing, path),
	};

	path.weighing = lightWeighingAtHit(path.weighing, factors);
}

void keptSurface(LightWalk& walk, bool connectable) {
	LightSubpath& kept    = walk.kept;
	const LightPath& path = walk.path;
	if (walk.use != LightUse::Leading || kept.count >= mostLightVertices) {
		return;
	}
	kept.items[kept.count] = LightVertex {
		.vertex      = path.vertex,
		.arrival     = -path.heading,
		.throughput  = path.throughput,
		.weighing    = path.weighing,
		.depth       = path.depth,
		.dispersed   = path.dispersed,
		.connectable = connectable,
		.relayed     = path.relayed,
		.rules       = path.rules,
	};

	kept.count += 1;
}

void lightRayAnswered(const Tracing& tracing, LightWalk& walk, const Answer& answer) {
	walk.phase = LightPhase::Done;
	if (answer.hit.kind == HitKind::Missed || passedFirst(tracing, walk, answer.hit)) {
		return;
	}
	const Vertex vertex = lightSurfaced(tracing, walk.path, answer);
	if (walk.path.crossings < mostCrossings && crossable(tracing, vertex)) {
		lightCrossed(walk, keptToHit(tracing, walk.path, answer.hit), vertex);
		return;
	}
	stoodLightOn(tracing, walk, answer, vertex);
	const bool first = walk.path.depth == 0;
	if (walk.use == LightUse::Joining && first && !walk.path.vertex.sharp) {
		return;
	}
	keptSurface(walk, connectableSurface(tracing, walk.path));

	const LightStanding standing = {
		.path      = walk.path,
		.registers = answer.registers,
	};

	walk.line = joinsHere(walk) ? lineToLens(tracing, standing) : noLensLine();
	if (walk.line.found) {
		walk.phase = LightPhase::Joining;
		return;
	}
	lightScattered(tracing, walk);
}

void landed(FilmLanding& landing, const Splat& splat) {
	landing.splats[landing.count] = splat;
	landing.count += 1;
}

void lensLineAnswered(const Tracing& tracing, LightWalk& walk, const Answer& answer) {
	const LineHit met = {
		.shadow      = walk.line.shadow,
		.hit         = answer.hit,
		.wavelengths = walk.path.wavelengths,
		.registers   = answer.registers,
	};

	const LineStep step = lineStepped(tracing, met);
	walk.line.radiance  = walk.line.radiance * step.kept;
	if (step.onward) {
		walk.line.shadow = step.shadow;
		return;
	}
	if (answer.hit.kind == HitKind::Missed) {
		const Splat splat = {
			.pixel  = walk.line.pixel,
			.colour = colourOf(walk.path.wavelengths, walk.line.radiance),
		};

		landed(walk.landing, splat);
	}
	lightScattered(tracing, walk);
}

void lightWalkAnswered(const Tracing& tracing, LightWalk& walk, const Answer& answer) {
	if (walk.phase == LightPhase::Joining) {
		lensLineAnswered(tracing, walk, answer);
		return;
	}
	lightRayAnswered(tracing, walk, answer);
}

}
