#include "material/material.h"

#include "sdf/program.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kodanuki {

constexpr double coatIndex      = 1.5;
constexpr double sharpRough     = 0.25;
constexpr double noticedLoss    = 1e-3;
constexpr double broadSharpness = 1.0;

struct Broad {
	double share;
	double through;
};

struct Glass {
	const Finish& finish;
	const Bsdf& bsdf;
	const Wavelengths& wavelengths;
};

struct Facing {
	math::Vector outgoing;
	math::Vector incoming;
	double leaving;
	double arriving;
};

struct MirrorLobe {
	double back;
	double facets;
};

struct FinishTables {
	Escape escape;
	Boundary boundary;
};

struct Bent {
	double stretch;
	math::Vector facet;
};

struct BentQuery {
	const Facing& facing;
	const math::Vector& normal;
	Indices indices;
};

Escape losslessEscape() {
	Escape escape = {
		.table   = {},
		.average = 1,
	};

	escape.table.fill(1);
	return escape;
}

Boundary emptyBoundary() {
	return Boundary {
		.rarer         = {},
		.denser        = {},
		.rarerAverage  = {},
		.denserAverage = {},
		.rarerThrough  = {},
	};
}

double rampMeanLuminance(const Albedo& albedo) {
	Colour sum          = { 0, 0, 0 };
	const std::size_t n = albedo.stops.size() / 3;
	for (std::size_t i = 0; i < n; ++i) {
		sum = sum + colourFrom(albedo.stops.data() + 3 * i);
	}
	return luminanceOf(sum * (1.0 / double(n)));
}

Coating coatingOf(double average) {
	const double faced = dielectricReflectance(1, 1 / coatIndex);
	const double front = faced + (1 - faced) / 21;
	return Coating {
		.front = front,
		.twice = front * front * average / (1 - front * (1 - average)),
		.room  = 1 - front * average,
	};
}

bool losesMuchOf(const Boundary& boundary) {
	double least = 1;
	for (int row = 0; row < boundaryRatios; ++row) {
		least = std::min(least, std::min(boundary.rarerAverage[row], boundary.denserAverage[row]));
	}
	return least < 1 - noticedLoss;
}

bool answersExactly(const Material& material, SmoothGlass smooth) {
	return material.kind == MaterialKind::Glass && material.roughness == 0 && smooth == SmoothGlass::Exact;
}

Film filmOf(const Material& material) {
	return Film {
		.thickness = material.kind == MaterialKind::Matte ? 0 : double(material.film[0]),
		.index     = double(material.film[1]),
	};
}

bool filmed(const Film& film) {
	return film.thickness > 0;
}

struct Amplitude {
	double perpendicular;
	double parallel;
};

struct FilmFace {
	double before;
	double after;
	double facing;
	double onward;
};

Amplitude faceAmplitude(const FilmFace& face) {
	return Amplitude {
		.perpendicular = (face.before * face.facing - face.after * face.onward) / (face.before * face.facing + face.after * face.onward),
		.parallel      = (face.after * face.facing - face.before * face.onward) / (face.after * face.facing + face.before * face.onward),
	};
}

double airySum(double front, double back, double shift) {
	const double both   = front * back;
	const double top    = front * front + back * back + 2 * both * shift;
	const double bottom = 1 + both * both + 2 * both * shift;
	return bottom > 0 ? std::clamp(top / bottom, 0.0, 1.0) : 1;
}

double bentCosine(double before, double facing, double after) {
	const double sine = before / after * std::sqrt(std::max(1 - facing * facing, 0.0));
	return sine >= 1 ? -1 : std::sqrt(1 - sine * sine);
}

double filmReflectance(const Film& film, const FilmSides& sides) {
	const double inside = bentCosine(sides.outside, sides.facing, film.index);
	const double beyond = inside < 0 ? -1 : bentCosine(film.index, inside, sides.beneath);
	if (inside < 0 || beyond < 0) {
		return 1;
	}

	const FilmFace front = {
		.before = sides.outside,
		.after  = film.index,
		.facing = sides.facing,
		.onward = inside,
	};

	const FilmFace back = {
		.before = film.index,
		.after  = sides.beneath,
		.facing = inside,
		.onward = beyond,
	};

	const Amplitude near = faceAmplitude(front);
	const Amplitude far  = faceAmplitude(back);
	const double shift   = std::cos(4 * math::pi * film.index * film.thickness * inside / sides.nanometres);
	return 0.5 * (airySum(near.perpendicular, far.perpendicular, shift) + airySum(near.parallel, far.parallel, shift));
}

double finishWidthOf(const Material& material) {
	return material.kind == MaterialKind::Matte ? 1 : facetWidthOf(double(material.roughness));
}

FinishTables freshTables(const Material& material) {
	const double width = finishWidthOf(material);
	return FinishTables {
		.escape   = material.kind == MaterialKind::Matte ? losslessEscape() : escapeOf(width),
		.boundary = material.kind == MaterialKind::Glass ? boundaryOf(width) : emptyBoundary(),
	};
}

Finish finishWith(const Material& material, SmoothGlass smooth, const FinishTables& made) {
	const bool glass      = material.kind == MaterialKind::Glass;
	const double width    = finishWidthOf(material);
	const bool sharpMetal = material.kind == MaterialKind::Metal && double(material.roughness) < sharpRough;
	return Finish {
		.kind       = material.kind,
		.width      = width,
		.sharpness  = lobeSharpness(width),
		.sharp      = glass || sharpMetal,
		.facetShare = std::clamp(made.escape.average, 0.25, 1.0),
		.body       = rampMeanLuminance(material.albedo),
		.coating    = coatingOf(made.escape.average),
		.index      = glass ? double(material.refraction) : 1,
		.escape     = made.escape,
		.boundary   = made.boundary,
		.losesMuch  = glass && losesMuchOf(made.boundary),
		.exact      = answersExactly(material, smooth),
		.film       = filmOf(material),
		.lean       = 0,
	};
}

Finish finishOf(const Material& material, SmoothGlass smooth) {
	return finishWith(material, smooth, freshTables(material));
}

bool tablesShared(const Finish& finish, const Material& material) {
	const bool laid  = finish.kind != MaterialKind::Matte && finish.kind != MaterialKind::Medium;
	const bool glass = material.kind == MaterialKind::Glass;
	return laid && (!glass || finish.kind == MaterialKind::Glass) && finish.width == finishWidthOf(material);
}

FinishTables tablesFrom(const Finish& finish, const Material& material) {
	return FinishTables {
		.escape   = finish.escape,
		.boundary = material.kind == MaterialKind::Glass ? finish.boundary : emptyBoundary(),
	};
}

FinishTables tablesFor(const Materials& made, const Material& material) {
	if (material.kind == MaterialKind::Matte) {
		return freshTables(material);
	}
	for (const Finish& finish : made.finishes) {
		if (tablesShared(finish, material)) {
			return tablesFrom(finish, material);
		}
	}
	return freshTables(material);
}

Finish finishAmong(const Materials& made, const Material& material, SmoothGlass smooth) {
	return finishWith(material, smooth, tablesFor(made, material));
}

Finish phaseFinish(double lean) {
	return Finish {
		.kind       = MaterialKind::Medium,
		.width      = 1,
		.sharpness  = broadSharpness,
		.sharp      = false,
		.facetShare = 0,
		.body       = 0,
		.coating    = coatingOf(1),
		.index      = 1,
		.escape     = losslessEscape(),
		.boundary   = emptyBoundary(),
		.losesMuch  = false,
		.exact      = false,
		.film       = Film {
			.thickness = 0,
			.index     = 1,
		},

		.lean = lean,
	};
}

bool passable(const Material& material) {
	return material.kind == MaterialKind::Glass && material.refraction == 1 && material.dispersion == 0 && material.film[0] == 0;
}

Materials materialsOf(const Looks& looks, SmoothGlass smooth) {
	Materials materials = {
		.finishes = {},
		.passable = std::any_of(looks.materials.begin(), looks.materials.end(), passable),
	};

	for (const Material& material : looks.materials) {
		materials.finishes.push_back(finishAmong(materials, material, smooth));
	}
	return materials;
}

Colour rampAt(const std::vector<float>& stops, double value) {
	const std::size_t n = stops.size() / 3;
	if (n == 1) {
		return colourFrom(stops.data());
	}
	const double place      = std::clamp(value, 0.0, 1.0) * double(n - 1);
	const std::size_t lower = std::min(std::size_t(place), n - 2);
	const double share      = place - double(lower);
	return colourFrom(stops.data() + 3 * lower) * (1 - share) + colourFrom(stops.data() + 3 * lower + 3) * share;
}

math::Vector placedVector(const Placement& placement, const math::Vector& v, bool offset) {
	const math::Vector origin = offset ? math::Vector { placement.origin[0], placement.origin[1], placement.origin[2] } : math::Vector { 0, 0, 0 };
	const math::Vector d      = v - origin;
	const double* row[3]      = { placement.linear[0], placement.linear[1], placement.linear[2] };
	return { row[0][0] * d.x + row[0][1] * d.y + row[0][2] * d.z, row[1][0] * d.x + row[1][1] * d.y + row[1][2] * d.z, row[2][0] * d.x + row[2][1] * d.y + row[2][2] * d.z };
}

double patternAt(const PatternSpot& spot, const Albedo& source, std::vector<double>& registers) {
	const bool local        = !source.world && spot.leaf != noLeaf;
	const Placement& placed = spot.scene.leaves[local ? spot.leaf : 0].placement;
	const math::Vector p    = local ? placedVector(placed, spot.point, true) : spot.point;
	const math::Vector n    = local ? math::normalized(placedVector(placed, spot.normal, false)) : spot.normal;

	const Inputs<double> inputs = {
		.arguments = spot.scene.arguments.data() + source.arguments,
		.p         = { p.x, p.y, p.z },
		.n         = { n.x, n.y, n.z },
	};

	return evaluate(spot.scene.programs[std::size_t(source.program)], inputs, registers);
}

Colour albedoAt(const PatternSpot& spot, const Albedo& albedo, std::vector<double>& registers) {
	if (albedo.program < 0) {
		return colourFrom(albedo.stops.data());
	}
	return rampAt(albedo.stops, patternAt(spot, albedo, registers));
}

std::uint32_t materialOfLeaf(const Scene& scene, std::uint32_t leaf) {
	return leaf == noLeaf ? 0 : scene.leaves[leaf].material;
}

Colour glowOf(const Material& material) {
	return material.glow * double(material.strength);
}

Sides sidesOf(const Materials& materials, std::uint32_t material, std::uint32_t medium) {
	const bool inside   = medium != noMaterial;
	const double within = inside ? materials.finishes[medium].index : 1;
	const double across = inside ? 1 : materials.finishes[material].index;
	return Sides {
		.travelling = flatSpectrum(within),
		.beyond     = flatSpectrum(across),
		.parted     = false,
		.inside     = inside,
	};
}

Colour mixed(const Colour& near, const Colour& far, double share) {
	return near * (1 - share) + far * share;
}

Shaded bsdfAt(const Shading& shading, const ShadingQuery& query) {
	const Scene& scene           = shading.scene;
	const Hit& hit               = query.hit;
	const std::uint32_t material = materialOfLeaf(scene, hit.leaf);
	const math::Vector normal    = math::facingAgainst(hit.normal, query.heading);
	const bool outside           = math::dot(hit.normal, query.heading) < 0;

	const PatternSpot spot = {
		.scene  = scene,
		.leaf   = hit.leaf,
		.point  = hit.point,
		.normal = normal,
	};

	const Colour black = { 0, 0, 0 };
	Colour albedo      = albedoAt(spot, shading.looks.materials[material].albedo, query.registers);
	Colour glow        = outside ? glowOf(shading.looks.materials[material]) : black;
	if (hit.other != noLeaf && hit.share > 0) {
		const std::uint32_t across = materialOfLeaf(scene, hit.other);

		const PatternSpot there = {
			.scene  = scene,
			.leaf   = hit.other,
			.point  = hit.point,
			.normal = normal,
		};

		albedo = mixed(albedo, albedoAt(there, shading.looks.materials[across].albedo, query.registers), hit.share);
		glow   = outside ? mixed(glow, glowOf(shading.looks.materials[across]), hit.share) : black;
	}

	const Bsdf bsdf = {
		.material = material,
		.albedo   = albedo,
		.normal   = normal,
		.sides    = sidesOf(shading.materials, material, query.medium),
	};

	return Shaded {
		.bsdf = bsdf,
		.glow = glow,
	};
}

Response noResponse(double density) {
	return Response {
		.value   = flatSpectrum(0),
		.density = density,
	};
}

Scatter noScatter() {
	return Scatter {
		.found     = false,
		.incoming  = { 0, 0, 0 },
		.weight    = flatSpectrum(0),
		.density   = 0,
		.sharpness = 1,
		.through   = false,
	};
}

Facing facingOf(const Bsdf& bsdf, const BsdfPair& pair) {
	return Facing {
		.outgoing = pair.outgoing,
		.incoming = pair.incoming,
		.leaving  = math::dot(bsdf.normal, pair.outgoing),
		.arriving = math::dot(bsdf.normal, pair.incoming),
	};
}

Spectrum4 matteValue(const Bsdf& bsdf, const BsdfPair& pair) {
	const Facing facing = facingOf(bsdf, pair);
	if (facing.leaving <= 0 || facing.arriving <= 0) {
		return flatSpectrum(0);
	}
	return spectrumOf(bsdf.albedo * (1 / math::pi), pair.wavelengths);
}

Response matteResponse(const Bsdf& bsdf, const BsdfPair& pair) {
	return Response {
		.value   = matteValue(bsdf, pair),
		.density = math::cosineDensity(bsdf.normal, pair.incoming),
	};
}

double plasticShare(const Finish& finish, double leaving) {
	const double face  = dielectricReflectance(leaving, 1 / coatIndex);
	const double share = face / std::max(face + (1 - face) * finish.body, 1e-12);
	return std::clamp(share, 0.25, 0.9);
}

double facetShareOf(const Finish& finish, double leaving) {
	return finish.kind == MaterialKind::Metal ? finish.facetShare : plasticShare(finish, leaving);
}

double roughDensity(const Finish& finish, const Bsdf& bsdf, const Facing& facing) {
	if (finish.kind == MaterialKind::Plastic && facing.leaving <= 0) {
		return 0;
	}
	const double share = facetShareOf(finish, facing.leaving);

	const FacetQuery query = {
		.normal   = bsdf.normal,
		.outgoing = facing.outgoing,
		.incoming = facing.incoming,
		.width    = finish.width,
	};

	return share * visibleFacetDensity(query) + (1 - share) * math::cosineDensity(bsdf.normal, facing.incoming);
}

struct Lobe {
	double onto;
	double tilt;
	double facets;
	double lost;
};

struct Halfway {
	math::Vector facet;
	double tilt;
	double distribution;
};

struct FacingHalf {
	const Facing& facing;
	const Halfway& half;
};

Halfway halfwayAt(const Finish& finish, const Bsdf& bsdf, const Facing& facing) {
	const math::Vector facet = halfwayOf(facing.outgoing, facing.incoming, bsdf.normal);
	const double tilt        = math::dot(bsdf.normal, facet);
	return Halfway {
		.facet        = facet,
		.tilt         = tilt,
		.distribution = facetDistribution(tilt, finish.width),
	};
}

double facetsDensity(const Finish& finish, const FacingHalf& seen) {
	if (math::dot(seen.facing.outgoing, seen.half.facet) <= 0) {
		return 0;
	}
	return seen.half.distribution * smithMasking(seen.facing.leaving, finish.width) / (4 * seen.facing.leaving);
}

double halfDensity(const Finish& finish, const Bsdf& bsdf, const FacingHalf& seen) {
	const double share = facetShareOf(finish, seen.facing.leaving);
	return share * facetsDensity(finish, seen) + (1 - share) * math::cosineDensity(bsdf.normal, seen.facing.incoming);
}

Facing turnedFacing(const Facing& facing) {
	return Facing {
		.outgoing = facing.incoming,
		.incoming = facing.outgoing,
		.leaving  = facing.arriving,
		.arriving = facing.leaving,
	};
}

FilmSides filmOver(double facing, double nanometres) {
	return FilmSides {
		.facing     = facing,
		.nanometres = nanometres,
		.outside    = 1,
		.beneath    = 1,
	};
}

double stackedFilm(const Film& film, const FilmSides& sides, double under) {
	if (!filmed(film)) {
		return under;
	}
	const double front = filmReflectance(film, sides);
	const double held  = std::clamp(under, 0.0, 1.0);
	const double room  = 1 - front * held;
	if (!(room > 1e-9)) {
		return 1;
	}
	return front + (1 - front) * (1 - front) * held / room;
}

double coatFacing(const Film& film, double onto, double nanometres) {
	if (!filmed(film)) {
		return dielectricReflectance(onto, 1 / coatIndex);
	}

	const FilmSides sides = {
		.facing     = onto,
		.nanometres = nanometres,
		.outside    = 1,
		.beneath    = coatIndex,
	};

	return filmReflectance(film, sides);
}

Coating filmCoating(const Finish& finish, double nanometres) {
	const double faced = coatFacing(finish.film, 1, nanometres);
	const double front = faced + (1 - faced) / 21;
	return Coating {
		.front = front,
		.twice = front * front * finish.escape.average / (1 - front * (1 - finish.escape.average)),
		.room  = 1 - front * finish.escape.average,
	};
}

BsdfPair turnedPair(const BsdfPair& pair) {
	return BsdfPair {
		.outgoing    = pair.incoming,
		.incoming    = pair.outgoing,
		.wavelengths = pair.wavelengths,
	};
}

bool roughShadowed(const Facing& facing) {
	return facing.leaving <= 0 || facing.arriving <= 0;
}

BsdfDensities roughDensities(const Finish& finish, const Bsdf& bsdf, const BsdfPair& pair) {
	const Facing facing = facingOf(bsdf, pair);
	if (roughShadowed(facing)) {
		return BsdfDensities {
			.density = roughDensity(finish, bsdf, facing),
			.reverse = roughDensity(finish, bsdf, facingOf(bsdf, turnedPair(pair))),
		};
	}
	const Halfway half = halfwayAt(finish, bsdf, facing);
	const Facing back  = turnedFacing(facing);

	const FacingHalf seen = {
		.facing = facing,
		.half   = half,
	};

	const FacingHalf seenBack = {
		.facing = back,
		.half   = half,
	};

	return BsdfDensities {
		.density = halfDensity(finish, bsdf, seen),
		.reverse = halfDensity(finish, bsdf, seenBack),
	};
}

Spectrum4 metalTwice(const Finish& finish, const Bsdf& bsdf, const Wavelengths& wavelengths) {
	const Spectrum4 average = spectrumOf(averagedFresnel(bsdf.albedo), wavelengths);
	Spectrum4 twice         = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		const double flat  = stackedFilm(finish.film, filmOver(1, wavelengths.nanometres[lane]), average.values[lane]);
		twice.values[lane] = flat * flat * finish.escape.average / (1 - flat * (1 - finish.escape.average));
	}
	return twice;
}

std::array<Coating, spectrumLanes> laneCoats(const Finish& finish, const Wavelengths& wavelengths) {
	std::array<Coating, spectrumLanes> coats = {};
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		coats[std::size_t(lane)] = filmCoating(finish, wavelengths.nanometres[lane]);
	}
	return coats;
}

ViewedBsdf roughViewed(const Finish& finish, const Bsdf& bsdf, const BsdfView& view) {
	const double leaving = math::dot(bsdf.normal, view.outgoing);
	const bool metal     = finish.kind == MaterialKind::Metal;
	const bool coated    = !metal && filmed(finish.film);
	return ViewedBsdf {
		.finish  = finish,
		.bsdf    = bsdf,
		.view    = view,
		.rough   = true,
		.leaving = leaving,
		.left    = escapeAt(finish.escape, leaving),
		.share   = finish.kind == MaterialKind::Plastic && leaving <= 0 ? 0 : facetShareOf(finish, leaving),
		.masking = smithMasking(leaving, finish.width),
		.body    = metal ? flatSpectrum(0) : spectrumOf(bsdf.albedo * (1 / math::pi), view.wavelengths),
		.twice   = metal ? metalTwice(finish, bsdf, view.wavelengths) : flatSpectrum(0),
		.coats   = coated ? laneCoats(finish, view.wavelengths) : std::array<Coating, spectrumLanes> {},
	};
}

double lostOf(const Escape& escape, double left, double came) {
	const double room = 1 - escape.average;
	if (room <= 1e-6) {
		return 0;
	}
	return (1 - left) * (1 - came) / (math::pi * room);
}

Lobe viewedLobe(const ViewedBsdf& viewed, const FacingHalf& seen, double came) {
	const Cosines cosines = {
		.leaving  = seen.facing.leaving,
		.arriving = seen.facing.arriving,
	};

	return Lobe {
		.onto   = std::max(math::dot(seen.facing.outgoing, seen.half.facet), 0.0),
		.tilt   = seen.half.tilt,
		.facets = seen.half.distribution * smithVisibility(cosines, viewed.finish.width),
		.lost   = lostOf(viewed.finish.escape, viewed.left, came),
	};
}

Spectrum4 viewedMetal(const ViewedBsdf& viewed, const Lobe& lobe) {
	const Spectrum4 face = spectrumOf(schlickOf(viewed.bsdf.albedo, lobe.onto), viewed.view.wavelengths);
	Spectrum4 value      = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		const double nanometres = viewed.view.wavelengths.nanometres[lane];
		value.values[lane]      = stackedFilm(viewed.finish.film, filmOver(lobe.onto, nanometres), face.values[lane]) * lobe.facets + viewed.twice.values[lane] * lobe.lost;
	}
	return value;
}

Spectrum4 viewedPlastic(const ViewedBsdf& viewed, const Lobe& lobe, double came) {
	const double left = viewed.left;
	if (!filmed(viewed.finish.film)) {
		const Coating& coat  = viewed.finish.coating;
		const double through = coat.room > 1e-6 ? (1 - coat.front * left) * (1 - coat.front * came) / coat.room : 0;
		const double coated  = dielectricReflectance(lobe.onto, 1 / coatIndex) * lobe.facets + coat.twice * lobe.lost;
		return viewed.body * through + flatSpectrum(coated);
	}
	Spectrum4 value = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		const double nanometres = viewed.view.wavelengths.nanometres[lane];
		const Coating& coat     = viewed.coats[std::size_t(lane)];
		const double through    = coat.room > 1e-6 ? (1 - coat.front * left) * (1 - coat.front * came) / coat.room : 0;
		value.values[lane]      = viewed.body.values[lane] * through + coatFacing(viewed.finish.film, lobe.onto, nanometres) * lobe.facets + coat.twice * lobe.lost;
	}
	return value;
}

double viewedDensity(const ViewedBsdf& viewed, const FacingHalf& seen) {
	const double facets = math::dot(seen.facing.outgoing, seen.half.facet) <= 0 ? 0 : seen.half.distribution * viewed.masking / (4 * seen.facing.leaving);
	return viewed.share * facets + (1 - viewed.share) * math::cosineDensity(viewed.bsdf.normal, seen.facing.incoming);
}

BsdfPair viewedPair(const ViewedBsdf& viewed, const math::Vector& incoming) {
	return BsdfPair {
		.outgoing    = viewed.view.outgoing,
		.incoming    = incoming,
		.wavelengths = viewed.view.wavelengths,
	};
}

BsdfAnswer shadowedViewed(const ViewedBsdf& viewed, const Facing& facing, const ViewedAsk& ask) {
	const BsdfPair turned = {
		.outgoing    = ask.incoming,
		.incoming    = viewed.view.outgoing,
		.wavelengths = viewed.view.wavelengths,
	};

	return BsdfAnswer {
		.value   = flatSpectrum(0),
		.density = roughDensity(viewed.finish, viewed.bsdf, facing),
		.reverse = ask.reverse ? roughDensity(viewed.finish, viewed.bsdf, facingOf(viewed.bsdf, turned)) : 0,
	};
}

Spectrum4 viewedValue(const ViewedBsdf& viewed, const Lobe& lobe, double came) {
	return viewed.finish.kind == MaterialKind::Metal ? viewedMetal(viewed, lobe) : viewedPlastic(viewed, lobe, came);
}

BsdfAnswer roughViewedAnswer(const ViewedBsdf& viewed, const ViewedAsk& ask) {
	const Facing facing = {
		.outgoing = viewed.view.outgoing,
		.incoming = ask.incoming,
		.leaving  = viewed.leaving,
		.arriving = math::dot(viewed.bsdf.normal, ask.incoming),
	};

	if (roughShadowed(facing)) {
		return shadowedViewed(viewed, facing, ask);
	}
	const Halfway half = halfwayAt(viewed.finish, viewed.bsdf, facing);
	const double came  = escapeAt(viewed.finish.escape, facing.arriving);

	const FacingHalf seen = {
		.facing = facing,
		.half   = half,
	};

	const Lobe lobe   = viewedLobe(viewed, seen, came);
	const Facing back = turnedFacing(facing);

	const FacingHalf seenBack = {
		.facing = back,
		.half   = half,
	};

	return BsdfAnswer {
		.value   = viewedValue(viewed, lobe, came),
		.density = viewedDensity(viewed, seen),
		.reverse = ask.reverse ? halfDensity(viewed.finish, viewed.bsdf, seenBack) : 0,
	};
}

Response roughResponse(const Finish& finish, const Bsdf& bsdf, const BsdfPair& pair) {
	const BsdfView view = {
		.outgoing    = pair.outgoing,
		.wavelengths = pair.wavelengths,
	};

	const ViewedAsk ask = {
		.incoming = pair.incoming,
		.reverse  = false,
	};

	const BsdfAnswer answer = roughViewedAnswer(roughViewed(finish, bsdf, view), ask);
	return Response {
		.value   = answer.value,
		.density = answer.density,
	};
}

Spectrum4 roughValue(const Finish& finish, const Bsdf& bsdf, const BsdfPair& pair) {
	return roughResponse(finish, bsdf, pair).value;
}

Indices heroIndices(const Sides& sides) {
	return Indices {
		.near = sides.travelling.values[0],
		.far  = sides.beyond.values[0],
	};
}

Indices laneIndices(const Sides& sides, int lane) {
	return Indices {
		.near = sides.travelling.values[lane],
		.far  = sides.beyond.values[lane],
	};
}

double lossLeft(const Boundary& boundary, const Indices& indices, double cosine) {
	const BoundaryReading reading = {
		.indices = indices,
		.cosine  = cosine,
	};

	return std::max(1 - boundaryAt(boundary, reading), 0.0);
}

Broad broadOf(const Glass& glass, double leaving) {
	const Indices indices = heroIndices(glass.bsdf.sides);
	return Broad {
		.share   = glass.finish.losesMuch ? std::clamp(lossLeft(glass.finish.boundary, indices, leaving), 0.0, 1.0) : 0,
		.through = glass.finish.losesMuch ? lossAt(glass.finish.boundary, indices).share : 0,
	};
}

double laneReflectance(const Glass& glass, double cosine, int lane) {
	const double within = glass.bsdf.sides.travelling.values[lane];
	const double beyond = glass.bsdf.sides.beyond.values[lane];
	if (!filmed(glass.finish.film)) {
		return dielectricReflectance(cosine, within / beyond);
	}

	const FilmSides film = {
		.facing     = cosine,
		.nanometres = glass.wavelengths.nanometres[lane],
		.outside    = within,
		.beneath    = beyond,
	};

	return filmReflectance(glass.finish.film, film);
}

Spectrum4 laneReflectances(const Glass& glass, double cosine) {
	if (!glass.bsdf.sides.parted) {
		return flatSpectrum(laneReflectance(glass, cosine, 0));
	}
	return { { laneReflectance(glass, cosine, 0), laneReflectance(glass, cosine, 1), laneReflectance(glass, cosine, 2), laneReflectance(glass, cosine, 3) } };
}

double reflectChance(const Glass& glass, double cosine) {
	return glass.bsdf.sides.parted ? laneMean(laneReflectances(glass, cosine)) : laneReflectance(glass, cosine, 0);
}

double filmedFromDenser(const Glass& glass, double arriving, int lane) {
	const FilmSides film = {
		.facing     = arriving,
		.nanometres = glass.wavelengths.nanometres[lane],
		.outside    = glass.bsdf.sides.beyond.values[lane],
		.beneath    = glass.bsdf.sides.travelling.values[lane],
	};

	return filmReflectance(glass.finish.film, film);
}

double reflectanceAcross(const Glass& glass, const Cosines& cosines, int lane) {
	const double within = glass.bsdf.sides.travelling.values[lane];
	const double beyond = glass.bsdf.sides.beyond.values[lane];
	if (within <= beyond) {
		return laneReflectance(glass, cosines.leaving, lane);
	}
	return filmed(glass.finish.film) ? filmedFromDenser(glass, cosines.arriving, lane) : dielectricReflectance(cosines.arriving, beyond / within);
}

double backLoss(const Glass& glass, const Indices& indices, const Facing& facing) {
	return lossAt(glass.finish.boundary, indices).back * lossLeft(glass.finish.boundary, indices, facing.leaving) * lossLeft(glass.finish.boundary, indices, facing.arriving) / math::pi;
}

double throughLoss(const Glass& glass, const Indices& indices, const Facing& facing) {
	const Indices turned = {
		.near = indices.far,
		.far  = indices.near,
	};

	return lossAt(glass.finish.boundary, indices).through * lossLeft(glass.finish.boundary, indices, facing.leaving) * lossLeft(glass.finish.boundary, turned, -facing.arriving) / math::pi;
}

Spectrum4 givenBack(const Glass& glass, const Facing& facing, bool through) {
	if (!glass.finish.losesMuch) {
		return flatSpectrum(0);
	}
	const int lanes = glass.bsdf.sides.parted ? spectrumLanes : 1;
	Spectrum4 given = flatSpectrum(0);
	for (int lane = 0; lane < lanes; ++lane) {
		const Indices indices = laneIndices(glass.bsdf.sides, lane);
		given.values[lane]    = through ? throughLoss(glass, indices, facing) : backLoss(glass, indices, facing);
	}
	return glass.bsdf.sides.parted ? given : flatSpectrum(given.values[0]);
}

double reflectionDensity(const Glass& glass, const Facing& facing, const MirrorLobe& mirror) {
	const Broad wide = broadOf(glass, facing.leaving);
	return (1 - wide.share) * mirror.back * mirror.facets + wide.share * (1 - wide.through) * facing.arriving / math::pi;
}

double mirrorFacets(const Glass& glass, const Facing& facing, double lobe) {
	return lobe * smithMasking(facing.leaving, glass.finish.width) / (4 * facing.leaving);
}

double mirrorLobe(const Glass& glass, double onto, const math::Vector& facet) {
	return onto > 0 ? facetDistribution(math::dot(glass.bsdf.normal, facet), glass.finish.width) : 0;
}

Response glassReflection(const Glass& glass, const Facing& facing) {
	const math::Vector facet = halfwayOf(facing.outgoing, facing.incoming, glass.bsdf.normal);
	const double onto        = math::dot(facing.outgoing, facet);
	const Spectrum4 each     = laneReflectances(glass, std::max(onto, 0.0));
	const double lobe        = mirrorLobe(glass, onto, facet);

	const MirrorLobe mirror = {
		.back   = glass.bsdf.sides.parted ? laneMean(each) : each.values[0],
		.facets = mirrorFacets(glass, facing, lobe),
	};

	const Cosines cosines = {
		.leaving  = facing.leaving,
		.arriving = facing.arriving,
	};

	return Response {
		.value   = each * (lobe * smithVisibility(cosines, glass.finish.width)) + givenBack(glass, facing, false),
		.density = reflectionDensity(glass, facing, mirror),
	};
}

double glassReflectionDensity(const Glass& glass, const Facing& facing) {
	const math::Vector facet = halfwayOf(facing.outgoing, facing.incoming, glass.bsdf.normal);
	const double onto        = math::dot(facing.outgoing, facet);

	const MirrorLobe mirror = {
		.back   = reflectChance(glass, std::max(onto, 0.0)),
		.facets = mirrorFacets(glass, facing, mirrorLobe(glass, onto, facet)),
	};

	return reflectionDensity(glass, facing, mirror);
}

Bent noBend() {
	return Bent {
		.stretch = 0,
		.facet   = { 0, 0, 1 },
	};
}

Bent bentFacet(const BentQuery& query) {
	const math::Vector sum = query.facing.outgoing * query.indices.near + query.facing.incoming * query.indices.far;
	const double size      = math::length(sum);
	if (!(size > 1e-12)) {
		return noBend();
	}
	const math::Vector facet = sum * ((math::dot(sum, query.normal) < 0 ? -1 : 1) / size);
	const double into        = math::dot(query.facing.incoming, facet);
	if (math::dot(query.facing.outgoing, facet) <= 0 || into >= 0) {
		return noBend();
	}
	return Bent {
		.stretch = query.indices.far * query.indices.far * -into / (size * size),
		.facet   = facet,
	};
}

struct LanePassage {
	double sent;
	double facets;
};

LanePassage lanePassage(const Glass& glass, const Facing& facing, int lane) {
	const BentQuery query = {
		.facing  = facing,
		.normal  = glass.bsdf.normal,
		.indices = laneIndices(glass.bsdf.sides, lane),
	};

	const Bent bent = bentFacet(query);
	if (bent.stretch <= 0) {
		return LanePassage {
			.sent   = 0,
			.facets = 0,
		};
	}
	const double onto = math::dot(facing.outgoing, bent.facet);
	const double lobe = facetDistribution(math::dot(glass.bsdf.normal, bent.facet), glass.finish.width) * onto * bent.stretch;

	const Cosines visible = {
		.leaving  = facing.leaving,
		.arriving = -facing.arriving,
	};

	const Cosines across = {
		.leaving  = onto,
		.arriving = -math::dot(facing.incoming, bent.facet),
	};

	const double kept = 1 - reflectanceAcross(glass, across, lane);
	return LanePassage {
		.sent   = kept * lobe * 4 * smithVisibility(visible, glass.finish.width),
		.facets = (1 - reflectChance(glass, onto)) * lobe * smithMasking(facing.leaving, glass.finish.width) / facing.leaving,
	};
}

double passageDensity(const Glass& glass, const Facing& facing, double facets) {
	const Broad wide = broadOf(glass, facing.leaving);
	return (1 - wide.share) * facets + wide.share * wide.through * -facing.arriving / math::pi;
}

Response glassPassage(const Glass& glass, const Facing& facing) {
	const int lanes = glass.bsdf.sides.parted ? spectrumLanes : 1;
	Spectrum4 sent  = flatSpectrum(0);
	double facets   = 0;
	for (int lane = 0; lane < lanes; ++lane) {
		const LanePassage passage = lanePassage(glass, facing, lane);
		sent.values[lane]         = passage.sent;
		facets                    = lane == 0 ? passage.facets : facets;
	}
	const Spectrum4 bent = glass.bsdf.sides.parted ? sent : flatSpectrum(sent.values[0]);
	return Response {
		.value   = bent + givenBack(glass, facing, true),
		.density = passageDensity(glass, facing, facets),
	};
}

Response glassResponse(const Finish& finish, const Bsdf& bsdf, const BsdfPair& pair) {
	const Facing facing = facingOf(bsdf, pair);
	if (facing.leaving <= 0 || finish.exact) {
		return noResponse(0);
	}

	const Glass glass = {
		.finish      = finish,
		.bsdf        = bsdf,
		.wavelengths = pair.wavelengths,
	};

	return facing.arriving < 0 ? glassPassage(glass, facing) : glassReflection(glass, facing);
}

double glassDensity(const Finish& finish, const Bsdf& bsdf, const BsdfPair& pair) {
	const Facing facing = facingOf(bsdf, pair);
	if (facing.leaving <= 0 || finish.exact) {
		return 0;
	}

	const Glass glass = {
		.finish      = finish,
		.bsdf        = bsdf,
		.wavelengths = pair.wavelengths,
	};

	return facing.arriving < 0 ? passageDensity(glass, facing, lanePassage(glass, facing, 0).facets) : glassReflectionDensity(glass, facing);
}

constexpr double isotropicLean = 1e-3;

double phaseValue(double lean, double cosine) {
	const double denominator = 1 + lean * lean - 2 * lean * cosine;
	return (1 - lean * lean) / (4 * math::pi * std::pow(std::max(denominator, 0.0), 1.5));
}

double phaseOf(const Finish& finish, const BsdfPair& pair) {
	return phaseValue(finish.lean, math::dot(-pair.outgoing, pair.incoming));
}

Response phaseResponse(const Finish& finish, const BsdfPair& pair) {
	const double value = phaseOf(finish, pair);
	return Response {
		.value   = flatSpectrum(value),
		.density = value,
	};
}

double phaseCosine(double lean, double u) {
	if (!(std::abs(lean) > isotropicLean)) {
		return 1 - 2 * u;
	}
	const double term = (1 - lean * lean) / (1 - lean + 2 * lean * u);
	return std::clamp((1 + lean * lean - term * term) / (2 * lean), -1.0, 1.0);
}

Scatter phaseScatter(const Finish& finish, const BsdfDraw& draw) {
	const math::Vector heading = -draw.outgoing;
	const double cosine        = phaseCosine(finish.lean, draw.numbers.pair.u);
	const double angle         = 2 * math::pi * draw.numbers.pair.v;
	const double reach         = std::sqrt(std::max(1 - cosine * cosine, 0.0));
	const math::Basis basis    = math::basisAround(heading);
	const math::Vector drawn   = basis.tangent * (reach * std::cos(angle)) + basis.bitangent * (reach * std::sin(angle)) + heading * cosine;
	return Scatter {
		.found     = true,
		.incoming  = drawn,
		.weight    = flatSpectrum(1),
		.density   = phaseValue(finish.lean, math::dot(heading, drawn)),
		.sharpness = broadSharpness,
		.through   = false,
	};
}

Response respondBsdf(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair) {
	const Finish& finish = materials.finishes[bsdf.material];
	switch (finish.kind) {
		case MaterialKind::Matte: return matteResponse(bsdf, pair);
		case MaterialKind::Plastic:
		case MaterialKind::Metal: return roughResponse(finish, bsdf, pair);
		case MaterialKind::Glass: return glassResponse(finish, bsdf, pair);
		case MaterialKind::Medium: return phaseResponse(finish, pair);
	}

	return noResponse(0);
}

Spectrum4 bsdfValue(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair) {
	const Finish& finish = materials.finishes[bsdf.material];
	switch (finish.kind) {
		case MaterialKind::Matte: return matteValue(bsdf, pair);
		case MaterialKind::Plastic:
		case MaterialKind::Metal: return roughValue(finish, bsdf, pair);
		case MaterialKind::Glass: return glassResponse(finish, bsdf, pair).value;
		case MaterialKind::Medium: return flatSpectrum(phaseOf(finish, pair));
	}

	return flatSpectrum(0);
}

double bsdfDensity(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair) {
	const Finish& finish = materials.finishes[bsdf.material];
	switch (finish.kind) {
		case MaterialKind::Matte: return math::cosineDensity(bsdf.normal, pair.incoming);
		case MaterialKind::Plastic:
		case MaterialKind::Metal: return roughDensity(finish, bsdf, facingOf(bsdf, pair));
		case MaterialKind::Glass: return glassDensity(finish, bsdf, pair);
		case MaterialKind::Medium: return phaseOf(finish, pair);
	}

	return 0;
}

ViewedBsdf viewedBsdf(const Materials& materials, const Bsdf& bsdf, const BsdfView& view) {
	const Finish& finish = materials.finishes[bsdf.material];
	if (finish.kind == MaterialKind::Plastic || finish.kind == MaterialKind::Metal) {
		return roughViewed(finish, bsdf, view);
	}
	return ViewedBsdf {
		.finish  = finish,
		.bsdf    = bsdf,
		.view    = view,
		.rough   = false,
		.leaving = 0,
		.left    = 0,
		.share   = 0,
		.masking = 0,
		.body    = flatSpectrum(0),
		.twice   = flatSpectrum(0),
		.coats   = {},
	};
}

BsdfAnswer answerViewed(const Materials& materials, const ViewedBsdf& viewed, const ViewedAsk& ask) {
	if (viewed.rough) {
		return roughViewedAnswer(viewed, ask);
	}
	const BsdfPair pair     = viewedPair(viewed, ask.incoming);
	const Response response = respondBsdf(materials, viewed.bsdf, pair);
	return BsdfAnswer {
		.value   = response.value,
		.density = response.density,
		.reverse = ask.reverse ? bsdfDensity(materials, viewed.bsdf, turnedPair(pair)) : 0,
	};
}

BsdfDensities densitiesOf(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair) {
	const Finish& finish = materials.finishes[bsdf.material];
	if (finish.kind == MaterialKind::Plastic || finish.kind == MaterialKind::Metal) {
		return roughDensities(finish, bsdf, pair);
	}
	return BsdfDensities {
		.density = bsdfDensity(materials, bsdf, pair),
		.reverse = bsdfDensity(materials, bsdf, turnedPair(pair)),
	};
}

Scatter scatterOf(const Response& response, const math::Vector& incoming, double cosine) {
	if (response.density <= 0) {
		return noScatter();
	}
	return Scatter {
		.found     = true,
		.incoming  = incoming,
		.weight    = response.value * (std::abs(cosine) / response.density),
		.density   = response.density,
		.sharpness = 1,
		.through   = false,
	};
}

Scatter matteScatter(const Bsdf& bsdf, const BsdfDraw& draw) {
	const math::Vector incoming = math::cosineHemisphere(math::basisAround(bsdf.normal), draw.numbers.pair);
	const double density        = math::cosineDensity(bsdf.normal, incoming);
	if (density <= 0) {
		return noScatter();
	}
	return Scatter {
		.found     = true,
		.incoming  = incoming,
		.weight    = spectrumOf(bsdf.albedo, draw.wavelengths),
		.density   = density,
		.sharpness = broadSharpness,
		.through   = false,
	};
}

FacetDraw facetDraw(const Finish& finish, const Bsdf& bsdf, const BsdfDraw& draw) {
	return FacetDraw {
		.normal   = bsdf.normal,
		.outgoing = draw.outgoing,
		.width    = finish.width,
		.pair     = draw.numbers.pair,
	};
}

Scatter roughScatter(const Finish& finish, const Bsdf& bsdf, const BsdfDraw& draw) {
	const double leaving = math::dot(bsdf.normal, draw.outgoing);
	if (leaving <= 0) {
		return noScatter();
	}
	const bool facetLobe        = draw.numbers.lobe < facetShareOf(finish, leaving);
	const math::Vector facet    = visibleFacet(facetDraw(finish, bsdf, draw));
	const math::Vector incoming = facetLobe ? math::mirrored(draw.outgoing, facet) : math::cosineHemisphere(math::basisAround(bsdf.normal), draw.numbers.pair);
	const double arriving       = math::dot(bsdf.normal, incoming);
	if ((facetLobe && math::dot(draw.outgoing, facet) <= 0) || arriving <= 0) {
		return noScatter();
	}

	const BsdfPair pair = {
		.outgoing    = draw.outgoing,
		.incoming    = incoming,
		.wavelengths = draw.wavelengths,
	};

	Scatter scatter   = scatterOf(roughResponse(finish, bsdf, pair), incoming, arriving);
	scatter.sharpness = facetLobe ? lobeSharpness(finish.width) : broadSharpness;
	return scatter;
}

struct GlassChoice {
	bool found;
	bool reflected;
	math::Vector incoming;
};

GlassChoice notChosen() {
	return GlassChoice {
		.found     = false,
		.reflected = true,
		.incoming  = { 0, 0, 0 },
	};
}

GlassChoice broadChoice(const Glass& glass, const BsdfDraw& draw, const Broad& wide) {
	const bool reflected    = draw.numbers.side >= wide.through;
	const math::Vector side = reflected ? glass.bsdf.normal : -glass.bsdf.normal;
	return GlassChoice {
		.found     = true,
		.reflected = reflected,
		.incoming  = math::cosineHemisphere(math::basisAround(side), draw.numbers.pair),
	};
}

GlassChoice facetChoice(const Glass& glass, const BsdfDraw& draw) {
	const math::Vector facet = visibleFacet(facetDraw(glass.finish, glass.bsdf, draw));
	const double onto        = math::dot(draw.outgoing, facet);
	if (onto <= 0) {
		return notChosen();
	}
	const bool reflected = draw.numbers.side < reflectChance(glass, onto);
	if (reflected) {
		return GlassChoice {
			.found     = true,
			.reflected = true,
			.incoming  = math::mirrored(draw.outgoing, facet),
		};
	}
	const Refraction refraction = refractedThrough(facet, draw.outgoing, glass.bsdf.sides.travelling.values[0] / glass.bsdf.sides.beyond.values[0]);
	return GlassChoice {
		.found     = refraction.passed,
		.reflected = false,
		.incoming  = refraction.direction,
	};
}

Spectrum4 squaredRatios(const Sides& sides) {
	Spectrum4 squared = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		const double ratio   = sides.travelling.values[lane] / sides.beyond.values[lane];
		squared.values[lane] = ratio * ratio;
	}
	return squared;
}

struct ExactSide {
	math::Vector incoming;
	Spectrum4 kept;
	double chance;
	bool through;
};

Scatter exactScatter(const ExactSide& side, const BsdfDraw& draw, const Sides& sides) {
	const bool carried     = draw.travel == Travel::FromLight && side.through;
	const Spectrum4 weight = side.kept / side.chance;
	return Scatter {
		.found     = true,
		.incoming  = side.incoming,
		.weight    = carried ? weight * squaredRatios(sides) : weight,
		.density   = 0,
		.sharpness = 0,
		.through   = side.through,
	};
}

Scatter exactGlassScatter(const Glass& glass, const BsdfDraw& draw) {
	const Bsdf& bsdf     = glass.bsdf;
	const double leaving = math::dot(bsdf.normal, draw.outgoing);
	if (leaving <= 0) {
		return noScatter();
	}
	const Sides& sides          = bsdf.sides;
	const Spectrum4 reflected   = laneReflectances(glass, leaving);
	const double chance         = reflectChance(glass, leaving);
	const Refraction refraction = refractedThrough(bsdf.normal, draw.outgoing, sides.travelling.values[0] / sides.beyond.values[0]);
	if (draw.numbers.side < chance || !refraction.passed) {
		const ExactSide mirror = {
			.incoming = math::mirrored(draw.outgoing, bsdf.normal),
			.kept     = reflected,
			.chance   = refraction.passed ? chance : 1,
			.through  = false,
		};

		return exactScatter(mirror, draw, sides);
	}

	const ExactSide passage = {
		.incoming = refraction.direction,
		.kept     = flatSpectrum(1) - reflected,
		.chance   = 1 - chance,
		.through  = true,
	};

	return exactScatter(passage, draw, sides);
}

Scatter glassScatter(const Finish& finish, const Bsdf& bsdf, const BsdfDraw& draw) {
	const Glass glass = {
		.finish      = finish,
		.bsdf        = bsdf,
		.wavelengths = draw.wavelengths,
	};

	if (finish.exact) {
		return exactGlassScatter(glass, draw);
	}
	const double leaving = math::dot(bsdf.normal, draw.outgoing);
	if (leaving <= 0) {
		return noScatter();
	}

	const Broad wide         = broadOf(glass, leaving);
	const GlassChoice chosen = draw.numbers.lobe < wide.share ? broadChoice(glass, draw, wide) : facetChoice(glass, draw);
	const double arriving    = math::dot(bsdf.normal, chosen.incoming);
	if (!chosen.found || (chosen.reflected ? arriving <= 0 : arriving >= 0)) {
		return noScatter();
	}

	const Facing facing = {
		.outgoing = draw.outgoing,
		.incoming = chosen.incoming,
		.leaving  = leaving,
		.arriving = arriving,
	};

	const Response response = chosen.reflected ? glassReflection(glass, facing) : glassPassage(glass, facing);
	Scatter scatter         = scatterOf(response, chosen.incoming, arriving);
	const bool carried      = draw.travel == Travel::FromLight && !chosen.reflected;
	scatter.weight          = carried ? scatter.weight * squaredRatios(bsdf.sides) : scatter.weight;
	scatter.sharpness       = finish.sharpness;
	scatter.through         = !chosen.reflected;
	return scatter;
}

Scatter sampleBsdf(const Materials& materials, const Bsdf& bsdf, const BsdfDraw& draw) {
	const Finish& finish = materials.finishes[bsdf.material];
	switch (finish.kind) {
		case MaterialKind::Matte: return matteScatter(bsdf, draw);
		case MaterialKind::Plastic:
		case MaterialKind::Metal: return roughScatter(finish, bsdf, draw);
		case MaterialKind::Glass: return glassScatter(finish, bsdf, draw);
		case MaterialKind::Medium: return phaseScatter(finish, draw);
	}

	return noScatter();
}

Bsdf turnedBsdf(const Bsdf& bsdf) {
	const Sides sides = {
		.travelling = bsdf.sides.beyond,
		.beyond     = bsdf.sides.travelling,
		.parted     = bsdf.sides.parted,
		.inside     = !bsdf.sides.inside,
	};

	return Bsdf {
		.material = bsdf.material,
		.albedo   = bsdf.albedo,
		.normal   = -bsdf.normal,
		.sides    = sides,
	};
}

bool isSharp(const Materials& materials, std::uint32_t material) {
	return materials.finishes[material].sharp;
}

bool isPassable(const Materials& materials, std::uint32_t material) {
	return materials.finishes[material].kind == MaterialKind::Glass;
}

bool isExact(const Materials& materials, std::uint32_t material) {
	return materials.finishes[material].exact;
}

}
