#include "transport/sampler.h"

#include "extras/sampler_correct_each_lamp_first/each_lamp_first.h"
#include "extras/sampler_correct_highlight_guide/mixture.h"
#include "extras/sampler_correct_sobol/sobol.h"
#include "math/random.h"

#include <algorithm>

namespace kodanuki {

std::uint64_t placeOf(const NumberQuery& query) {
	return sharedDraw(query.coordinate) ? query.numbers.group : query.numbers.index;
}

double numberAt(const Sampling& sampling, const NumberQuery& query) {
	const math::Key key = {
		.stream     = query.numbers.stream,
		.index      = placeOf(query),
		.coordinate = dimensionOf(query.coordinate),
	};

	const bool sequenced = query.numbers.sequenced && key.coordinate < sobolDimensions;
	switch (sampling.sequence) {
		case SamplerMember::Independent: return math::hashedNumber(key);
		case SamplerMember::Sobol: return sequenced ? sobolNumber(key) : math::hashedNumber(key);
		default: break;
	}

	return math::hashedNumber(key);
}

NumberQuery nextDraw(const NumberQuery& first) {
	return NumberQuery {
		.numbers    = first.numbers,
		.coordinate = coordinateOf(first.coordinate.event, Draw(int(first.coordinate.draw) + 1)),
	};
}

bool pairedInSequence(const Sampling& sampling, const NumberQuery& first) {
	const std::uint64_t dimension = dimensionOf(first.coordinate);
	const bool even               = dimension % 2 == 0 && dimension + 1 < sobolDimensions;
	return sampling.sequence == SamplerMember::Sobol && first.numbers.sequenced && even && dimensionOf(nextDraw(first).coordinate) == dimension + 1;
}

math::UnitPair pairAt(const Sampling& sampling, const NumberQuery& first) {
	if (!pairedInSequence(sampling, first)) {
		return { numberAt(sampling, first), numberAt(sampling, nextDraw(first)) };
	}

	const math::Key key = {
		.stream     = first.numbers.stream,
		.index      = placeOf(first),
		.coordinate = dimensionOf(first.coordinate),
	};

	return sobolPair(key);
}

Scatter materialDirection(const DirectionQuery& query) {
	const BsdfDraw draw = {
		.outgoing    = -query.path.heading,
		.travel      = Travel::FromEye,
		.numbers     = query.numbers,
		.wavelengths = query.path.wavelengths,
	};

	return sampleBsdf(query.materials, query.path.vertex.bsdf, draw);
}

double pathNumber(const Sampling& sampling, const Path& path, Draw draw) {
	const NumberQuery query = {
		.numbers    = path.numbers,
		.coordinate = coordinateOf(path.depth, draw),
	};

	return numberAt(sampling, query);
}

Scatter guidedDirection(const Sampling& sampling, const DirectionQuery& query) {
	if (!query.guide.steering.guide.live) {
		return materialDirection(query);
	}

	const MixtureNumbers numbers = {
		.part = pathNumber(sampling, query.path, Draw::MixturePart),
		.lump = pathNumber(sampling, query.path, Draw::MixtureLump),
	};

	return mixtureDirection(query, numbers);
}

Scatter drawDirection(const Sampling& sampling, const DirectionQuery& query) {
	switch (sampling.direction) {
		case SamplerMember::Material: return materialDirection(query);
		case SamplerMember::HighlightGuide: return guidedDirection(sampling, query);
		default: break;
	}

	return materialDirection(query);
}

double materialDensity(const DirectionDensityQuery& query) {
	return query.own;
}

double directionDensity(const Sampling& sampling, const DirectionDensityQuery& query) {
	switch (sampling.direction) {
		case SamplerMember::Material: return materialDensity(query);
		case SamplerMember::HighlightGuide: return mixtureDensity(query);
		default: break;
	}

	return materialDensity(query);
}

GuideState startedGuide(const Sampling& sampling, std::uint32_t pixel) {
	const math::Vector origin = { 0, 0, 0 };

	return GuideState {
		.pixel    = sampling.direction == SamplerMember::HighlightGuide ? pixel : unguided,
		.steering = seenLumps(noGuide(), origin),
		.unpacked = unguided,
	};
}

bool deepened(const Sampling& sampling, const Path& path) {
	const bool guided = sampling.direction == SamplerMember::HighlightGuide && sampling.guides.tunings.deep;
	const bool open   = path.medium == noMaterial;
	return guided && open && path.depth >= 1 && !sampling.guides.survey.filed.empty() && path.density >= sampling.guides.tunings.deepLeastDensity;
}

DeepGuide reachedDeepGuide(const Sampling& sampling, const ReachQuery& query) {
	const Path& path = query.path;

	const DeepGuides deep = {
		.guides = sampling.guides,
		.lights = query.lights,
	};

	const DeepPlace place = {
		.material = path.vertex.bsdf.material,
		.point    = path.vertex.point,
		.normal   = math::facingAgainst(path.vertex.bsdf.normal, path.heading),
	};

	return deepGuide(deep, place);
}

void guideReached(const Sampling& sampling, const ReachQuery& query, GuideState& guide) {
	if (query.path.depth == 0) {
		steerFirst(sampling.guides, query.path, guide);
		return;
	}
	if (!deepened(sampling, query.path)) {
		unsteered(guide);
		return;
	}
	steerDeep(reachedDeepGuide(sampling, query), query.path, guide);
}

void guideRestarted(const Sampling& sampling, std::uint32_t pixel, GuideState& guide) {
	guide.pixel = sampling.direction == SamplerMember::HighlightGuide ? pixel : unguided;
	unsteered(guide);
}

Selection selectLight(const Sampling& sampling, const SelectionQuery& query) {
	switch (sampling.selection) {
		case SamplerMember::ByContribution: return Selection::Shared;
		case SamplerMember::EachLampFirst: return eachLampFirst(query);
		default: break;
	}

	return Selection::Shared;
}

bool roundEmitter(const LampPoint& point) {
	return point.normal.x == 0 && point.normal.y == 0 && point.normal.z == 0;
}

EmissionDraw cosineEmission(const EmissionQuery& query) {
	const bool round             = roundEmitter(query.point);
	const math::Vector direction = round ? math::uniformSphere(query.pair) : math::cosineHemisphere(math::basisAround(query.point.normal), query.pair);
	const double density         = round ? 1 / (4 * math::pi) : math::cosineDensity(query.point.normal, direction);
	return EmissionDraw {
		.found     = density > 0,
		.direction = direction,
		.density   = density,
	};
}

double cosineEmissionDensity(const EmissionDensityQuery& query) {
	return roundEmitter(query.point) ? 1 / (4 * math::pi) : math::cosineDensity(query.point.normal, query.direction);
}

double mirrorEmissionDensity(const Sampling& sampling, const EmissionDensityQuery& query) {
	const MirrorAsk ask = {
		.point     = query.point,
		.direction = query.direction,
	};

	return mirrorDensity(sampling.mirrors, ask);
}

double emissionDensity(const Sampling& sampling, const EmissionDensityQuery& query) {
	double density = 0;
	for (const EmissionShare& share : sampling.emission) {
		switch (share.member) {
			case SamplerMember::Cosine: density += share.share * cosineEmissionDensity(query); break;
			case SamplerMember::TowardMirrors: density += share.share * mirrorEmissionDensity(sampling, query); break;
			default: break;
		}
	}
	return density;
}

struct EmissionPart {
	SamplerMember member;
	double pick;
};

EmissionPart emissionPart(const Sampling& sampling, double pick) {
	double before = 0;
	for (const EmissionShare& share : sampling.emission) {
		const bool last = &share == &sampling.emission.back();
		if (pick < before + share.share || last) {
			return EmissionPart {
				.member = share.member,
				.pick   = std::clamp((pick - before) / share.share, 0.0, 1.0),
			};
		}
		before += share.share;
	}
	return EmissionPart {
		.member = SamplerMember::Cosine,
		.pick   = pick,
	};
}

EmissionDraw mirrorEmission(const Sampling& sampling, const EmissionQuery& query, double pick) {
	const MirrorDraw draw = {
		.point = query.point,
		.pick  = pick,
		.pair  = query.pair,
	};

	const MirrorDirection drawn = mirrorDirection(sampling.mirrors, draw);
	return EmissionDraw {
		.found     = drawn.found,
		.direction = drawn.direction,
		.density   = 0,
	};
}

EmissionDraw partDrawn(const Sampling& sampling, const EmissionQuery& query) {
	const EmissionPart part = emissionPart(sampling, query.pick);
	switch (part.member) {
		case SamplerMember::Cosine: return cosineEmission(query);
		case SamplerMember::TowardMirrors: return mirrorEmission(sampling, query, part.pick);
		default: break;
	}

	return cosineEmission(query);
}

EmissionDraw drawEmission(const Sampling& sampling, const EmissionQuery& query) {
	const EmissionDraw drawn = partDrawn(sampling, query);

	const EmissionDensityQuery asked = {
		.lights    = query.lights,
		.point     = query.point,
		.direction = drawn.direction,
	};

	const double density = drawn.found ? emissionDensity(sampling, asked) : 0;
	return EmissionDraw {
		.found     = density > 0,
		.direction = drawn.direction,
		.density   = density,
	};
}

bool aimOn(const Sampling& sampling, SamplerMember member) {
	return std::find(sampling.aims.begin(), sampling.aims.end(), member) != sampling.aims.end();
}

bool skyCharted(const Sampling& sampling) {
	return aimOn(sampling, SamplerMember::SkyMap) && sampling.sky.charted;
}

bool glowsAimed(const Sampling& sampling) {
	return aimOn(sampling, SamplerMember::GlowingSurfaces) && !sampling.glows.places.empty();
}

std::uint32_t aimableCount(const Sampling& sampling, const Lights& lights) {
	return lightCount(lights) + (glowsAimed(sampling) ? 1 : 0);
}

double askedNumber(const Sampling& sampling, const AimAsk& ask, Draw draw) {
	const NumberQuery query = {
		.numbers    = ask.numbers,
		.coordinate = coordinateOf(ask.event, draw),
	};

	return numberAt(sampling, query);
}

LightAim skyAim(const Sampling& sampling, const Lights& lights, const AimAsk& ask) {
	const SkyDraw draw = {
		.sky         = lights.sky,
		.part        = askedNumber(sampling, ask, Draw::AimPart),
		.pair        = ask.query.pair,
		.jitter      = { askedNumber(sampling, ask, Draw::AimJitterU), askedNumber(sampling, ask, Draw::AimJitterV) },
		.wavelengths = ask.query.wavelengths,
	};

	return chartAim(sampling.sky, draw);
}

LightAim glowsAim(const Sampling& sampling, const AimAsk& ask) {
	const GlowDraw draw = {
		.from = ask.query.from,
		.part = askedNumber(sampling, ask, Draw::AimPart),
		.pair = ask.query.pair,
	};

	return glowAim(sampling.glows, draw);
}

LightAim aimAt(const Sampling& sampling, const Lights& lights, const AimAsk& ask) {
	const std::uint32_t light = ask.query.light;
	if (light == skyLight(lights) && skyCharted(sampling)) {
		return skyAim(sampling, lights, ask);
	}
	if (light == lightCount(lights) && glowsAimed(sampling)) {
		return glowsAim(sampling, ask);
	}
	return aimLight(lights, ask.query);
}

double aimDensityAt(const Sampling& sampling, const Lights& lights, const AimDensityQuery& query) {
	if (query.light == skyLight(lights)) {
		return skyCharted(sampling) ? chartDensity(sampling.sky, query.toward) : 0;
	}
	return aimDensity(lights, query);
}

double glowAimDensity(const Sampling& sampling, const GlowSeen& seen) {
	return glowsAimed(sampling) ? glowDensity(sampling.glows, seen) : 0;
}

double glanceCounted(const Sampling& sampling, std::uint32_t cell, const math::Vector& place) {
	return glowCounted(sampling.glows, cell, place);
}

Sampling coreSampling() {
	const EmissionShare cosine = {
		.member = SamplerMember::Cosine,
		.share  = 1,
	};

	return Sampling {
		.sequence  = SamplerMember::Independent,
		.direction = SamplerMember::Material,
		.selection = SamplerMember::ByContribution,
		.emission  = { cosine },
		.aims      = { SamplerMember::Lamps },
		.guides    = noHighlightGuides(),
		.sky       = unchartedSky(),
		.glows     = noGlowCells(),
		.mirrors   = noMirrorBalls(),
	};
}

Sampling defaultSampling() {
	Sampling sampling  = coreSampling();
	sampling.sequence  = SamplerMember::Sobol;
	sampling.direction = SamplerMember::HighlightGuide;
	sampling.selection = SamplerMember::EachLampFirst;
	sampling.aims      = { SamplerMember::Lamps, SamplerMember::SkyMap, SamplerMember::GlowingSurfaces };

	const EmissionShare mirrors = {
		.member = SamplerMember::TowardMirrors,
		.share  = 1,
	};

	sampling.emission = { mirrors };
	return sampling;
}

}
