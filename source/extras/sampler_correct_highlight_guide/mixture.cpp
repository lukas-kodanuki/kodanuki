#include "extras/sampler_correct_highlight_guide/mixture.h"

namespace kodanuki {

GuidePlace guidePlaceOf(const Path& path) {
	return GuidePlace {
		.material = path.vertex.bsdf.material,
		.inside   = path.medium != noMaterial,
		.point    = path.vertex.point,
	};
}

math::Vector mixtureNormal(const Path& path) {
	return math::facingAgainst(path.vertex.bsdf.normal, path.heading);
}

Scatter ownDirection(const DirectionQuery& query) {
	const BsdfDraw draw = {
		.outgoing    = -query.path.heading,
		.travel      = Travel::FromEye,
		.numbers     = query.numbers,
		.wavelengths = query.path.wavelengths,
	};

	return sampleBsdf(query.materials, query.path.vertex.bsdf, draw);
}

double ownDensity(const Materials& materials, const Path& path, const math::Vector& incoming) {
	const BsdfPair pair = {
		.outgoing    = -path.heading,
		.incoming    = incoming,
		.wavelengths = path.wavelengths,
	};

	return bsdfDensity(materials, path.vertex.bsdf, pair);
}

double lumpsDensity(const SeenLumps& lumps, const Path& path, const math::Vector& incoming) {
	const LumpPair pair = {
		.normal   = mixtureNormal(path),
		.outgoing = -path.heading,
		.incoming = incoming,
	};

	return lumpDensity(lumps, pair);
}

Scatter undrawn() {
	return Scatter {
		.found     = false,
		.incoming  = { 0, 0, 1 },
		.weight    = flatSpectrum(0),
		.density   = 0,
		.sharpness = 1,
		.through   = false,
	};
}

Scatter mixedOwnDirection(const DirectionQuery& query, const SeenLumps& lumps) {
	const double share = lumps.guide.share;
	Scatter drawn      = ownDirection(query);
	if (!drawn.found) {
		return drawn;
	}
	if (!(drawn.density > 0)) {
		drawn.weight = drawn.weight * (1 / (1 - share));
		return drawn;
	}
	const double all = share * lumpsDensity(lumps, query.path, drawn.incoming) + (1 - share) * drawn.density;
	drawn.weight     = drawn.weight * (drawn.density / all);
	drawn.density    = all;
	return drawn;
}

Scatter lumpsDirection(const DirectionQuery& query, const SeenLumps& lumps, double pick) {
	const Path& path          = query.path;
	const math::Vector normal = mixtureNormal(path);

	const LumpQuery asked = {
		.normal   = normal,
		.outgoing = -path.heading,
		.pick     = pick,
		.pair     = query.numbers.pair,
	};

	const LumpDraw drawn = lumpDirection(lumps, asked);
	if (!drawn.found) {
		return undrawn();
	}
	const double share = lumps.guide.share;
	const double all   = share * lumpsDensity(lumps, path, drawn.incoming) + (1 - share) * ownDensity(query.materials, path, drawn.incoming);
	if (!(all > 0)) {
		return undrawn();
	}

	const BsdfPair pair = {
		.outgoing    = -path.heading,
		.incoming    = drawn.incoming,
		.wavelengths = path.wavelengths,
	};

	const Spectrum4 value = bsdfValue(query.materials, path.vertex.bsdf, pair);
	return Scatter {
		.found     = true,
		.incoming  = drawn.incoming,
		.weight    = value * (math::dot(normal, drawn.incoming) / all),
		.density   = all,
		.sharpness = lumps.guide.sharpness,
		.through   = false,
	};
}

Scatter mixtureDirection(const DirectionQuery& query, const MixtureNumbers& numbers) {
	const SeenLumps& lumps = query.guide.steering;
	if (!lumps.guide.live) {
		return ownDirection(query);
	}
	return numbers.part < lumps.guide.share ? lumpsDirection(query, lumps, numbers.lump) : mixedOwnDirection(query, lumps);
}

double mixtureDensity(const DirectionDensityQuery& query) {
	const SeenLumps& lumps = query.guide.steering;
	if (!lumps.guide.live) {
		return query.own;
	}
	return lumps.guide.share * lumpsDensity(lumps, query.path, query.incoming) + (1 - lumps.guide.share) * query.own;
}

void steerFirst(const HighlightGuides& guides, const Path& path, GuideState& state) {
	steerByPixel(guides, guidePlaceOf(path), state);
}

void steerDeep(const DeepGuide& deep, const Path& path, GuideState& state) {
	steerByDeep(deep, guidePlaceOf(path), state);
}

}
