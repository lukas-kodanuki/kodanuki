#include "plugins/sampler/highlight_guide/survey.h"

#include "plugins/sampler/highlight_guide/chain.h"
#include "sdf/numbers.h"
#include "shared/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace kodanuki {

constexpr int surveyStep             = 8;
constexpr int surveyProbes           = 128;
constexpr int pixelDraws             = 8;
constexpr std::uint32_t mostSeeds    = 16;
constexpr int seedsPerLeaf           = 3;
constexpr std::size_t mostHighlights = 48;
constexpr double shinyWidth          = 0.125;
constexpr int refinedLumps           = 2;
constexpr double groundedField       = 1e-4;
constexpr double guideEfficiency     = 0.3;
constexpr double filingCell          = 0.3;
constexpr double sameSpot            = 0.05;
constexpr std::uint64_t surveyStir   = 0x6A09E667F3BCC909ull;
constexpr std::uint32_t surveyNoSlot = 0xFFFFFFFFu;

struct SurveySurface {
	std::uint32_t material;
	math::Vector normal;
	Bsdf bsdf;
};

struct SurveyStand {
	bool found;
	SurveySurface seen;
	math::Vector outgoing;
	math::Vector origin;
};

struct SurveyLobe {
	double sharpness;
	int wide;
	int count;
	bool through;
	std::array<math::Vector, surveyProbes> kept;
};

struct SurveyField {
	double value;
	math::Vector gradient;
	std::uint32_t leaf;
};

struct LampOutline {
	bool seen;
	math::Vector towards;
	double size;
};

struct SurveySeed {
	math::Vector direction;
	std::uint32_t material;
	std::uint32_t leaf;
	math::Vector normal;
	double width;
	math::Vector point;
};

struct SpotSource {
	const Program& leaf;
	std::uint32_t index;
	double width;
	std::uint32_t light;
};

struct SurveyCandidate {
	Lump lump;
	double radiance;
	const Highlight* source;
	double blend;
};

struct PixelSpot {
	int x;
	int y;
};

SurveySurface surveySurface(const Tracing& tracing, const Hit& hit, const math::Vector& heading) {
	std::vector<double> registers;

	const Shading shading = {
		.scene     = tracing.scene,
		.looks     = tracing.looks,
		.materials = tracing.materials,
	};

	const ShadingQuery query = {
		.hit       = hit,
		.heading   = heading,
		.medium    = noMaterial,
		.registers = registers,
	};

	const Bsdf bsdf = bsdfAt(shading, query).bsdf;
	return SurveySurface {
		.material = bsdf.material,
		.normal   = math::facingAgainst(bsdf.normal, heading),
		.bsdf     = bsdf,
	};
}

SurveyStand noStand() {
	const math::Vector none = { 0, 0, 0 };

	const Bsdf bsdf = {
		.material = noMaterial,
		.albedo   = { 0, 0, 0 },
		.normal   = none,
		.sides    = {},
	};

	const SurveySurface surface = {
		.material = noMaterial,
		.normal   = none,
		.bsdf     = bsdf,
	};

	return SurveyStand {
		.found    = false,
		.seen     = surface,
		.outgoing = none,
		.origin   = none,
	};
}

SurveyStand standAt(const Tracing& tracing, const PixelSpot& spot) {
	const Ray ray = pixelRay(tracing.lens, spot.x, spot.y);
	const Hit hit = intersect(tracing.geometry, ray);
	if (hit.kind == HitKind::Missed) {
		return noStand();
	}
	const math::Vector heading = math::normalized(ray.direction);
	const SurveySurface seen   = surveySurface(tracing, hit, heading);
	const double lift          = tracing.limits.bias + 2 * hit.loosened;
	if (!tracing.looks.materials[seen.material].guided) {
		return noStand();
	}
	return SurveyStand {
		.found    = true,
		.seen     = seen,
		.outgoing = -heading,
		.origin   = hit.point + seen.normal * lift,
	};
}

std::uint64_t surveyStream(const Tracing& tracing, const PixelSpot& spot) {
	const std::uint64_t place = (std::uint64_t(std::uint32_t(spot.x)) << 32) | std::uint64_t(std::uint32_t(spot.y));
	return math::mixedBits(tracing.seed ^ surveyStir) ^ math::mixedBits(place);
}

double surveyNumber(std::uint64_t stream, int draw, int role) {
	const math::Key key = {
		.stream     = stream,
		.index      = std::uint64_t(draw),
		.coordinate = std::uint64_t(role),
	};

	return math::hashedNumber(key);
}

struct LobeAsk {
	const Tracing& tracing;
	const SurveyStand& stand;
	std::uint64_t stream;
};

Scatter surveyDraw(const LobeAsk& ask, int draw) {
	const Wavelengths wavelengths = spreadWavelengths(0.5);

	const LobeNumbers numbers = {
		.pair = { surveyNumber(ask.stream, draw, 0), surveyNumber(ask.stream, draw, 1) },
		.lobe = surveyNumber(ask.stream, draw, 2),
		.side = surveyNumber(ask.stream, draw, 3),
	};

	const BsdfDraw asked = {
		.outgoing    = ask.stand.outgoing,
		.travel      = Travel::FromEye,
		.numbers     = numbers,
		.wavelengths = wavelengths,
	};

	return sampleBsdf(ask.tracing.materials, ask.stand.seen.bsdf, asked);
}

SurveyLobe sharpestLobe(const LobeAsk& ask, int draws, bool keep) {
	SurveyLobe lobe = {
		.sharpness = 1,
		.wide      = 0,
		.count     = 0,
		.through   = false,
		.kept      = {},
	};

	for (int draw = 0; draw < draws; ++draw) {
		const Scatter drawn = surveyDraw(ask, draw);
		if (drawn.found && drawn.through) {
			lobe.through = true;
			return lobe;
		}
		if (!drawn.found || !(drawn.density > 0)) {
			continue;
		}
		lobe.sharpness = drawn.sharpness < 1 ? std::min(lobe.sharpness, drawn.sharpness) : lobe.sharpness;
		lobe.wide += drawn.sharpness < 1 ? 0 : 1;
		if (keep) {
			lobe.kept[std::size_t(lobe.count)] = drawn.incoming;
			lobe.count += 1;
		}
	}
	return lobe;
}

math::Dual objectDual(const Program& program, const math::Vector& point, std::vector<math::Dual>& registers) {
	const math::Dual zero = math::constantAs<math::Dual>(0);

	const Inputs<math::Dual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::Dual>(point.x, 0), math::coordinateAs<math::Dual>(point.y, 1), math::coordinateAs<math::Dual>(point.z, 2) },
		.n         = { zero, zero, zero },
	};

	return evaluate(program, inputs, registers);
}

double objectValue(const Program& program, const math::Vector& point, std::vector<double>& registers) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { point.x, point.y, point.z },
		.n         = { 0, 0, 0 },
	};

	return evaluate(program, inputs, registers);
}

std::size_t nearestObject(const Scene& scene, const math::Vector& point) {
	thread_local std::vector<double> registers;
	std::size_t nearest = scene.objectPrograms.size();
	double least        = std::numeric_limits<double>::infinity();
	for (std::size_t k = 0; k < scene.objectPrograms.size(); ++k) {
		const double value = objectValue(scene.objectPrograms[k], point, registers);
		nearest            = value < least ? k : nearest;
		least              = std::min(least, value);
	}
	return nearest;
}

SurveyField surveyFieldAt(const Scene& scene, const math::Vector& point) {
	thread_local std::vector<math::Dual> registers;
	const std::size_t nearest = nearestObject(scene, point);
	if (nearest == scene.objectPrograms.size()) {
		return SurveyField {
			.value    = std::numeric_limits<double>::infinity(),
			.gradient = { 0, 0, 0 },
			.leaf     = noLeaf,
		};
	}

	const Program& program = scene.objectPrograms[nearest];
	const math::Dual value = objectDual(program, point, registers);
	return SurveyField {
		.value    = value.value,
		.gradient = { value.gradient[0], value.gradient[1], value.gradient[2] },
		.leaf     = sourceAt(program, point).leaf,
	};
}

LampOutline outlineToward(const math::Vector& to, bool seen, double size) {
	const double distance = math::length(to);
	return LampOutline {
		.seen    = seen,
		.towards = to * (1 / distance),
		.size    = size,
	};
}

LampOutline lampOutline(const Lamp& lamp, const math::Vector& from) {
	const math::Vector middle = lamp.kind == LightKind::Quad ? lamp.at + (lamp.along + lamp.across) * 0.5 : lamp.at;
	const math::Vector to     = middle - from;
	const double distance     = math::length(to);
	switch (lamp.kind) {
		case LightKind::Point: return outlineToward(to, true, 0);
		case LightKind::Sphere: return outlineToward(to, distance > lamp.radius, std::asin(std::min(lamp.radius / std::max(distance, 1e-9), 1.0)));
		case LightKind::Quad: return outlineToward(to, distance > 1e-9, std::sqrt(lamp.area / math::pi) / std::max(distance, 1e-9));
		case LightKind::Directional: return outlineToward(lamp.direction, true, 0);
		case LightKind::Moon:
		case LightKind::Sun: return outlineToward(lamp.direction, true, std::acos(std::clamp(lamp.cosine, -1.0, 1.0)));
	}

	return outlineToward(to, false, 0);
}

bool groundedOn(const Tracing& tracing, const SurveyStand& stand, const SpotSource& source, const math::Vector& point) {
	const SurveyField there = surveyFieldAt(tracing.scene, point);
	const bool facing       = math::dot(there.gradient, stand.origin - point) > 0;
	const bool lit          = lampOutline(tracing.lights.lamps[source.light], point).seen;
	return there.leaf == source.index && std::abs(there.value) < groundedField && facing && lit;
}

SettledChain walkedFrom(const Tracing& tracing, const SurveyStand& stand, const SpotSource& source, const math::Vector& seed) {
	const math::Basis basis = math::basisAround(stand.seen.normal);

	const BounceChain chain = {
		.from     = stand.origin,
		.sideways = { basis.tangent, basis.bitangent },
		.leaf     = source.leaf,
		.width    = source.width,
		.lamp     = tracing.lights.lamps[source.light],
	};

	SettledChain settled = walkedChain(chain, seed);
	settled.found        = settled.found && groundedOn(tracing, stand, source, settled.point);
	return settled;
}

Lump shapedLump(const Lump& lump, const std::array<double, 3>& spread) {
	const double det     = spread[0] * spread[2] - spread[1] * spread[1];
	const double factor0 = std::sqrt(spread[0]);
	const double factor1 = spread[1] / factor0;
	const double factor2 = std::sqrt(std::max(spread[2] - factor1 * factor1, 1e-30));
	Lump shaped          = lump;
	shaped.precision     = { float(spread[2] / det), float(-spread[1] / det), float(spread[0] / det) };
	shaped.factor        = { float(factor0), float(factor1), float(factor2) };
	shaped.height        = 1 / (2 * math::pi * std::sqrt(det));
	return shaped;
}

Lump bareLump(const math::Vector& axis, const math::Vector& across, const math::Vector& point) {
	return Lump {
		.axis      = axis,
		.across    = across,
		.up        = math::cross(axis, across),
		.mean      = { 0, 0 },
		.precision = { 0, 0, 0 },
		.factor    = { 0, 0, 0 },
		.height    = 0,
		.weight    = 0,
		.point     = point,
	};
}

Lump chainLump(const SettledChain& settled) {
	Lump lump = bareLump(settled.axis, settled.across, settled.point);
	lump.up   = settled.up;
	return shapedLump(lump, settled.spread);
}

math::Vector lumpMiddle(const Lump& lump) {
	return math::normalized(lump.axis + lump.across * lump.mean[0] + lump.up * lump.mean[1]);
}

struct LobeMirror {
	math::Vector mirror;
	double width;
};

Lump narrowedLump(const Lump& lump, const LobeMirror& lobe) {
	const double along = math::dot(lobe.mirror, lump.axis);
	if (!(along > 0)) {
		return lump;
	}
	const double centre[2] = { math::dot(lobe.mirror, lump.across) / along, math::dot(lobe.mirror, lump.up) / along };
	const double own       = 1 / (lobe.width * lobe.width);
	const double p0        = lump.precision[0];
	const double p1        = lump.precision[1];
	const double p2        = lump.precision[2];
	const double inverse   = 1 / (p0 * p2 - p1 * p1);
	const double c00       = inverse * p2;
	const double c01       = -inverse * p1;
	const double c11       = inverse * p0;
	const double du        = lump.mean[0] - centre[0];
	const double dv        = lump.mean[1] - centre[1];
	const bool inside      = du * du + dv * dv <= lobe.width * lobe.width;
	const bool wider       = std::sqrt(c00 * c11 - c01 * c01) > lobe.width * lobe.width;
	if (!inside || !wider) {
		return lump;
	}
	const double q0                    = p0 + own;
	const double q1                    = p1;
	const double q2                    = p2 + own;
	const double qdet                  = q0 * q2 - q1 * q1;
	const double r0                    = p0 * lump.mean[0] + p1 * lump.mean[1] + own * centre[0];
	const double r1                    = p1 * lump.mean[0] + p2 * lump.mean[1] + own * centre[1];
	Lump narrowed                      = lump;
	narrowed.mean                      = { float((q2 * r0 - q1 * r1) / qdet), float((-q1 * r0 + q0 * r1) / qdet) };
	const std::array<double, 3> spread = { q2 / qdet, -q1 / qdet, q0 / qdet };
	return shapedLump(narrowed, spread);
}

bool finiteLump(const Lump& lump) {
	const double values[] = { lump.mean[0], lump.mean[1], lump.precision[0], lump.precision[1], lump.precision[2], lump.factor[0], lump.factor[1], lump.factor[2], lump.height, lump.weight, lump.axis.x, lump.axis.y, lump.axis.z, lump.across.x, lump.across.y, lump.across.z, lump.up.x, lump.up.y, lump.up.z, lump.point.x, lump.point.y, lump.point.z };
	return std::all_of(std::begin(values), std::end(values), [](double value) {
		return std::isfinite(value);
	});
}

bool soundLump(const Lump& lump) {
	const double det = lump.precision[0] * lump.precision[2] - lump.precision[1] * lump.precision[1];
	const bool sized = lump.factor[0] > 1e-9 && lump.factor[2] > 1e-9 && lump.factor[0] < 1 && lump.factor[2] < 1;
	return finiteLump(lump) && lump.precision[0] > 0 && lump.precision[2] > 0 && det > 0 && sized && lump.weight > 0;
}

double standDensity(const Tracing& tracing, const SurveyStand& stand, const math::Vector& incoming) {
	const Wavelengths wavelengths = spreadWavelengths(0.5);

	const BsdfPair pair = {
		.outgoing    = stand.outgoing,
		.incoming    = incoming,
		.wavelengths = wavelengths,
	};

	return bsdfDensity(tracing.materials, stand.seen.bsdf, pair);
}

double weighedLump(const Tracing& tracing, const SurveyStand& stand, const Highlight& spot, const Lump& lump) {
	const double reach = standDensity(tracing, stand, lumpMiddle(lump));
	return lampGlare(tracing.lights.lamps[spot.light]) * lump.factor[0] * lump.factor[2] * reach;
}

struct HighlightNote {
	const SurveySeed& seed;
	const SurveyStand& stand;
	bool broad;
	std::uint32_t light;
};

bool knownSpot(const std::vector<Highlight>& spots, const HighlightNote& note, const math::Vector& point) {
	return std::any_of(spots.begin(), spots.end(), [&](const Highlight& known) {
		return known.leaf == note.seed.leaf && known.light == note.light && math::length(known.point - point) < sameSpot;
	});
}

Highlight highlightOf(const HighlightNote& note, const SettledChain& settled) {
	const math::Basis basis = math::basisAround(note.stand.seen.normal);
	return Highlight {
		.material   = note.seed.material,
		.leaf       = note.seed.leaf,
		.light      = note.light,
		.point      = settled.point,
		.normal     = note.seed.normal,
		.width      = note.seed.width,
		.across     = settled.across,
		.up         = settled.up,
		.spread     = settled.spread,
		.distance   = math::length(settled.point - note.stand.origin),
		.origin     = note.stand.origin,
		.sideways   = { basis.tangent, basis.bitangent },
		.motion     = settled.motion,
		.seen       = note.stand.seen.material,
		.broad      = note.broad,
		.seenNormal = note.stand.seen.normal,
	};
}

void notedHighlight(std::vector<Highlight>& spots, const HighlightNote& note, const SettledChain& settled) {
	if (knownSpot(spots, note, settled.point) || spots.size() >= mostHighlights) {
		return;
	}
	const Highlight made = highlightOf(note, settled);
	if (std::isfinite(made.distance) && made.spread[0] > 0 && made.spread[2] > 0) {
		spots.push_back(made);
	}
}

Ray probeRay(const math::Vector& origin, const math::Vector& direction) {
	return Ray {
		.origin    = origin,
		.direction = direction,
		.far       = std::numeric_limits<float>::infinity(),
		.inside    = false,
		.within    = noMaterial,
	};
}

bool seedTaken(const std::vector<SurveySeed>& seeds, const Hit& hit, const math::Vector& direction) {
	int same   = 0;
	bool close = false;
	for (const SurveySeed& seed : seeds) {
		if (seed.leaf == hit.leaf) {
			same += 1;
			close = close || math::dot(seed.direction, direction) > std::cos(sameSpot);
		}
	}
	return same >= seedsPerLeaf || close;
}

bool shinyFinish(const Tracing& tracing, std::uint32_t material) {
	const Finish& finish = tracing.materials.finishes[material];
	return !isPassable(tracing.materials, material) && isSharp(tracing.materials, material) && finish.width <= shinyWidth;
}

std::vector<SurveySeed> surveySeeds(const Tracing& tracing, const SurveyStand& stand, const SurveyLobe& lobe) {
	std::vector<SurveySeed> seeds;
	for (int k = 0; k < lobe.count && seeds.size() < mostSeeds; ++k) {
		const math::Vector direction = lobe.kept[std::size_t(k)];
		const Hit hit                = intersect(tracing.geometry, probeRay(stand.origin, direction));
		if (hit.kind == HitKind::Missed || seedTaken(seeds, hit, direction)) {
			continue;
		}
		const SurveySurface there = surveySurface(tracing, hit, direction);
		if (!shinyFinish(tracing, there.material)) {
			continue;
		}

		const SurveySeed seed = {
			.direction = direction,
			.material  = there.material,
			.leaf      = hit.leaf,
			.normal    = there.normal,
			.width     = tracing.materials.finishes[there.material].width,
			.point     = hit.point,
		};

		seeds.push_back(seed);
	}
	return seeds;
}

void seedHighlights(const Tracing& tracing, const HighlightNote& base, std::vector<Highlight>& spots) {
	const Program leaf = leafProgram(tracing.scene, base.seed.leaf);
	for (std::uint32_t light = 0; light < tracing.lights.lamps.size(); ++light) {
		const SpotSource source = {
			.leaf  = leaf,
			.index = base.seed.leaf,
			.width = base.seed.width,
			.light = light,
		};

		const SettledChain settled = walkedFrom(tracing, base.stand, source, base.seed.point);
		if (!settled.found) {
			continue;
		}
		HighlightNote note = base;
		note.light         = light;
		notedHighlight(spots, note, settled);
	}
}

std::vector<Highlight> searchedPoint(const Tracing& tracing, const PixelSpot& spot) {
	const SurveyStand stand = standAt(tracing, spot);
	if (!stand.found) {
		return {};
	}

	const LobeAsk ask = {
		.tracing = tracing,
		.stand   = stand,
		.stream  = surveyStream(tracing, spot),
	};

	const SurveyLobe lobe = sharpestLobe(ask, surveyProbes, true);
	if (lobe.through || lobe.count == 0) {
		return {};
	}
	const bool broad = lobe.wide * 4 >= lobe.count;
	std::vector<Highlight> spots;
	for (const SurveySeed& seed : surveySeeds(tracing, stand, lobe)) {
		const HighlightNote note = {
			.seed  = seed,
			.stand = stand,
			.broad = broad,
			.light = 0,
		};

		seedHighlights(tracing, note, spots);
	}
	return spots;
}

double spotImportance(const Lights& lights, const Highlight& spot) {
	const double det = spot.spread[0] * spot.spread[2] - spot.spread[1] * spot.spread[1];
	return lampGlare(lights.lamps[spot.light]) * std::sqrt(std::max(det, 0.0));
}

void sortedSpots(const Lights& lights, std::vector<Highlight>& spots) {
	std::stable_sort(spots.begin(), spots.end(), [&](const Highlight& a, const Highlight& b) {
		return spotImportance(lights, a) > spotImportance(lights, b);
	});
}

void filedSurvey(HighlightSurvey& survey) {
	for (std::uint32_t index = 0; index < survey.spots.size(); ++index) {
		const std::vector<Highlight>& spots = survey.spots[index];
		if (spots.empty() || !spots.front().broad) {
			continue;
		}
		for (int k = 0; k < 27; ++k) {
			const std::array<int, 3> shift = { k % 3 - 1, k / 3 % 3 - 1, k / 9 - 1 };
			survey.filed[filedKey(spots.front().origin, filingCell, shift)].push_back(index);
		}
	}
}

PixelSpot surveyPoint(const Lens& lens, const HighlightSurvey& survey, std::size_t index) {
	return PixelSpot {
		.x = std::min(int(index) % survey.across * surveyStep + surveyStep / 2, lens.width - 1),
		.y = std::min(int(index) / survey.across * surveyStep + surveyStep / 2, lens.height - 1),
	};
}

HighlightSurvey surveyOf(const GuideFrame& frame) {
	const Tracing& tracing = frame.tracing;
	HighlightSurvey survey = noHighlightGuides().survey;
	if (tracing.lights.lamps.empty()) {
		return survey;
	}
	survey.across = (tracing.lens.width + surveyStep - 1) / surveyStep;
	survey.down   = (tracing.lens.height + surveyStep - 1) / surveyStep;
	survey.spots.assign(std::size_t(survey.across) * std::size_t(survey.down), {});
	inParallel(frame.threads, survey.spots.size(), [&](std::size_t k) {
		survey.spots[k] = searchedPoint(tracing, surveyPoint(tracing.lens, survey, k));
		sortedSpots(tracing.lights, survey.spots[k]);
	});
	filedSurvey(survey);
	return survey;
}

struct CarryAsk {
	const Tracing& tracing;
	const HighlightSurvey& survey;
	const SurveyStand& stand;
	const LobeMirror& lobe;
	bool broad;
};

struct BlendCell {
	int left;
	int top;
	double fx;
	double fy;
};

BlendCell blendCellOf(const HighlightSurvey& survey, const PixelSpot& pixel) {
	const int left = std::clamp((pixel.x - surveyStep / 2) / surveyStep, 0, survey.across - 1);
	const int top  = std::clamp((pixel.y - surveyStep / 2) / surveyStep, 0, survey.down - 1);
	return BlendCell {
		.left = left,
		.top  = top,
		.fx   = std::clamp((pixel.x - (left * surveyStep + surveyStep / 2)) / double(surveyStep), 0.0, 1.0),
		.fy   = std::clamp((pixel.y - (top * surveyStep + surveyStep / 2)) / double(surveyStep), 0.0, 1.0),
	};
}

double blendOf(const HighlightSurvey& survey, const BlendCell& cell, const PixelSpot& at) {
	const bool oneColumn = cell.left + 1 > survey.across - 1;
	const bool oneRow    = cell.top + 1 > survey.down - 1;
	const double across  = oneColumn ? 1 : (at.x == cell.left ? 1 - cell.fx : cell.fx);
	const double down    = oneRow ? 1 : (at.y == cell.top ? 1 - cell.fy : cell.fy);
	return across * down;
}

struct UsedSpots {
	std::vector<const Highlight*> spots;
	std::vector<std::uint32_t> candidates;
};

std::uint32_t knownUse(const UsedSpots& used, const Highlight& spot) {
	for (std::uint32_t k = 0; k < used.spots.size(); ++k) {
		const Highlight& known = *used.spots[k];
		if (known.leaf == spot.leaf && known.light == spot.light && math::length(known.point - spot.point) < sameSpot) {
			return k;
		}
	}
	return surveyNoSlot;
}

struct CarriedLump {
	bool found;
	Lump lump;
};

CarriedLump carriedLump(const CarryAsk& ask, const Highlight& spot) {
	const SurveyStand& stand   = ask.stand;
	const math::Vector away    = stand.origin - spot.origin;
	const math::Vector point   = spot.point + spot.motion[0] * math::dot(away, spot.sideways[0]) + spot.motion[1] * math::dot(away, spot.sideways[1]);
	const math::Vector towards = point - stand.origin;
	const double distance      = math::length(towards);

	const CarriedLump none = {
		.found = false,
		.lump  = bareLump(stand.seen.normal, stand.seen.normal, point),
	};

	if (!(distance > 1e-6) || math::dot(towards, stand.seen.normal) <= 0) {
		return none;
	}
	const double scale                 = spot.distance * spot.distance / (distance * distance);
	const std::array<double, 3> spread = { spot.spread[0] * scale, spot.spread[1] * scale, spot.spread[2] * scale };
	const math::Vector axis            = towards * (1 / distance);
	const math::Vector across          = math::normalized(spot.across - axis * math::dot(spot.across, axis));
	const Lump shaped                  = shapedLump(bareLump(axis, across, point), spread);
	Lump lump                          = ask.broad ? shaped : narrowedLump(shaped, ask.lobe);
	lump.weight                        = weighedLump(ask.tracing, stand, spot, lump);
	return CarriedLump {
		.found = soundLump(lump),
		.lump  = lump,
	};
}

void carriedSpot(const CarryAsk& ask, const Highlight& spot, double blend, UsedSpots& used, std::vector<SurveyCandidate>& found) {
	const std::uint32_t known = knownUse(used, spot);
	if (known != surveyNoSlot) {
		if (used.candidates[known] != surveyNoSlot) {
			found[used.candidates[known]].blend += blend;
		}
		return;
	}
	used.spots.push_back(&spot);
	used.candidates.push_back(surveyNoSlot);
	if (spot.seen != ask.stand.seen.material) {
		return;
	}
	const CarriedLump carried = carriedLump(ask, spot);
	if (!carried.found) {
		return;
	}
	Lump lump              = carried.lump;
	lump.weight            = lump.weight * (1 + 1e-12 * double(found.size()));
	used.candidates.back() = std::uint32_t(found.size());

	const SurveyCandidate candidate = {
		.lump     = lump,
		.radiance = lampGlare(ask.tracing.lights.lamps[spot.light]),
		.source   = &spot,
		.blend    = blend,
	};

	found.push_back(candidate);
}

std::vector<SurveyCandidate> blendedCandidates(std::vector<SurveyCandidate>& found) {
	std::size_t kept = 0;
	for (SurveyCandidate& one : found) {
		one.lump.weight *= one.blend;
		one.radiance *= one.blend;
		if (one.lump.weight > 0 && std::isfinite(one.lump.weight)) {
			found[kept] = one;
			kept += 1;
		}
	}
	found.resize(kept);
	return found;
}

std::vector<SurveyCandidate> carriedCandidates(const CarryAsk& ask, const PixelSpot& pixel) {
	const HighlightSurvey& survey = ask.survey;
	const BlendCell cell          = blendCellOf(survey, pixel);
	std::vector<SurveyCandidate> found;

	UsedSpots used = {
		.spots      = {},
		.candidates = {},
	};

	for (int row = cell.top; row <= std::min(cell.top + 1, survey.down - 1); ++row) {
		for (int column = cell.left; column <= std::min(cell.left + 1, survey.across - 1); ++column) {
			const PixelSpot at = {
				.x = column,
				.y = row,
			};

			const double blend = blendOf(survey, cell, at);
			for (const Highlight& spot : survey.spots[std::size_t(row) * std::size_t(survey.across) + std::size_t(column)]) {
				carriedSpot(ask, spot, blend, used, found);
			}
		}
	}
	return blendedCandidates(found);
}

double respondedToward(const Tracing& tracing, const SurveyStand& stand, const math::Vector& towards) {
	const double facing = math::dot(stand.seen.normal, towards);
	if (facing <= 0) {
		return 0;
	}
	const Wavelengths wavelengths = spreadWavelengths(0.5);

	const BsdfPair pair = {
		.outgoing    = stand.outgoing,
		.incoming    = towards,
		.wavelengths = wavelengths,
	};

	return laneMean(bsdfValue(tracing.materials, stand.seen.bsdf, pair)) * facing;
}

struct ShareAsk {
	const GuideFrame& frame;
	const SurveyStand& stand;
	const std::array<double, mostLumps>& radiance;
};

double directLight(const Tracing& tracing, const SurveyStand& stand) {
	double rest = 0;
	for (const Lamp& lamp : tracing.lights.lamps) {
		const LampOutline towards = lampOutline(lamp, stand.origin);
		if (!towards.seen) {
			continue;
		}
		const double size = std::max(towards.size, 1e-3);
		rest += respondedToward(tracing, stand, towards.towards) * math::pi * size * size * lampGlare(lamp);
	}
	return rest;
}

double guideShare(const ShareAsk& ask, const Guide& guide) {
	double spots = 0;
	for (std::uint32_t k = 0; k < guide.count; ++k) {
		const Lump& lump = guide.lumps[k];
		spots += respondedToward(ask.frame.tracing, ask.stand, lumpMiddle(lump)) * 2 * math::pi * lump.factor[0] * lump.factor[2] * ask.radiance[k];
	}
	const double rest    = directLight(ask.frame.tracing, ask.stand);
	const double spotted = spots / std::sqrt(guideEfficiency);
	const double share   = spotted / std::max(spotted + rest, 1e-30);
	return std::min(std::max(share, ask.frame.tunings.leastShare), ask.frame.tunings.mostShare);
}

std::vector<std::uint32_t> heaviestFirst(const std::vector<SurveyCandidate>& found) {
	std::vector<std::uint32_t> order(found.size());
	for (std::uint32_t k = 0; k < order.size(); ++k) {
		order[k] = k;
	}
	std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
		return found[a].lump.weight > found[b].lump.weight;
	});

	return order;
}

void refinedCandidate(const CarryAsk& ask, SurveyCandidate& one) {
	const Highlight& spot = *one.source;
	const Program leaf    = leafProgram(ask.tracing.scene, spot.leaf);

	const SpotSource source = {
		.leaf  = leaf,
		.index = spot.leaf,
		.width = spot.width,
		.light = spot.light,
	};

	const SettledChain settled = walkedFrom(ask.tracing, ask.stand, source, one.lump.point);
	if (!settled.found) {
		return;
	}
	const Lump walked = chainLump(settled);
	Lump better       = ask.broad ? walked : narrowedLump(walked, ask.lobe);
	better.weight     = one.lump.weight;
	one.lump          = soundLump(better) ? better : one.lump;
}

struct KeptLumps {
	Guide guide;
	std::array<double, mostLumps> radiance;
};

KeptLumps keptLumps(const GuideFrame& frame, const std::vector<SurveyCandidate>& found, const std::vector<std::uint32_t>& order) {
	KeptLumps kept = {
		.guide    = noGuide(),
		.radiance = {},
	};

	for (const std::uint32_t index : order) {
		if (kept.guide.count == frame.tunings.lumps || kept.guide.count == mostLumps) {
			break;
		}
		kept.radiance[kept.guide.count]    = found[index].radiance;
		kept.guide.lumps[kept.guide.count] = found[index].lump;
		kept.guide.count += 1;
	}
	return kept;
}

KeptLumps brightLumps(const KeptLumps& kept) {
	double brightest = 0;
	for (std::uint32_t k = 0; k < kept.guide.count; ++k) {
		brightest = std::max(brightest, double(kept.guide.lumps[k].weight));
	}
	KeptLumps bright   = kept;
	bright.guide.count = 0;
	for (std::uint32_t k = 0; k < kept.guide.count; ++k) {
		if (kept.guide.lumps[k].weight >= 0.01 * brightest) {
			bright.radiance[bright.guide.count]    = kept.radiance[k];
			bright.guide.lumps[bright.guide.count] = kept.guide.lumps[k];
			bright.guide.count += 1;
		}
	}
	return bright;
}

Guide normalizedLumps(const Guide& guide) {
	double total = 0;
	for (std::uint32_t k = 0; k < guide.count; ++k) {
		total += guide.lumps[k].weight;
	}
	Guide normal = guide;
	normal.count = total > 0 && std::isfinite(total) ? guide.count : 0;
	for (std::uint32_t k = 0; k < normal.count; ++k) {
		normal.lumps[k].weight = guide.lumps[k].weight / total;
	}
	normal.live = normal.count > 0;
	return normal;
}

Guide pixelGuideAt(const GuideFrame& frame, const HighlightSurvey& survey, const PixelSpot& pixel) {
	const Tracing& tracing  = frame.tracing;
	const SurveyStand stand = standAt(tracing, pixel);
	if (!stand.found) {
		return noGuide();
	}

	const LobeAsk ask = {
		.tracing = tracing,
		.stand   = stand,
		.stream  = surveyStream(tracing, pixel),
	};

	const SurveyLobe lobe = sharpestLobe(ask, pixelDraws, false);
	if (lobe.through) {
		return noGuide();
	}
	const math::Vector normal = stand.seen.normal;

	const LobeMirror mirror = {
		.mirror = normal * (2 * math::dot(normal, stand.outgoing)) - stand.outgoing,
		.width  = std::sqrt(2.0) * 0.5 * std::max(lobe.sharpness, 1e-3),
	};

	const CarryAsk carry = {
		.tracing = tracing,
		.survey  = survey,
		.stand   = stand,
		.lobe    = mirror,
		.broad   = lobe.wide * 4 >= pixelDraws,
	};

	std::vector<SurveyCandidate> found     = carriedCandidates(carry, pixel);
	const std::vector<std::uint32_t> order = heaviestFirst(found);
	for (std::size_t rank = 0; rank < std::min(std::size_t(refinedLumps), order.size()); ++rank) {
		refinedCandidate(carry, found[order[rank]]);
	}
	const KeptLumps bright = brightLumps(keptLumps(frame, found, order));
	Guide guide            = normalizedLumps(bright.guide);
	guide.material         = stand.seen.material;
	guide.sharpness        = lobe.sharpness;

	const ShareAsk share = {
		.frame    = frame,
		.stand    = stand,
		.radiance = bright.radiance,
	};

	guide.share = guide.live ? guideShare(share, guide) : 0;
	return guide;
}

void guidedRow(const GuideFrame& frame, const HighlightSurvey& survey, HighlightGuides& guides, int y) {
	GuideRow& row = guides.rows[std::size_t(y)];
	for (int x = 0; x < frame.tracing.lens.width; ++x) {
		const PixelSpot pixel = {
			.x = x,
			.y = y,
		};

		const Guide guide = pixelGuideAt(frame, survey, pixel);
		if (!guide.live) {
			continue;
		}
		guides.heads[std::size_t(y) * guides.width + std::size_t(x)] = std::uint32_t(row.heads.size());
		row.heads.push_back(packedGuide(guide, std::uint32_t(row.lumps.size()), row.lumps));
	}
	row.heads.shrink_to_fit();
	row.lumps.shrink_to_fit();
}

HighlightGuides pixelGuides(const GuideFrame& frame, const HighlightSurvey& survey) {
	const Lens& lens       = frame.tracing.lens;
	HighlightGuides guides = noHighlightGuides();
	guides.tunings         = frame.tunings;
	if (survey.across == 0) {
		return guides;
	}
	guides.width = std::uint32_t(lens.width);
	guides.heads.assign(std::size_t(lens.width) * std::size_t(lens.height), unguided);

	const GuideRow empty = {
		.heads = {},
		.lumps = {},
	};

	guides.rows.assign(std::size_t(lens.height), empty);
	inParallel(frame.threads, std::size_t(std::max(lens.height, 0)), [&](std::size_t y) {
		guidedRow(frame, survey, guides, int(y));
	});
	return guides;
}

}
