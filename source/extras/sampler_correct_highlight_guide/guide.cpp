#include "extras/sampler_correct_highlight_guide/guide.h"

#include <algorithm>
#include <cmath>

namespace kodanuki {

constexpr double lumpCutoff         = 60;
constexpr std::uint32_t deepPoints  = 4;
constexpr std::uint32_t deepSpots   = 3;
constexpr std::uint32_t deepFound   = 64;
constexpr double deepCell           = 0.3;
constexpr std::uint32_t noGuideKind = 0xFFFFFFFFu;

double keptMass() {
	return 1 - std::exp(-0.5 * lumpCutoff);
}

Guide noGuide() {
	Guide guide;
	guide.live      = false;
	guide.material  = noGuideKind;
	guide.count     = 0;
	guide.share     = 0;
	guide.sharpness = 1;
	return guide;
}

DeepGuide noDeepGuide() {
	const PackedGuide head = {
		.first     = 0,
		.count     = 0,
		.material  = noGuideKind,
		.share     = 0,
		.sharpness = 1,
	};

	DeepGuide deep;
	deep.head = head;
	return deep;
}

math::Vector floatVector(const std::array<float, 3>& v) {
	return { double(v[0]), double(v[1]), double(v[2]) };
}

std::array<float, 3> floatsOf(const math::Vector& v) {
	return { float(v.x), float(v.y), float(v.z) };
}

Lump unpackedLump(const PackedLump& packed) {
	const math::Vector axis            = math::normalized(floatVector(packed.axis));
	const math::Vector raw             = floatVector(packed.across);
	const math::Vector across          = math::normalized(raw - axis * math::dot(raw, axis));
	const std::array<double, 3> factor = { double(packed.factor[0]), double(packed.factor[1]), double(packed.factor[2]) };
	const double c00                   = factor[0] * factor[0];
	const double c01                   = factor[0] * factor[1];
	const double c11                   = factor[1] * factor[1] + factor[2] * factor[2];
	const double root                  = factor[0] * factor[2];
	const double det                   = root * root;
	return Lump {
		.axis      = axis,
		.across    = across,
		.up        = math::cross(axis, across),
		.mean      = { double(packed.mean[0]), double(packed.mean[1]) },
		.precision = { c11 / det, -c01 / det, c00 / det },
		.factor    = factor,
		.height    = 1 / (2 * math::pi * root),
		.weight    = double(packed.weight),
		.point     = floatVector(packed.point),
	};
}

void headInto(const PackedGuide& head, Guide& guide) {
	guide.live      = head.count > 0;
	guide.material  = head.material;
	guide.count     = head.count;
	guide.share     = double(head.share);
	guide.sharpness = double(head.sharpness);
}

void unpackGuide(const PackedGuide& head, const PackedLump* lumps, Guide& guide) {
	double total = 0;
	for (std::uint32_t k = 0; k < head.count; ++k) {
		guide.lumps[k] = unpackedLump(lumps[k]);
		total += guide.lumps[k].weight;
	}
	for (std::uint32_t k = 0; k < head.count; ++k) {
		guide.lumps[k].weight = guide.lumps[k].weight / total;
	}
	headInto(head, guide);
}

Guide unpackedGuide(const PackedGuide& head, const PackedLump* lumps) {
	Guide guide = noGuide();
	if (head.count == 0) {
		return guide;
	}
	unpackGuide(head, lumps, guide);
	return guide;
}

PackedLump packedLump(const Lump& lump) {
	return PackedLump {
		.axis   = floatsOf(lump.axis),
		.across = floatsOf(lump.across),
		.point  = floatsOf(lump.point),
		.mean   = { float(lump.mean[0]), float(lump.mean[1]) },
		.factor = { float(lump.factor[0]), float(lump.factor[1]), float(lump.factor[2]) },
		.weight = float(lump.weight),
	};
}

PackedGuide packedGuide(const Guide& guide, std::uint32_t first, std::vector<PackedLump>& lumps) {
	const std::uint32_t count = guide.live ? guide.count : 0;
	for (std::uint32_t k = 0; k < count; ++k) {
		lumps.push_back(packedLump(guide.lumps[k]));
	}
	return PackedGuide {
		.first     = first,
		.count     = count,
		.material  = guide.material,
		.share     = float(guide.share),
		.sharpness = float(guide.sharpness),
	};
}

std::array<double, 2> shiftedCentre(const Lump& lump, const math::Vector& place) {
	const math::Vector to = math::normalized(lump.point - place);
	const double along    = math::dot(to, lump.axis);
	if (!(along > 1e-3)) {
		return lump.mean;
	}
	return { lump.mean[0] + math::dot(to, lump.across) / along, lump.mean[1] + math::dot(to, lump.up) / along };
}

SeenLumps seenLumps(const Guide& guide, const math::Vector& place) {
	SeenLumps seen = {
		.guide   = guide,
		.centres = {},
	};

	for (std::uint32_t k = 0; k < guide.count; ++k) {
		seen.centres[k] = shiftedCentre(guide.lumps[k], place);
	}
	return seen;
}

void unsteered(GuideState& state) {
	state.steering.guide.live  = false;
	state.steering.guide.count = 0;
}

void turnSteering(const GuidePlace& place, SeenLumps& seen) {
	for (std::uint32_t k = 0; k < seen.guide.count; ++k) {
		seen.centres[k] = shiftedCentre(seen.guide.lumps[k], place.point);
	}
}

bool steersPlace(const PackedGuide& head, const GuidePlace& place) {
	return !place.inside && head.count > 0 && head.material == place.material;
}

void steerByHead(const PackedGuide& head, const PackedLump* lumps, const GuidePlace& place, GuideState& state) {
	if (!steersPlace(head, place)) {
		unsteered(state);
		return;
	}
	unpackGuide(head, lumps, state.steering.guide);
	state.unpacked = unguided;
	turnSteering(place, state.steering);
}

void steerByKeptHead(const PackedGuide& head, const GuidePlace& place, GuideState& state) {
	if (!steersPlace(head, place)) {
		unsteered(state);
		return;
	}
	headInto(head, state.steering.guide);
	turnSteering(place, state.steering);
}

bool pixelGuided(const HighlightGuides& guides, std::uint32_t pixel) {
	return pixel != unguided && pixel < guides.heads.size() && guides.heads[pixel] != unguided;
}

void steerByPixel(const HighlightGuides& guides, const GuidePlace& place, GuideState& state) {
	if (!pixelGuided(guides, state.pixel)) {
		unsteered(state);
		return;
	}
	const GuideRow& row     = guides.rows[state.pixel / guides.width];
	const PackedGuide& head = row.heads[guides.heads[state.pixel]];
	if (state.unpacked == state.pixel) {
		steerByKeptHead(head, place, state);
		return;
	}
	steerByHead(head, row.lumps.data() + head.first, place, state);
	state.unpacked = state.steering.guide.live ? state.pixel : unguided;
}

void steerByDeep(const DeepGuide& deep, const GuidePlace& place, GuideState& state) {
	steerByHead(deep.head, deep.lumps.data(), place, state);
}

double lumpAt(const Lump& lump, const std::array<double, 2>& centre, const math::Vector& direction) {
	const double along = math::dot(direction, lump.axis);
	if (along <= 1e-9) {
		return 0;
	}
	const double u      = math::dot(direction, lump.across) / along - centre[0];
	const double v      = math::dot(direction, lump.up) / along - centre[1];
	const double spread = lump.precision[0] * u * u + 2 * lump.precision[1] * u * v + lump.precision[2] * v * v;
	if (spread > lumpCutoff) {
		return 0;
	}
	return lump.height / keptMass() * std::exp(-0.5 * spread) / (along * along * along);
}

bool aboveBoth(const math::Vector& normal, const math::Vector& outgoing, const math::Vector& incoming) {
	return math::dot(normal, incoming) > 0 && math::dot(normal, outgoing) > 0;
}

double lumpDensity(const SeenLumps& lumps, const LumpPair& pair) {
	if (!aboveBoth(pair.normal, pair.outgoing, pair.incoming)) {
		return 0;
	}
	double sum = 0;
	for (std::uint32_t k = 0; k < lumps.guide.count; ++k) {
		sum += lumps.guide.lumps[k].weight * lumpAt(lumps.guide.lumps[k], lumps.centres[k], pair.incoming);
	}
	return sum;
}

std::uint32_t pickedLump(const Guide& guide, double pick) {
	std::uint32_t index = 0;
	double below        = guide.lumps[0].weight;
	while (index + 1 < guide.count && pick >= below) {
		index += 1;
		below += guide.lumps[index].weight;
	}
	return index;
}

math::Vector drawnFromLump(const SeenLumps& lumps, const LumpQuery& query) {
	const std::uint32_t index         = pickedLump(lumps.guide, query.pick);
	const Lump& lump                  = lumps.guide.lumps[index];
	const std::array<double, 2>& seen = lumps.centres[index];
	const double radius               = std::sqrt(-2 * std::log(std::max(1 - query.pair.u * keptMass(), 1e-300)));
	const double angle                = 2 * math::pi * query.pair.v;
	const double a                    = radius * std::cos(angle);
	const double b                    = radius * std::sin(angle);
	const double u                    = seen[0] + lump.factor[0] * a;
	const double v                    = seen[1] + lump.factor[1] * a + lump.factor[2] * b;
	return math::normalized(lump.axis + lump.across * u + lump.up * v);
}

LumpDraw lumpDirection(const SeenLumps& lumps, const LumpQuery& query) {
	const math::Vector incoming = drawnFromLump(lumps, query);
	const bool finite           = std::isfinite(incoming.x + incoming.y + incoming.z);
	return LumpDraw {
		.found    = finite && aboveBoth(query.normal, query.outgoing, incoming),
		.incoming = incoming,
	};
}

double lampGlare(const Lamp& lamp) {
	const double bright = luminanceOf(lamp.power);
	switch (lamp.kind) {
		case LightKind::Sphere: return bright / (math::pi * std::max(lamp.radius * lamp.radius, 1e-12));
		case LightKind::Quad: return bright / std::max(lamp.area, 1e-12);
		case LightKind::Moon:
		case LightKind::Sun: return bright / (math::pi * std::max(std::pow(std::acos(std::clamp(lamp.cosine, -1.0, 1.0)), 2), 1e-12));
		case LightKind::Point:
		case LightKind::Directional: return bright * 1e4;
	}

	return bright * 1e4;
}

std::uint64_t filedKey(const math::Vector& point, double side, const std::array<int, 3>& shift) {
	const std::int64_t x = std::int64_t(std::floor(point.x / side)) + shift[0];
	const std::int64_t y = std::int64_t(std::floor(point.y / side)) + shift[1];
	const std::int64_t z = std::int64_t(std::floor(point.z / side)) + shift[2];
	return (std::uint64_t(x) & 0x1FFFFF) | ((std::uint64_t(y) & 0x1FFFFF) << 21) | ((std::uint64_t(z) & 0x1FFFFF) << 42);
}

struct DeepSpot {
	const Highlight& spot;
	const DeepPlace& place;
	double near;
};

Lump spreadLump(const Highlight& spot, const math::Vector& towards, const std::array<double, 3>& spread) {
	const double distance     = math::length(towards);
	const math::Vector axis   = towards * (1 / distance);
	const math::Vector across = math::normalized(spot.across - axis * math::dot(spot.across, axis));
	const double det          = spread[0] * spread[2] - spread[1] * spread[1];
	const double factor0      = std::sqrt(spread[0]);
	const double factor1      = spread[1] / factor0;
	return Lump {
		.axis      = axis,
		.across    = across,
		.up        = math::cross(axis, across),
		.mean      = { 0, 0 },
		.precision = { spread[2] / det, -spread[1] / det, spread[0] / det },
		.factor    = { factor0, factor1, std::sqrt(std::max(spread[2] - factor1 * factor1, 1e-30)) },
		.height    = 1 / (2 * math::pi * std::sqrt(det)),
		.weight    = 0,
		.point     = spot.point,
	};
}

math::Vector movedPoint(const Highlight& spot, const math::Vector& standing) {
	const math::Vector away = standing - spot.origin;
	return spot.point + spot.motion[0] * math::dot(away, spot.sideways[0]) + spot.motion[1] * math::dot(away, spot.sideways[1]);
}

std::array<double, 3> scaledSpread(const Highlight& spot, double distance) {
	const double scale = spot.distance * spot.distance / (distance * distance);
	return { spot.spread[0] * scale, spot.spread[1] * scale, spot.spread[2] * scale };
}

bool spreadFits(const std::array<double, 3>& spread) {
	const double det = spread[0] * spread[2] - spread[1] * spread[1];
	return det > 1e-24 && spread[0] < 1 && spread[2] < 1;
}

struct FoundLumps {
	std::array<Lump, deepFound> lumps;
	std::uint32_t count;
};

void foundDeepSpot(const DeepGuides& deep, const DeepSpot& found, FoundLumps& into) {
	const Highlight& spot      = found.spot;
	const math::Vector point   = movedPoint(spot, found.place.point);
	const math::Vector towards = point - found.place.point;
	const double distance      = math::length(towards);
	const double facing        = math::dot(towards, found.place.normal);
	if (!(distance > 1e-6) || facing <= 0) {
		return;
	}
	const std::array<double, 3> spread = scaledSpread(spot, distance);
	if (!spreadFits(spread)) {
		return;
	}
	Lump lump        = spreadLump(spot, towards, spread);
	lump.point       = point;
	const double det = spread[0] * spread[2] - spread[1] * spread[1];
	lump.weight      = found.near * lampGlare(deep.lights.lamps[spot.light]) * std::sqrt(det) * facing / distance;
	if (!(lump.weight > 0) || !std::isfinite(lump.weight)) {
		return;
	}
	into.lumps[into.count] = lump;
	into.count += 1;
}

bool nearSurvey(const Highlight& front, const DeepPlace& place) {
	const double apart = math::length(place.point - front.origin);
	return front.seen == place.material && apart <= 1.5 * deepCell && math::dot(front.seenNormal, place.normal) >= 0.9;
}

void foundDeepPoint(const DeepGuides& deep, const std::vector<Highlight>& spots, const DeepPlace& place, FoundLumps& into) {
	const double reach  = 1.5 * deepCell;
	const double apart  = math::length(place.point - spots.front().origin);
	const double near   = (1 - apart / reach) * (1 - apart / reach);
	std::uint32_t taken = 0;
	for (const Highlight& spot : spots) {
		if (into.count == deepFound || taken == deepSpots || spot.light >= deep.lights.lamps.size()) {
			return;
		}
		taken += 1;

		const DeepSpot found = {
			.spot  = spot,
			.place = place,
			.near  = near,
		};

		foundDeepSpot(deep, found, into);
	}
}

FoundLumps foundDeepLumps(const DeepGuides& deep, const std::vector<std::uint32_t>& points, const DeepPlace& place) {
	FoundLumps found;
	found.count = 0;

	std::uint32_t used = 0;
	for (const std::uint32_t index : points) {
		const std::vector<Highlight>& spots = deep.guides.survey.spots[index];
		if (used == deepPoints) {
			break;
		}
		if (!nearSurvey(spots.front(), place)) {
			continue;
		}
		used += 1;
		foundDeepPoint(deep, spots, place, found);
	}
	return found;
}

Guide keptDeepLumps(const DeepGuides& deep, FoundLumps& found, std::uint32_t material) {
	std::sort(found.lumps.begin(), found.lumps.begin() + found.count, [](const Lump& a, const Lump& b) {
		return a.weight > b.weight;
	});

	const std::uint32_t kept = std::min(found.count, deep.guides.tunings.lumps);
	double total             = 0;
	for (std::uint32_t k = 0; k < kept; ++k) {
		total += found.lumps[k].weight;
	}
	if (!(total > 0) || !std::isfinite(total)) {
		return noGuide();
	}
	Guide guide = noGuide();
	for (std::uint32_t k = 0; k < kept; ++k) {
		guide.lumps[k]        = found.lumps[k];
		guide.lumps[k].weight = found.lumps[k].weight / total;
	}
	guide.live      = true;
	guide.material  = material;
	guide.count     = kept;
	guide.share     = deep.guides.tunings.deepShare;
	guide.sharpness = 1;
	return guide;
}

DeepGuide deepGuide(const DeepGuides& deep, const DeepPlace& place) {
	const std::array<int, 3> here = { 0, 0, 0 };
	const auto filed              = deep.guides.survey.filed.find(filedKey(place.point, deepCell, here));
	if (filed == deep.guides.survey.filed.end()) {
		return noDeepGuide();
	}
	FoundLumps found = foundDeepLumps(deep, filed->second, place);
	if (found.count == 0) {
		return noDeepGuide();
	}
	const Guide guide = keptDeepLumps(deep, found, place.material);
	thread_local std::vector<PackedLump> lumps;
	lumps.clear();
	DeepGuide packed = noDeepGuide();
	packed.head      = packedGuide(guide, 0, lumps);
	std::copy(lumps.begin(), lumps.end(), packed.lumps.begin());
	return packed;
}

GuideTunings defaultGuideTunings() {
	return GuideTunings {
		.lumps            = 12,
		.leastShare       = 0.02,
		.mostShare        = 0.5,
		.deepShare        = 0.3,
		.deepLeastDensity = 3,
		.deep             = true,
	};
}

HighlightGuides noHighlightGuides() {
	const HighlightSurvey survey = {
		.across = 0,
		.down   = 0,
		.spots  = {},
		.filed  = {},
	};

	return HighlightGuides {
		.survey  = survey,
		.width   = 0,
		.heads   = {},
		.rows    = {},
		.tunings = defaultGuideTunings(),
	};
}

}
