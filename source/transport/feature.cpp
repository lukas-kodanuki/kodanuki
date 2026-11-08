#include "transport/feature.h"

#include "light/light.h"
#include "spectral/colour.h"

#include <algorithm>
#include <cmath>

namespace kodanuki {

constexpr int colourDraws     = 16;
constexpr int ambientTurns    = 8;
constexpr double ambientTilt  = 0.2;
constexpr double ambientShare = 0.5;

Spectrum4 absorbedAlong(const Features& features, const MediumQuery& query) {
	const AbsorbedStretch stretch = {
		.material    = query.material,
		.distance    = query.distance,
		.wavelengths = query.wavelengths,
	};

	return keptThrough(features.absorption, stretch);
}

Spectrum4 veiledSky(const Features& features, const SkyQuery& query, const Spectrum4& behind) {
	const Veil veil = veiled(features.clouds, query.direction, query.exact);
	return behind * veil.through + spectrumOf(veil.shown, query.wavelengths);
}

Spectrum4 skyAt(const Features& features, const SkyQuery& query) {
	Spectrum4 radiance = query.radiance;
	for (const FeatureMember member : features.members) {
		switch (member) {
			case FeatureMember::Absorption: break;
			case FeatureMember::Moon: break;
			case FeatureMember::Atmosphere: radiance = airSeen(features.atmosphere, query.direction, query.wavelengths); break;
			case FeatureMember::PaintedSky: radiance = paintedSeen(features.layers, query.direction, query.wavelengths); break;
			case FeatureMember::SkyGlow: radiance = radiance + glowSeen(features.layers, query.direction, query.wavelengths); break;
			case FeatureMember::Stars: radiance = radiance + starsSeen(features.layers, query.direction, query.wavelengths); break;
			case FeatureMember::Clouds: radiance = veiledSky(features, query, radiance); break;
			case FeatureMember::Dispersion:
			case FeatureMember::ThinFilm:
			case FeatureMember::Scattering:
			case FeatureMember::Coats:
			case FeatureMember::PathRules: break;
		}
	}
	return radiance;
}

double thinnedBy(const Cloudscape& clouds, const LampQuery& query) {
	const bool thinned = query.light < clouds.thinned.size() && clouds.thinned[query.light];
	return thinned ? overcast(clouds, query.point, query.toward) : 1.0;
}

Spectrum4 lampAt(const Features& features, const LampQuery& query) {
	Spectrum4 radiance = query.radiance;
	for (const FeatureMember member : features.members) {
		switch (member) {
			case FeatureMember::Absorption:
			case FeatureMember::SkyGlow:
			case FeatureMember::Stars: break;
			case FeatureMember::Atmosphere: radiance = radiance * sunPassed(features.atmosphere, query.light, query.wavelengths); break;
			case FeatureMember::PaintedSky: radiance = radiance * settingTint(features.layers, query.light, query.wavelengths); break;
			case FeatureMember::Moon: radiance = radiance * moonLit(features.layers, query.light, query.toward); break;
			case FeatureMember::Clouds: radiance = radiance * thinnedBy(features.clouds, query); break;
			case FeatureMember::Dispersion:
			case FeatureMember::ThinFilm:
			case FeatureMember::Scattering:
			case FeatureMember::Coats:
			case FeatureMember::PathRules: break;
		}
	}
	return radiance;
}

bool skyMember(FeatureMember member) {
	switch (member) {
		case FeatureMember::Atmosphere:
		case FeatureMember::PaintedSky:
		case FeatureMember::SkyGlow:
		case FeatureMember::Stars:
		case FeatureMember::Clouds: return true;
		case FeatureMember::Absorption:
		case FeatureMember::Moon:
		case FeatureMember::Dispersion:
		case FeatureMember::ThinFilm:
		case FeatureMember::Scattering:
		case FeatureMember::Coats:
		case FeatureMember::PathRules: return false;
	}

	return false;
}

bool skyFeatured(const Features& features) {
	return std::any_of(features.members.begin(), features.members.end(), skyMember);
}

Colour skyColourAt(const Features& features, const GradientSky& sky, const math::Vector& direction) {
	if (!skyFeatured(features)) {
		return gradientColour(sky, direction);
	}
	Colour gathered = { 0, 0, 0 };
	for (int draw = 0; draw < colourDraws; ++draw) {
		const Wavelengths wavelengths = spreadWavelengths((double(draw) + 0.5) / colourDraws);

		const SkyQuery query = {
			.direction   = direction,
			.radiance    = skyRadiance(sky, direction, wavelengths),
			.wavelengths = wavelengths,
			.exact       = false,
		};

		gathered = gathered + colourOf(wavelengths, skyAt(features, query));
	}
	return gathered * (1.0 / colourDraws);
}

Colour ambientColour(const Features& features, const Lighting& lighting) {
	const GradientSky sky   = lightsOf(lighting).sky;
	const math::Basis basis = math::basisAround(sky.up);
	Colour ambient          = skyColourAt(features, sky, sky.up) * ambientShare;
	for (int turn = 0; turn < ambientTurns; ++turn) {
		const double angle           = 2.0 * math::pi * turn / ambientTurns;
		const math::Vector direction = math::normalized(basis.tangent * std::cos(angle) + basis.bitangent * std::sin(angle) + sky.up * ambientTilt);
		ambient                      = ambient + skyColourAt(features, sky, direction) * (ambientShare / ambientTurns);
	}
	return ambient;
}

Spectrum4 lampTint(const Features& features, std::uint32_t light, const Wavelengths& wavelengths) {
	return sunPassed(features.atmosphere, light, wavelengths) * settingTint(features.layers, light, wavelengths);
}

Colour lampArriving(const Features& features, const Lighting& lighting, std::uint32_t light) {
	Colour gathered = { 0, 0, 0 };
	for (int draw = 0; draw < colourDraws; ++draw) {
		const Wavelengths wavelengths = spreadWavelengths((double(draw) + 0.5) / colourDraws);
		const Spectrum4 sent          = spectrumOf(lighting.lights[light].power, wavelengths) * lampTint(features, light, wavelengths);
		gathered                      = gathered + colourOf(wavelengths, sent);
	}
	return gathered * (1.0 / colourDraws);
}

std::uint32_t firstMoon(const Lighting& lighting) {
	for (std::uint32_t light = 0; light < lighting.lights.size(); ++light) {
		if (lighting.lights[light].kind == LightKind::Moon) {
			return light;
		}
	}
	return std::uint32_t(lighting.lights.size());
}

std::uint32_t cloudLamp(const Features& features, const Lighting& lighting) {
	const std::uint32_t sun  = firstSun(lighting);
	const std::uint32_t moon = firstMoon(lighting);
	const std::uint32_t none = std::uint32_t(lighting.lights.size());
	if (sun < none && (moon == none || math::dot(features.layers.up, features.layers.sun) > -0.05)) {
		return sun;
	}
	return moon;
}

Colour cloudLamplight(const Features& features, const Lighting& lighting, std::uint32_t lamp) {
	const Colour arriving = lampArriving(features, lighting, lamp);
	if (lighting.lights[lamp].kind != LightKind::Moon || firstSun(lighting) >= lighting.lights.size()) {
		return arriving;
	}
	const math::Vector moon = features.layers.lamps[lamp].direction;
	return arriving * (0.5 * (1.0 - math::dot(moon, features.layers.sun)));
}

CloudLight cloudLightOf(const Features& features, const Lighting& lighting, int threads) {
	const std::uint32_t lamp = cloudLamp(features, lighting);
	const bool lit           = lamp < lighting.lights.size() && lighting.clouds.present;
	const math::Vector up    = { 0, 0, 1 };
	return CloudLight {
		.lighting = lighting,
		.toward   = lit ? features.layers.lamps[lamp].direction : up,
		.light    = lit ? cloudLamplight(features, lighting, lamp) : Colour { 0, 0, 0 },
		.ambient  = lighting.clouds.present ? ambientColour(features, lighting) : Colour { 0, 0, 0 },
		.threads  = threads,
	};
}

std::uint32_t firstRung(const Features& features, const Looks& looks) {
	const bool scattering = std::find(features.members.begin(), features.members.end(), FeatureMember::Scattering) != features.members.end();
	std::uint32_t phases  = 0;
	for (const std::uint32_t phase : features.scattering.phases) {
		phases += scattering && phase != noMaterial ? 1 : 0;
	}
	return std::uint32_t(looks.materials.size()) + phases;
}

FeatureQuery featureQueryOf(const SceneFile& file, int threads) {
	return FeatureQuery {
		.scene    = file.scene,
		.looks    = file.looks,
		.lighting = file.lighting,
		.threads  = threads,
	};
}

Features featuresOf(const FeatureQuery& query) {
	const Looks& looks       = query.looks;
	const Lighting& lighting = query.lighting;

	Features features = {
		.members    = {},
		.absorption = absorptionOf(looks),
		.atmosphere = atmosphereOf(lighting),
		.layers     = skyLayersOf(lighting),
		.clouds     = {},
		.dispersion = dispersionOf(looks),
		.scattering = scatteringOf(looks),
		.coats      = {},
		.rules      = pathRulesOf(query.scene, lighting),
		.ruled      = false,
	};

	if (sceneAbsorbs(looks)) {
		features.members.push_back(FeatureMember::Absorption);
	}
	if (features.atmosphere.present) {
		features.members.push_back(FeatureMember::Atmosphere);
	}
	if (features.layers.painted) {
		features.members.push_back(FeatureMember::PaintedSky);
	}
	if (skyGlows(features.layers)) {
		features.members.push_back(FeatureMember::SkyGlow);
	}
	if (lighting.stars.present) {
		features.members.push_back(FeatureMember::Stars);
	}
	if (skyHasMoon(features.layers)) {
		features.members.push_back(FeatureMember::Moon);
	}
	features.clouds = cloudscapeOf(cloudLightOf(features, lighting, query.threads));
	if (features.clouds.present) {
		features.members.push_back(FeatureMember::Clouds);
	}
	if (sceneDisperses(looks)) {
		features.members.push_back(FeatureMember::Dispersion);
	}
	if (sceneFilmsGlass(looks)) {
		features.members.push_back(FeatureMember::ThinFilm);
	}
	if (sceneScatters(looks)) {
		features.members.push_back(FeatureMember::Scattering);
	}
	features.coats = coatLaddersOf(looks, firstRung(features, looks));
	if (sceneWearsCoats(query.scene)) {
		features.members.push_back(FeatureMember::Coats);
	}
	if (sceneHasRules(query.scene, lighting)) {
		features.members.push_back(FeatureMember::PathRules);
		features.ruled = true;
	}
	return features;
}

CoatWear coatWear(const WearQuery& query, const Shaded& shaded) {
	return CoatWear {
		.shading   = query.shading,
		.hit       = query.hit,
		.shaded    = shaded,
		.heading   = query.heading,
		.outside   = query.outside,
		.registers = query.registers,
	};
}

Shaded wornAt(const Features& features, const WearQuery& query) {
	Shaded shaded = query.shaded;
	for (const FeatureMember member : features.members) {
		switch (member) {
			case FeatureMember::Absorption:
			case FeatureMember::Atmosphere:
			case FeatureMember::PaintedSky:
			case FeatureMember::SkyGlow:
			case FeatureMember::Stars:
			case FeatureMember::Moon:
			case FeatureMember::Clouds:
			case FeatureMember::Dispersion:
			case FeatureMember::ThinFilm:
			case FeatureMember::Scattering:
			case FeatureMember::PathRules: break;
			case FeatureMember::Coats: shaded = wornCoats(features.coats, coatWear(query, shaded)); break;
		}
	}
	return shaded;
}

std::uint32_t mediumBody(const Features& features, std::uint32_t material) {
	return ladderBody(features.coats, material);
}

DispersedQuery dispersedQuery(const Features& features, const InterfaceQuery& query, const Sides& sides) {
	return DispersedQuery {
		.material    = mediumBody(features, query.material),
		.medium      = mediumBody(features, query.medium),
		.sides       = sides,
		.wavelengths = query.wavelengths,
	};
}

Sides sidesAt(const Features& features, const InterfaceQuery& query) {
	Sides sides = query.sides;
	for (const FeatureMember member : features.members) {
		switch (member) {
			case FeatureMember::Absorption:
			case FeatureMember::Atmosphere:
			case FeatureMember::PaintedSky:
			case FeatureMember::SkyGlow:
			case FeatureMember::Stars:
			case FeatureMember::Moon:
			case FeatureMember::Clouds: break;
			case FeatureMember::Dispersion: sides = dispersedSides(features.dispersion, dispersedQuery(features, query, sides)); break;
			case FeatureMember::ThinFilm: sides = filmedSides(features.dispersion, dispersedQuery(features, query, sides)); break;
			case FeatureMember::Scattering:
			case FeatureMember::Coats:
			case FeatureMember::PathRules: break;
		}
	}
	return sides;
}

ScatteredStretch scatteredStretch(const Features& features, const MediumQuery& query) {
	return ScatteredStretch {
		.material = mediumBody(features, query.material),
		.distance = query.distance,
		.walking  = query.use == MediumUse::Walking,
		.number   = query.number,
	};
}

MediumAnswer collisionAlong(const Features& features, const MediumQuery& query) {
	MediumAnswer answer = {
		.kept     = query.kept,
		.collided = false,
		.at       = query.distance,
	};

	for (const FeatureMember member : features.members) {
		if (member == FeatureMember::Scattering) {
			const Scattered scattered = scatteredAlong(features.scattering, scatteredStretch(features, query));
			answer.kept               = answer.kept * scattered.kept;
			answer.collided           = scattered.collided;
			answer.at                 = scattered.at;
		}
	}
	return answer;
}

MediumAnswer mediumAlong(const Features& features, const MediumQuery& query) {
	MediumAnswer answer = collisionAlong(features, query);
	MediumQuery kept    = query;
	kept.material       = mediumBody(features, query.material);
	kept.distance       = answer.at;
	for (const FeatureMember member : features.members) {
		switch (member) {
			case FeatureMember::Absorption: answer.kept = answer.kept * absorbedAlong(features, kept); break;
			case FeatureMember::Atmosphere:
			case FeatureMember::PaintedSky:
			case FeatureMember::SkyGlow:
			case FeatureMember::Stars:
			case FeatureMember::Moon:
			case FeatureMember::Clouds:
			case FeatureMember::Dispersion:
			case FeatureMember::ThinFilm:
			case FeatureMember::Scattering:
			case FeatureMember::Coats:
			case FeatureMember::PathRules: break;
		}
	}
	return answer;
}

bool mediumCollides(const Features& features, std::uint32_t medium) {
	const std::uint32_t body = mediumBody(features, medium);
	return body < features.scattering.coefficients.size() && features.scattering.coefficients[body] > 0;
}

Materials featuredMaterials(const Features& features, const Materials& materials, SmoothGlass smooth) {
	Materials featured = materials;
	for (const FeatureMember member : features.members) {
		if (member == FeatureMember::Scattering) {
			featured = withPhases(features.scattering, featured);
		}
		if (member == FeatureMember::Coats) {
			featured = withRungs(features.coats, featured, smooth);
		}
	}
	return featured;
}

double tableMean(const SpectrumTable& table) {
	double total = 0;
	for (const double value : table.values) {
		total += value;
	}
	return total / tablePlaces;
}

double passedMean(const Atmosphere& atmosphere, std::uint32_t light) {
	return atmosphere.present && light == atmosphere.sunLight ? tableMean(atmosphere.passed) : 1.0;
}

double settingMean(const SkyLayers& layers, std::uint32_t light) {
	return layers.lamps[light].tinted ? tableMean(layers.lamps[light].tint) : 1.0;
}

double lampThinning(const Features& features, std::uint32_t light, const math::Vector& point) {
	const bool thinned        = features.clouds.present && features.clouds.thinned[light];
	const math::Vector toward = features.layers.lamps[light].direction;
	const double overcasting  = thinned ? overcast(features.clouds, point, toward) : 1.0;
	return overcasting * passedMean(features.atmosphere, light) * settingMean(features.layers, light);
}

bool rulesSaid(const Features& features) {
	return features.ruled;
}

bool lightEnds(const Features& features, const RuleEnding& ending) {
	return !rulesSaid(features) || rulesLetEnd(features.rules, ending);
}

bool glowEnds(const Features& features, const GlowEnding& ending) {
	return !rulesSaid(features) || rulesLetGlow(features.rules, ending);
}

RuleState ruledStep(const Features& features, const RuleStep& step) {
	return rulesSaid(features) ? steppedRules(features.rules, step) : step.state;
}

RuleState ruledLightStep(const Features& features, const RuleStep& step) {
	return rulesSaid(features) ? lightSteppedRules(features.rules, step) : step.state;
}

bool passes(const Features& features, std::uint32_t leaf, Passage passage) {
	return rulesSaid(features) && rulesPass(features.rules, leaf, passage);
}

}
