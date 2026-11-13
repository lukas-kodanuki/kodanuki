#include "check.h"
#include "math/colour.h"
#include "math/random.h"
#include "plugins/feature/absorption/absorption.h"
#include "plugins/feature/coats/coats.h"
#include "plugins/feature/scattering/scattering.h"
#include "plugins/transport/paths/material/material.h"
#include "plugins/transport/paths/paths.h"
#include "syntax/evaluator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>

namespace kodanuki {

struct Swatch {
	const char* name;
	MaterialKind kind;
	float roughness;
	bool inside;
	double furnaceFaceOn;
	double furnaceGrazing;
};

Material swatchMaterial(const Swatch& swatch) {
	const Albedo white = {
		.stops     = { 1, 1, 1 },
		.program   = -1,
		.arguments = 0,
		.world     = false,
	};

	return Material {
		.kind       = swatch.kind,
		.albedo     = white,
		.roughness  = swatch.roughness,
		.refraction = 1.5f,
		.dispersion = 0,
		.scatter    = 0,
		.lean       = 0,
		.absorb     = { 0, 0, 0 },
		.glow       = { 0, 0, 0 },
		.strength   = 0,
		.film       = { 0, 1 },
		.guided     = true,
		.relays     = true,
		.aimed      = true,
	};
}

Bsdf swatchBsdf(const Swatch& swatch, const math::Vector& normal) {
	const bool glass    = swatch.kind == MaterialKind::Glass;
	const double within = swatch.inside ? 1.5 : 1;
	const double beyond = glass && !swatch.inside ? 1.5 : 1;

	const Sides sides = {
		.travelling = flatSpectrum(within),
		.beyond     = flatSpectrum(beyond),
		.parted     = false,
		.inside     = swatch.inside,
	};

	return Bsdf {
		.material = 0,
		.albedo   = { 1, 1, 1 },
		.normal   = normal,
		.sides    = sides,
	};
}

math::Vector leaningFrom(const math::Vector& normal, double cosine) {
	const math::Basis basis = math::basisAround(normal);
	const math::Vector lean = { std::sqrt(1 - cosine * cosine), 0, cosine };
	return math::toWorld(basis, lean);
}

LobeNumbers lobeNumbers(std::mt19937& random) {
	std::uniform_real_distribution<double> unit(0, 1);
	const math::UnitPair pair = { unit(random), unit(random) };
	return LobeNumbers {
		.pair = pair,
		.lobe = unit(random),
		.side = unit(random),
	};
}

struct Swatched {
	const Materials& materials;
	const Bsdf& bsdf;
	math::Vector outgoing;
};

struct Survey {
	double furnace;
	double found;
	double density;
	double weightGap;
	double densityGap;
	double reciprocity;
};

double relativeGap(double a, double b) {
	return std::abs(a - b) / std::max({ std::abs(a), std::abs(b), 1e-9 });
}

Scatter drawnScatter(const Swatched& probe, std::mt19937& random, const Wavelengths& wavelengths) {
	const BsdfDraw draw = {
		.outgoing    = probe.outgoing,
		.travel      = Travel::FromEye,
		.numbers     = lobeNumbers(random),
		.wavelengths = wavelengths,
	};

	return sampleBsdf(probe.materials, probe.bsdf, draw);
}

Survey surveyed(const Swatched& probe) {
	std::mt19937 random(11);
	const Wavelengths wavelengths = sampledWavelengths(0.4);
	Survey survey                 = { 0, 0, 0, 0, 0, 0 };
	const int count               = 200000;
	for (int i = 0; i < count; ++i) {
		const Scatter scatter = drawnScatter(probe, random, wavelengths);
		if (!scatter.found) {
			continue;
		}
		survey.furnace += laneMean(scatter.weight) / count;
		survey.found += 1.0 / count;

		const BsdfPair pair = {
			.outgoing    = probe.outgoing,
			.incoming    = scatter.incoming,
			.wavelengths = wavelengths,
		};

		const Response response = respondBsdf(probe.materials, probe.bsdf, pair);
		const double cosine     = std::abs(math::dot(probe.bsdf.normal, scatter.incoming));
		survey.weightGap        = std::max(survey.weightGap, relativeGap(laneMean(scatter.weight), laneMean(response.value) * cosine / response.density));
		survey.densityGap       = std::max(survey.densityGap, relativeGap(scatter.density, response.density));
	}
	return survey;
}

double densityAt(const Swatched& probe, const math::Vector& incoming, const Wavelengths& wavelengths) {
	const BsdfPair asked = {
		.outgoing    = probe.outgoing,
		.incoming    = incoming,
		.wavelengths = wavelengths,
	};

	return bsdfDensity(probe.materials, probe.bsdf, asked);
}

double densityIntegral(const Swatched& probe) {
	std::mt19937 random(9);
	std::uniform_real_distribution<double> unit(0, 1);
	const Wavelengths wavelengths = sampledWavelengths(0.4);
	const int count               = 400000;
	double total                  = 0;
	for (int i = 0; i < count; ++i) {
		const math::UnitPair pair = { unit(random), unit(random) };
		const Scatter scatter     = drawnScatter(probe, random, wavelengths);
		const bool even           = i % 2 == 0;
		if (!even && !scatter.found) {
			continue;
		}
		const math::Vector incoming = even ? math::uniformSphere(pair) : scatter.incoming;
		const double density        = densityAt(probe, incoming, wavelengths);
		total += density / (0.5 / (4 * math::pi) + 0.5 * density) / count;
	}
	return total;
}

double reciprocityGap(const Swatched& probe) {
	std::mt19937 random(5);
	std::uniform_real_distribution<double> unit(0, 1);
	const Wavelengths wavelengths = sampledWavelengths(0.4);
	double worst                  = 0;
	for (int i = 0; i < 2000; ++i) {
		const math::UnitPair pair   = { unit(random), unit(random) };
		const math::Vector incoming = math::cosineHemisphere(math::basisAround(probe.bsdf.normal), pair);

		const BsdfPair forward = {
			.outgoing    = probe.outgoing,
			.incoming    = incoming,
			.wavelengths = wavelengths,
		};

		const BsdfPair backward = {
			.outgoing    = incoming,
			.incoming    = probe.outgoing,
			.wavelengths = wavelengths,
		};

		const double there = laneMean(respondBsdf(probe.materials, probe.bsdf, forward).value);
		const double back  = laneMean(respondBsdf(probe.materials, probe.bsdf, backward).value);
		worst              = std::max(worst, relativeGap(there, back));
	}
	return worst;
}

void checkSwatch(const Swatch& swatch, double cosine) {
	const Materials materials = {
		.finishes = { finishOf(swatchMaterial(swatch), SmoothGlass::Lobe) },
		.passable = passable(swatchMaterial(swatch)),
	};

	const math::Vector normal = math::normalized(math::Vector { 0.3, -0.2, 1 });
	const Bsdf bsdf           = swatchBsdf(swatch, normal);

	const Swatched probe = {
		.materials = materials,
		.bsdf      = bsdf,
		.outgoing  = leaningFrom(normal, cosine),
	};

	const Survey survey     = surveyed(probe);
	const double integral   = densityIntegral(probe);
	const double reciprocal = swatch.kind == MaterialKind::Glass ? 0 : reciprocityGap(probe);
	std::printf("shading %s at cosine %.2f: furnace %.4f, found %.4f, density integral %.4f, weight gap %.1e, density gap %.1e, reciprocity gap %.1e\n", swatch.name, cosine, survey.furnace, survey.found, integral, survey.weightGap, survey.densityGap, reciprocal);
	const std::string name = swatch.name;
	checkNear(Near { integral, survey.found, 0.01, (name + ": the density integrates to the share of draws that find a direction").c_str() });
	check(survey.weightGap < 1e-9 && survey.densityGap < 1e-9, (name + ": a drawn direction's weight and density are what respondBsdf answers").c_str());
	check(reciprocal < 1e-6, (name + ": the value is the same both ways").c_str());
	checkNear(Near { survey.furnace, cosine > 0.5 ? swatch.furnaceFaceOn : swatch.furnaceGrazing, 0.006, (name + ": a white surface in a white furnace returns what it is given").c_str() });
}

struct ExactSurvey {
	double furnace;
	double mirrored;
	double density;
};

ExactSurvey exactSurvey(const Swatched& probe) {
	std::mt19937 random(13);
	const Wavelengths wavelengths = sampledWavelengths(0.4);
	ExactSurvey survey            = { 0, 0, 0 };
	const int count               = 100000;
	for (int i = 0; i < count; ++i) {
		const Scatter scatter = drawnScatter(probe, random, wavelengths);

		const BsdfPair pair = {
			.outgoing    = probe.outgoing,
			.incoming    = scatter.incoming,
			.wavelengths = wavelengths,
		};

		const Response response = respondBsdf(probe.materials, probe.bsdf, pair);
		const math::Vector to   = math::mirrored(probe.outgoing, probe.bsdf.normal);
		survey.furnace += scatter.found ? laneMean(scatter.weight) / count : 0;
		survey.mirrored += scatter.found && math::length(scatter.incoming - to) < 1e-12 ? 1.0 / count : 0;
		survey.density = std::max({ survey.density, scatter.density, response.density, laneMean(response.value) });
	}
	return survey;
}

void checkExactGlass(bool inside, double cosine) {
	const Swatch swatch = { "exact glass", MaterialKind::Glass, 0, inside, 1, 1 };

	const Materials materials = {
		.finishes = { finishOf(swatchMaterial(swatch), SmoothGlass::Exact) },
		.passable = passable(swatchMaterial(swatch)),
	};

	const math::Vector normal = math::normalized(math::Vector { 0.3, -0.2, 1 });
	const Bsdf bsdf           = swatchBsdf(swatch, normal);

	const Swatched probe = {
		.materials = materials,
		.bsdf      = bsdf,
		.outgoing  = leaningFrom(normal, cosine),
	};

	const ExactSurvey survey = exactSurvey(probe);
	const double fresnel     = dielectricReflectance(cosine, inside ? 1.5 : 1 / 1.5);
	std::printf("shading exact glass from %s at cosine %.2f: furnace %.4f, mirrored %.4f (Fresnel %.4f), largest density or value %.1e\n", inside ? "inside" : "outside", cosine, survey.furnace, survey.mirrored, fresnel, survey.density);
	checkNear(Near { survey.furnace, 1, 1e-9, "an exact glass in a white furnace returns what it is given" });
	checkNear(Near { survey.mirrored, fresnel, 0.005, "an exact glass mirrors as often as its Fresnel term says" });
	check(survey.density == 0, "an exact glass draws with density 0 and answers 0 to every pair asked");
}

void checkAbsorption() {
	const Absorption absorption = {
		.coefficients = { { 0.5, 0.5, 0.5 }, { 1.0, 0.3, 0.1 } },
	};

	const Wavelengths wavelengths = sampledWavelengths(0.6);

	const AbsorbedStretch grey = {
		.material    = 0,
		.distance    = 2,
		.wavelengths = wavelengths,
	};

	checkNear(Near { laneMean(keptThrough(absorption, grey)), std::exp(-1.0), 1e-12, "a grey glass keeps e to the minus its coefficient times the distance" });
	Colour mean = { 0, 0, 0 };
	for (int k = 0; k < 4096; ++k) {
		const Wavelengths drawn = sampledWavelengths((k + 0.5) / 4096);

		const AbsorbedStretch tinted = {
			.material    = 1,
			.distance    = 1,
			.wavelengths = drawn,
		};

		mean = mean + colourOf(drawn, keptThrough(absorption, tinted)) * (1.0 / 4096);
	}
	std::printf("shading absorption [1, 0.3, 0.1] over 1 m keeps [%.4f, %.4f, %.4f]\n", mean.red, mean.green, mean.blue);
	check(mean.red < mean.green && mean.green < mean.blue, "a glass that absorbs red most keeps blue most");
}

void checkFilm() {
	const Film bare = {
		.thickness = 1e-9,
		.index     = 1.33,
	};

	double worst = 0;
	for (const double cosine : { 1.0, 0.7, 0.3, 0.05 }) {
		const FilmSides sides = {
			.facing     = cosine,
			.nanometres = 550,
			.outside    = 1,
			.beneath    = 1.5,
		};

		worst = std::max(worst, std::abs(filmReflectance(bare, sides) - dielectricReflectance(cosine, 1 / 1.5)));
	}
	std::printf("shading film of no thickness against the bare Fresnel term: worst gap %.2e\n", worst);
	check(worst < 1e-9, "a film of no thickness reflects as the bare surface beneath it");

	const Film soap = {
		.thickness = 420,
		.index     = 1.33,
	};

	const FilmSides blue = {
		.facing     = 1,
		.nanometres = 450,
		.outside    = 1,
		.beneath    = 1,
	};

	FilmSides red  = blue;
	red.nanometres = 650;
	check(std::abs(filmReflectance(soap, blue) - filmReflectance(soap, red)) > 0.01, "a soap film reflects blue and red differently");
}

double phaseIntegral(const Materials& materials, const Bsdf& bsdf, const Wavelengths& wavelengths) {
	const int rows = 400;
	double total   = 0;
	double gap     = 0;
	for (int i = 0; i < rows; ++i) {
		for (int j = 0; j < rows; ++j) {
			const math::UnitPair pair = {
				.u = (i + 0.5) / rows,
				.v = (j + 0.5) / rows,
			};

			const LobeNumbers numbers = {
				.pair = pair,
				.lobe = 0.5,
				.side = 0.5,
			};

			const BsdfDraw draw = {
				.outgoing    = { 0, 0, -1 },
				.travel      = Travel::FromEye,
				.numbers     = numbers,
				.wavelengths = wavelengths,
			};

			const Scatter drawn = sampleBsdf(materials, bsdf, draw);

			const BsdfPair asked = {
				.outgoing    = { 0, 0, -1 },
				.incoming    = drawn.incoming,
				.wavelengths = wavelengths,
			};

			gap = std::max(gap, std::abs(respondBsdf(materials, bsdf, asked).density - drawn.density));
			total += 1 / drawn.density;
		}
	}
	check(gap < 1e-9, "a phase draw's density is what respondBsdf answers");
	return total / (rows * rows);
}

void checkPhase() {
	const Wavelengths wavelengths = spreadWavelengths(0.3);
	for (const double lean : { 0.0, 0.3, -0.6 }) {
		const Materials materials = {
			.finishes = { phaseFinish(lean) },
			.passable = false,
		};

		const Sides sides = {
			.travelling = flatSpectrum(1),
			.beyond     = flatSpectrum(1),
			.parted     = false,
			.inside     = true,
		};

		const Bsdf bsdf = {
			.material = 0,
			.albedo   = { 1, 1, 1 },
			.normal   = { 0, 0, 1 },
			.sides    = sides,
		};

		const double sphere = phaseIntegral(materials, bsdf, wavelengths);
		std::printf("shading phase lean %.1f: mean of 1 / density over its draws %.5f (4 pi = %.5f)\n", lean, sphere, 4 * math::pi);
		check(std::abs(sphere / (4 * math::pi) - 1) < 0.01, "the phase function's density integrates to one over the sphere");
	}

	const Scattering scattering = {
		.coefficients = { 2 },
		.leans        = { 0 },
		.phases       = { 1 },
	};

	double travelled = 0;
	for (int i = 0; i < 10000; ++i) {
		const ScatteredStretch walking = {
			.material = 0,
			.distance = 1e9,
			.walking  = true,
			.number   = (i + 0.5) / 10000,
		};

		travelled += scatteredAlong(scattering, walking).at;
	}

	const ScatteredStretch crossing = {
		.material = 0,
		.distance = 0.7,
		.walking  = false,
		.number   = 0.5,
	};

	std::printf("shading scattering 2 per metre: mean free path %.4f, kept over 0.7 m crossing %.4f\n", travelled / 10000, scatteredAlong(scattering, crossing).kept);
	check(std::abs(travelled / 10000 - 0.5) < 0.005, "a walking ray collides after 1 / the coefficient on average");
	check(std::abs(scatteredAlong(scattering, crossing).kept - std::exp(-1.4)) < 1e-12, "a crossing ray keeps e to the minus the coefficient times the distance");
}

const char* coatedText = R"(
	function sphere(r) {
		return length(p) - r;
	}

	let red  = plastic([0.7, 0.2, 0.15], roughness = 0.4);
	let gilt = metal([0.95, 0.75, 0.35], roughness = 0.2);

	coat(tint([0, 0, 0]), above(100)) coat(rough(0.5), above(100)) paint(red) sphere(1);
	coat(bump(grain, 0, span = 0.05)) paint(red) at([3, 0, 0]) sphere(1);
	coat(bump(grain, 0.05, span = 0.05)) paint(red) at([6, 0, 0]) sphere(1);
	coat(gilt, above(0)) paint(red) at([9, 0, 0]) sphere(1);
)";

struct CoatedHit {
	const Scene& scene;
	const Looks& looks;
	const Materials& materials;
	const CoatLadders& ladders;
	std::uint32_t leaf;
};

Hit coatHit(std::uint32_t leaf, const math::Vector& point, const math::Vector& normal) {
	return Hit {
		.kind     = HitKind::Surface,
		.distance = 1,
		.point    = point,
		.normal   = normal,
		.leaf     = leaf,
		.other    = noLeaf,
		.share    = 0,
		.loosened = 0,
		.leafKind = LeafKind::Held,
		.cell     = 0,
	};
}

struct CoatedPair {
	Shaded bare;
	Shaded worn;
};

CoatedPair coatedAt(const CoatedHit& coated, const math::Vector& point) {
	const math::Vector centre  = { coated.scene.leaves[coated.leaf].placement.origin[0], 0, 0 };
	const math::Vector normal  = math::normalized(point - centre);
	const math::Vector heading = -normal;
	const Hit hit              = coatHit(coated.leaf, point, normal);
	std::vector<double> registers;

	const Shading shading = {
		.scene     = coated.scene,
		.looks     = coated.looks,
		.materials = coated.materials,
	};

	const ShadingQuery query = {
		.hit       = hit,
		.heading   = heading,
		.medium    = noMaterial,
		.registers = registers,
	};

	const Shaded bare = bsdfAt(shading, query);

	const CoatWear wear = {
		.shading   = shading,
		.hit       = hit,
		.shaded    = bare,
		.heading   = heading,
		.outside   = true,
		.registers = registers,
	};

	return CoatedPair {
		.bare = bare,
		.worn = wornCoats(coated.ladders, wear),
	};
}

bool sameShading(const Shaded& a, const Shaded& b) {
	const bool albedo = a.bsdf.albedo.red == b.bsdf.albedo.red && a.bsdf.albedo.green == b.bsdf.albedo.green && a.bsdf.albedo.blue == b.bsdf.albedo.blue;
	const bool normal = a.bsdf.normal.x == b.bsdf.normal.x && a.bsdf.normal.y == b.bsdf.normal.y && a.bsdf.normal.z == b.bsdf.normal.z;
	return albedo && normal && a.bsdf.material == b.bsdf.material;
}

void checkCoatRungs(const SceneFile& file, const CoatLadders& ladders, const Materials& materials) {
	const std::uint32_t red = file.scene.leaves[0].material;
	bool same               = ladders.ladders[red] != noMaterial;
	for (int step = 0; same && step <= roughnessSteps; ++step) {
		Material rung        = file.looks.materials[red];
		rung.roughness       = float(double(step) / roughnessSteps);
		const Finish wanted  = finishOf(rung, SmoothGlass::Exact);
		const Finish& gotten = materials.finishes[ladders.ladders[red] + std::uint32_t(step)];
		same                 = gotten.width == wanted.width && gotten.kind == wanted.kind && ladderBody(ladders, ladders.ladders[red] + std::uint32_t(step)) == red;
	}
	check(same, "a ladder's rung k is the material's finish at roughness k/16");
}

void checkCoatDither(const CoatLadders& ladders, std::uint32_t material) {
	std::mt19937 random(7);
	std::uniform_real_distribution<double> place(-5, 5);
	const int points = 10000;
	int upper        = 0;
	for (int k = 0; k < points; ++k) {
		const math::Vector point = { place(random), place(random), place(random) };
		upper += ladderedMaterial(ladders, material, 0.53, point) == ladders.ladders[material] + 9 ? 1 : 0;
	}
	const double share = double(upper) / points;
	std::printf("coats: the upper rung of roughness 0.53 picked at %.4f, want %.4f\n", share, 0.53 * 16 - 8);
	check(std::abs(share - (0.53 * 16 - 8)) < 0.02, "the rung dither picks the upper rung at the share the roughness lies past the lower");
}

void checkCoatLadders(const SceneFile& file, const CoatLadders& ladders, const Materials& materials) {
	if (!coatsRoughen) {
		check(ladders.rungs.empty() && ladders.laddered.empty(), "a build with the coats off climbs no ladders");
		return;
	}
	checkCoatRungs(file, ladders, materials);
	checkCoatDither(ladders, file.scene.leaves[0].material);
}

CoatedPair coatedLeaf(const CoatedHit& coated, std::uint32_t leaf, const math::Vector& point) {
	CoatedHit hit = coated;
	hit.leaf      = leaf;
	return coatedAt(hit, point);
}

void checkCoats() {
	const SceneFile file      = sceneOrExit(writeScene("coats", coatedText));
	const CoatLadders ladders = coatLaddersOf(file.looks, std::uint32_t(file.looks.materials.size()));
	const Materials materials = withRungs(ladders, materialsOf(file.looks, SmoothGlass::Exact), SmoothGlass::Exact);
	const math::Vector side   = math::normalized({ 0.3, -0.8, 0.5 });
	checkCoatLadders(file, ladders, materials);

	const CoatedHit coated = {
		.scene     = file.scene,
		.looks     = file.looks,
		.materials = materials,
		.ladders   = ladders,
		.leaf      = 0,
	};

	const CoatedPair unworn = coatedLeaf(coated, 0, side);
	check(sameShading(unworn.bare, unworn.worn), "coats of weight 0 change nothing");
	const CoatedPair level = coatedLeaf(coated, 1, math::Vector { 3, 0, 0 } + side);
	check(sameShading(level.bare, level.worn), "a bump of amount 0 leaves the normal");
	const CoatedPair tilted = coatedLeaf(coated, 2, math::Vector { 6, 0, 0 } + side);
	const double turned     = math::dot(tilted.worn.bsdf.normal, tilted.bare.bsdf.normal);
	check(turned < 1 - 1e-6 && turned > 0.5 && math::dot(tilted.worn.bsdf.normal, side) > 0, "a bump tilts the normal, which still faces the ray");
	const CoatedPair painted = coatedLeaf(coated, 3, math::Vector { 9, 0, 0 } + side);
	const std::uint32_t gilt = file.looks.coats[file.scene.leaves[3].coats[0]].material;
	check(painted.worn.bsdf.material == gilt, "a paint of full weight becomes its material");
}

void checkShading() {
	checkCoats();
	checkAbsorption();
	checkPhase();
	checkFilm();

	const Swatch swatches[] = {
		{ "matte", MaterialKind::Matte, 1, false, 1, 1 },
		{ "rough metal", MaterialKind::Metal, 0.6f, false, 0.998, 0.989 },
		{ "smooth metal", MaterialKind::Metal, 0.15f, false, 1, 1 },
		{ "rough plastic", MaterialKind::Plastic, 0.5f, false, 0.965, 1.034 },
		{ "smooth plastic", MaterialKind::Plastic, 0.1f, false, 0.957, 1.121 },
		{ "rough glass from outside", MaterialKind::Glass, 0.5f, false, 1.005, 1.018 },
		{ "rough glass from inside", MaterialKind::Glass, 0.5f, true, 0.998, 1.009 },
		{ "smooth glass from outside", MaterialKind::Glass, 0.05f, false, 1, 1 },
	};

	for (const Swatch& swatch : swatches) {
		checkSwatch(swatch, 0.8);
		checkSwatch(swatch, 0.3);
	}
	for (const bool inside : { false, true }) {
		checkExactGlass(inside, 0.8);
		checkExactGlass(inside, 0.3);
	}
}

}
