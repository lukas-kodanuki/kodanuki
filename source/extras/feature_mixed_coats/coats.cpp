#include "extras/feature_mixed_coats/coats.h"

#include "math/random.h"
#include "sdf/numbers.h"
#include "sdf/program.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace kodanuki {

struct Worn {
	Colour albedo;
	Colour glow;
	std::uint32_t material;
	math::Vector normal;
};

struct LeafWear {
	const CoatLadders& ladders;
	const CoatWear& wear;
	const PatternSpot& spot;
};

struct Wearing {
	Worn worn;
	double roughness;
	math::Vector tilt;
};

struct GrainCell {
	std::int64_t at[3];
	double along[3];
};

struct DualGrainCell {
	std::int64_t at[3];
	math::Dual along[3];
};

struct Layer {
	const Coat& coat;
	double weight;
};

double eased(double place) {
	const double held = std::clamp(place, 0.0, 1.0);
	return held * held * (3.0 - 2.0 * held);
}

double faded(double reach, double soft) {
	if (!(soft > 0.0)) {
		return reach >= 0.0 ? 1.0 : 0.0;
	}
	return eased(reach / soft + 0.5);
}

double hashedUnit(std::uint64_t value) {
	return math::unitOf(math::mixedBits(value));
}

double grainCorner(std::int64_t x, std::int64_t y, std::int64_t z) {
	const std::uint64_t key = math::mixedBits(std::uint64_t(x)) ^ math::mixedBits(std::uint64_t(y) + 0x51ULL) ^ math::mixedBits(std::uint64_t(z) + 0xA3ULL);
	return hashedUnit(key);
}

double grainRow(const GrainCell& cell, std::int64_t row, std::int64_t layer) {
	const double low = grainCorner(cell.at[0], row, layer);
	return low + (grainCorner(cell.at[0] + 1, row, layer) - low) * cell.along[0];
}

double grainPlane(const GrainCell& cell, std::int64_t layer) {
	const double low = grainRow(cell, cell.at[1], layer);
	return low + (grainRow(cell, cell.at[1] + 1, layer) - low) * cell.along[1];
}

double grainAt(const math::Vector& place) {
	const double floors[3] = { std::floor(place.x), std::floor(place.y), std::floor(place.z) };

	const GrainCell cell = {
		.at    = { std::int64_t(floors[0]), std::int64_t(floors[1]), std::int64_t(floors[2]) },
		.along = { eased(place.x - floors[0]), eased(place.y - floors[1]), eased(place.z - floors[2]) },
	};

	const double low = grainPlane(cell, cell.at[2]);
	return low + (grainPlane(cell, cell.at[2] + 1) - low) * cell.along[2];
}

double dithered(const math::Vector& place) {
	const std::uint64_t x = std::bit_cast<std::uint64_t>(place.x);
	const std::uint64_t y = math::mixedBits(std::bit_cast<std::uint64_t>(place.y));
	const std::uint64_t z = math::mixedBits(math::mixedBits(std::bit_cast<std::uint64_t>(place.z)));
	return hashedUnit(x ^ y ^ z);
}

math::Dual dualConstant(double value) {
	return math::constantAs<math::Dual>(value);
}

math::Dual easedDual(const math::Dual& place) {
	return place * place * (dualConstant(3.0) - dualConstant(2.0) * place);
}

math::Dual grainRowDual(const DualGrainCell& cell, std::int64_t row, std::int64_t layer) {
	const double low = grainCorner(cell.at[0], row, layer);
	return dualConstant(low) + dualConstant(grainCorner(cell.at[0] + 1, row, layer) - low) * cell.along[0];
}

math::Dual grainPlaneDual(const DualGrainCell& cell, std::int64_t layer) {
	const math::Dual low = grainRowDual(cell, cell.at[1], layer);
	return low + (grainRowDual(cell, cell.at[1] + 1, layer) - low) * cell.along[1];
}

math::Dual grainDual(const math::Dual place[3]) {
	const double floors[3] = { std::floor(place[0].value), std::floor(place[1].value), std::floor(place[2].value) };

	const DualGrainCell cell = {
		.at    = { std::int64_t(floors[0]), std::int64_t(floors[1]), std::int64_t(floors[2]) },
		.along = { easedDual(place[0] - dualConstant(floors[0])), easedDual(place[1] - dualConstant(floors[1])), easedDual(place[2] - dualConstant(floors[2])) },
	};

	const math::Dual low = grainPlaneDual(cell, cell.at[2]);
	return low + (grainPlaneDual(cell, cell.at[2] + 1) - low) * cell.along[2];
}

math::Dual scaledDual(const math::Dual& value, double factor, double offset) {
	return value * dualConstant(factor) + dualConstant(offset);
}

math::Dual grainRelief(const math::Dual place[3]) {
	const math::Dual coarse  = grainDual(place);
	const math::Dual fine[3] = { scaledDual(place[0], 2.0, 17.3), scaledDual(place[1], 2.0, 5.1), scaledDual(place[2], 2.0, 11.7) };
	return dualConstant(2.0) * (dualConstant(0.65) * coarse + dualConstant(0.35) * grainDual(fine)) - dualConstant(1.0);
}

math::Dual pitsRelief(const math::Dual place[3]) {
	const math::Dual deep = (grainDual(place) - dualConstant(0.6)) * dualConstant(4.0);
	return -easedDual(math::min(math::max(deep, dualConstant(0.0)), dualConstant(1.0)));
}

math::Dual streakRelief(const Coat& coat, const math::Dual place[3]) {
	const math::Dual reach    = place[0] * dualConstant(coat.along.x) + place[1] * dualConstant(coat.along.y) + place[2] * dualConstant(coat.along.z);
	const math::Dual drawn    = reach * dualConstant(-0.88);
	const math::Dual moved[3] = { place[0] + drawn * dualConstant(coat.along.x), place[1] + drawn * dualConstant(coat.along.y), place[2] + drawn * dualConstant(coat.along.z) };
	return dualConstant(2.0) * grainDual(moved) - dualConstant(1.0);
}

math::Dual noiseRelief(const Coat& coat, const math::Dual place[3]) {
	switch (coat.relief) {
		case Relief::Grain: return grainRelief(place);
		case Relief::Pits: return pitsRelief(place);
		case Relief::Streak: return streakRelief(coat, place);
		case Relief::Pattern: break;
	}

	return dualConstant(0);
}

math::Vector dualSlope(const math::Dual& height) {
	return { height.gradient[0], height.gradient[1], height.gradient[2] };
}

math::Vector noiseSlope(const Coat& coat, const math::Vector& world) {
	const double span      = double(coat.span);
	const math::Dual at[3] = { math::coordinateAs<math::Dual>(world.x / span, 0), math::coordinateAs<math::Dual>(world.y / span, 1), math::coordinateAs<math::Dual>(world.z / span, 2) };
	return dualSlope(noiseRelief(coat, at)) * (double(coat.amount) / span);
}

math::Vector placedPoint(const Placement& placement, const math::Vector& v, bool offset) {
	const math::Vector origin = offset ? math::Vector { placement.origin[0], placement.origin[1], placement.origin[2] } : math::Vector { 0, 0, 0 };
	const math::Vector d      = v - origin;
	const double* row[3]      = { placement.linear[0], placement.linear[1], placement.linear[2] };
	return { row[0][0] * d.x + row[0][1] * d.y + row[0][2] * d.z, row[1][0] * d.x + row[1][1] * d.y + row[1][2] * d.z, row[2][0] * d.x + row[2][1] * d.y + row[2][2] * d.z };
}

math::Vector placedBack(const Placement& placement, const math::Vector& g) {
	const double* row[3] = { placement.linear[0], placement.linear[1], placement.linear[2] };
	return { row[0][0] * g.x + row[1][0] * g.y + row[2][0] * g.z, row[0][1] * g.x + row[1][1] * g.y + row[2][1] * g.z, row[0][2] * g.x + row[1][2] * g.y + row[2][2] * g.z };
}

math::Vector patternSlope(const Coat& coat, const PatternSpot& spot) {
	const bool local        = !coat.height.world && spot.leaf != noLeaf;
	const Placement& placed = spot.scene.leaves[local ? spot.leaf : 0].placement;
	const math::Vector p    = local ? placedPoint(placed, spot.point, true) : spot.point;
	const math::Vector n    = local ? math::normalized(placedPoint(placed, spot.normal, false)) : spot.normal;
	std::vector<math::Dual> registers;

	const Inputs<math::Dual> inputs = {
		.arguments = spot.scene.arguments.data() + coat.height.arguments,
		.p         = { math::coordinateAs<math::Dual>(p.x, 0), math::coordinateAs<math::Dual>(p.y, 1), math::coordinateAs<math::Dual>(p.z, 2) },
		.n         = { dualConstant(n.x), dualConstant(n.y), dualConstant(n.z) },
	};

	const math::Vector slope = dualSlope(evaluate(spot.scene.programs[std::size_t(coat.height.program)], inputs, registers));
	return (local ? placedBack(placed, slope) : slope) * double(coat.amount);
}

math::Vector reliefSlope(const Coat& coat, const PatternSpot& spot) {
	if (coat.relief == Relief::Pattern) {
		return patternSlope(coat, spot);
	}
	return noiseSlope(coat, spot.point);
}

double shapeAt(const CoatProbe& probe, const Coat& coat) {
	return patternAt(probe.spot, coat.pattern, probe.registers);
}

double maskReach(const CoatProbe& probe, const Coat& coat) {
	switch (coat.mask) {
		case CoatMask::Inside: return -shapeAt(probe, coat);
		case CoatMask::Along: return double(coat.width) * 0.5 - std::abs(shapeAt(probe, coat));
		case CoatMask::Facing: return math::dot(probe.spot.normal, coat.axis) - double(coat.level);
		case CoatMask::Above: return math::dot(probe.spot.point, coat.axis) - double(coat.level);
		case CoatMask::Pattern: return patternAt(probe.spot, coat.pattern, probe.registers);
		case CoatMask::Everywhere: break;
	}

	return 1;
}

double raggedReach(const Coat& coat, const math::Vector& world) {
	if (!(coat.grain > 0)) {
		return 0;
	}
	return double(coat.ragged) * (2.0 * grainAt(world * (1.0 / double(coat.grain))) - 1.0);
}

double coatWeight(const CoatProbe& probe, const Coat& coat) {
	if (coat.mask == CoatMask::Everywhere) {
		return 1;
	}
	const double reach = maskReach(probe, coat) + raggedReach(coat, probe.spot.point);
	if (coat.mask == CoatMask::Pattern) {
		return std::clamp(reach, 0.0, 1.0);
	}
	return faded(reach, double(coat.soft));
}

std::uint32_t ladderedMaterial(const CoatLadders& ladders, std::uint32_t material, double roughness, const math::Vector& point) {
	const std::uint32_t first = ladders.ladders[material];
	if (first == noMaterial || roughness == ladders.roughness[material]) {
		return material;
	}
	const double rung  = std::clamp(roughness, 0.0, 1.0) * roughnessSteps;
	const double lower = std::min(std::floor(rung), double(roughnessSteps));
	const int step     = int(lower) + (dithered(point) < rung - lower ? 1 : 0);
	return first + std::uint32_t(std::min(step, roughnessSteps));
}

Colour toward(const Colour& from, const Colour& to, double share) {
	return Colour {
		.red   = from.red + (to.red - from.red) * share,
		.green = from.green + (to.green - from.green) * share,
		.blue  = from.blue + (to.blue - from.blue) * share,
	};
}

Wearing painted(const LeafWear& leaf, const Wearing& wearing, const Layer& layer) {
	const Material& paint = leaf.wear.shading.looks.materials[layer.coat.material];
	const double own      = leaf.ladders.roughness[layer.coat.material];
	Wearing next          = wearing;
	next.worn.albedo      = toward(wearing.worn.albedo, albedoAt(leaf.spot, paint.albedo, leaf.wear.registers), layer.weight);
	next.worn.glow        = leaf.wear.outside ? toward(wearing.worn.glow, glowOf(paint), layer.weight) : wearing.worn.glow;
	next.roughness        = layer.weight >= 1.0 ? own : wearing.roughness + (own - wearing.roughness) * layer.weight;
	next.worn.material    = layer.weight >= 0.5 ? layer.coat.material : wearing.worn.material;
	return next;
}

Colour shadedAlbedo(const Colour& albedo, double amount) {
	const Colour white = { 1, 1, 1 };
	return amount < 0.0 ? albedo * (1.0 + std::max(amount, -1.0)) : toward(albedo, white, std::min(amount, 1.0));
}

double roughened(double roughness, double amount) {
	return roughness + (amount > 0.0 ? (1.0 - roughness) * std::min(amount, 1.0) : roughness * std::max(amount, -1.0));
}

math::Vector tilted(const LeafWear& leaf, const Wearing& wearing, const Layer& layer) {
	const math::Vector rise   = reliefSlope(layer.coat, leaf.spot);
	const math::Vector normal = leaf.spot.normal;
	const math::Vector slope  = (rise - normal * math::dot(rise, normal)) * layer.weight;
	return math::length(slope) > 0.0 ? wearing.tilt + slope : wearing.tilt;
}

Wearing layered(const LeafWear& leaf, const Wearing& wearing, const Layer& layer) {
	const Coat& coat     = layer.coat;
	const double amount  = double(coat.amount) * layer.weight;
	const Colour white   = { 1, 1, 1 };
	const double wetness = std::clamp(amount, 0.0, 1.0);
	Wearing next         = wearing;
	switch (coat.effect) {
		case CoatEffect::Paint: return painted(leaf, wearing, layer);
		case CoatEffect::Tint: next.worn.albedo = wearing.worn.albedo * toward(white, coat.colour, layer.weight); break;
		case CoatEffect::Shade: next.worn.albedo = shadedAlbedo(wearing.worn.albedo, amount); break;
		case CoatEffect::Rough: next.roughness = roughened(wearing.roughness, amount); break;
		case CoatEffect::Wet:
			next.worn.albedo = wearing.worn.albedo * (1.0 - 0.5 * wetness);
			next.roughness   = wearing.roughness * (1.0 - 0.8 * wetness);
			break;
		case CoatEffect::Glow: next.worn.glow = leaf.wear.outside ? wearing.worn.glow + coat.colour * amount : wearing.worn.glow; break;
		case CoatEffect::Bump: next.tilt = tilted(leaf, wearing, layer); break;
	}

	return next;
}

Worn bareWorn(const CoatWear& wear, const PatternSpot& spot, std::uint32_t leaf) {
	const std::uint32_t material = wear.shading.scene.leaves[leaf].material;
	const Colour black           = { 0, 0, 0 };
	return Worn {
		.albedo   = albedoAt(spot, wear.shading.looks.materials[material].albedo, wear.registers),
		.glow     = wear.outside ? glowOf(wear.shading.looks.materials[material]) : black,
		.material = material,
		.normal   = spot.normal,
	};
}

Worn leafWorn(const LeafWear& leaf, std::uint32_t index) {
	const Worn bare = bareWorn(leaf.wear, leaf.spot, index);

	const CoatProbe probe = {
		.spot      = leaf.spot,
		.registers = leaf.wear.registers,
	};

	Wearing wearing = {
		.worn      = bare,
		.roughness = leaf.ladders.roughness[bare.material],
		.tilt      = { 0, 0, 0 },
	};

	for (const std::uint32_t coat : leaf.wear.shading.scene.leaves[index].coats) {
		const Layer layer = {
			.coat   = leaf.wear.shading.looks.coats[coat],
			.weight = coatWeight(probe, leaf.wear.shading.looks.coats[coat]),
		};

		wearing = layer.weight > 0.0 ? layered(leaf, wearing, layer) : wearing;
	}
	const bool tilting = wearing.tilt.x != 0.0 || wearing.tilt.y != 0.0 || wearing.tilt.z != 0.0;
	Worn worn          = wearing.worn;
	worn.normal        = tilting ? math::normalized(bare.normal - wearing.tilt) : bare.normal;
	worn.material      = ladderedMaterial(leaf.ladders, worn.material, wearing.roughness, leaf.spot.point);
	return worn;
}

Worn sideWorn(const CoatLadders& ladders, const CoatWear& wear, std::uint32_t index) {
	const PatternSpot spot = {
		.scene  = wear.shading.scene,
		.leaf   = index,
		.point  = wear.hit.point,
		.normal = wear.shaded.bsdf.normal,
	};

	const LeafWear leaf = {
		.ladders = ladders,
		.wear    = wear,
		.spot    = spot,
	};

	return leafWorn(leaf, index);
}

bool leafCoated(const Scene& scene, std::uint32_t leaf) {
	return leaf != noLeaf && !scene.leaves[leaf].coats.empty();
}

bool seamed(const Hit& hit) {
	return hit.other != noLeaf && hit.share > 0;
}

Sides sidesBeyond(const CoatWear& wear, std::uint32_t material) {
	Sides sides = wear.shaded.bsdf.sides;
	if (material == wear.shaded.bsdf.material || sides.inside) {
		return sides;
	}
	sides.beyond = flatSpectrum(wear.shading.materials.finishes[material].index);
	return sides;
}

Shaded wornCoats(const CoatLadders& ladders, const CoatWear& wear) {
	const Hit& hit = wear.hit;
	if (!leafCoated(wear.shading.scene, hit.leaf) && !(seamed(hit) && leafCoated(wear.shading.scene, hit.other))) {
		return wear.shaded;
	}
	const Worn near    = sideWorn(ladders, wear, hit.leaf);
	Shaded shaded      = wear.shaded;
	shaded.bsdf.albedo = near.albedo;
	shaded.glow        = near.glow;
	if (seamed(hit)) {
		const Worn far     = sideWorn(ladders, wear, hit.other);
		shaded.bsdf.albedo = near.albedo * (1 - hit.share) + far.albedo * hit.share;
		shaded.glow        = wear.outside ? near.glow * (1 - hit.share) + far.glow * hit.share : near.glow;
	}
	shaded.bsdf.sides    = sidesBeyond(wear, near.material);
	shaded.bsdf.material = near.material;
	shaded.bsdf.normal   = math::dot(near.normal, -wear.heading) > 0.0 ? near.normal : wear.shaded.bsdf.normal;
	return shaded;
}

bool sceneWearsCoats(const Scene& scene) {
	for (const Leaf& leaf : scene.leaves) {
		if (!leaf.coats.empty()) {
			return true;
		}
	}
	return false;
}

CoatLadders coatLaddersOf(const Looks& looks, std::uint32_t first) {
	const bool roughens = coatsRoughen && sceneRoughens(looks);

	CoatLadders ladders = {
		.ladders   = {},
		.roughness = {},
		.rungs     = {},
		.laddered  = {},
		.first     = first,
	};

	for (std::uint32_t material = 0; material < looks.materials.size(); ++material) {
		const Material& own = looks.materials[material];
		const bool matte    = own.kind == MaterialKind::Matte;
		const bool climbs   = roughens && !matte;
		ladders.ladders.push_back(climbs ? first + std::uint32_t(ladders.rungs.size()) : noMaterial);
		ladders.roughness.push_back(matte ? 1.0 : double(own.roughness));
		if (climbs) {
			ladders.laddered.push_back(material);
		}
		for (int step = 0; climbs && step <= roughnessSteps; ++step) {
			Material rung  = own;
			rung.roughness = float(double(step) / roughnessSteps);
			ladders.rungs.push_back(rung);
		}
	}
	return ladders;
}

Materials withRungs(const CoatLadders& ladders, const Materials& materials, SmoothGlass smooth) {
	Materials featured = materials;
	for (const Material& rung : ladders.rungs) {
		featured.finishes.push_back(finishAmong(featured, rung, smooth));
	}
	return featured;
}

std::uint32_t ladderBody(const CoatLadders& ladders, std::uint32_t material) {
	if (material == noMaterial || material < ladders.first || material >= ladders.first + ladders.rungs.size()) {
		return material;
	}
	return ladders.laddered[(material - ladders.first) / std::uint32_t(roughnessSteps + 1)];
}

}
