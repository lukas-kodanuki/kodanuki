#include "extras/sampler_correct_glowing_surfaces/glowing_surfaces.h"

#include "tree/cut.h"
#include "walk/hit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kodanuki {

struct FoundCell {
	std::uint64_t place;
	double guess;
	double weight;
	math::Vector middle;
	math::Vector half;
};

struct GlowSources {
	std::vector<double> leaves;
	std::vector<double> objects;
	std::vector<double> cuts;
};

struct GridGathering {
	const Geometry& geometry;
	const GlowSources& sources;
	int depth;
	std::vector<FoundCell>& found;
};

struct CellBox {
	math::Vector middle;
	math::Vector half;
};

struct BoxSpan {
	double enter;
	double leave;
};

GlowCells noGlowCells() {
	const Region none = {
		.lower = { 0, 0, 0 },
		.size  = { 0, 0, 0 },
	};

	return GlowCells {
		.region  = none,
		.depth   = 0,
		.places  = {},
		.middles = {},
		.halves  = {},
		.guesses = {},
		.running = {},
	};
}

double meanGlow(const Colour& colour, double scale) {
	return std::max(channelMean(colour) * scale, 0.0);
}

double materialGlow(const Looks& looks, std::uint32_t material) {
	const Material& worn = looks.materials[material];
	return worn.aimed ? meanGlow(worn.glow, double(worn.strength)) : 0;
}

double sceneLeafGlow(const Looks& looks, const Leaf& leaf) {
	double worn  = materialGlow(looks, leaf.material);
	double added = 0;
	for (const std::uint32_t coat : leaf.coats) {
		const Coat& layer = looks.coats[coat];
		added += layer.effect == CoatEffect::Glow ? meanGlow(layer.colour, double(layer.amount)) : 0;
		worn = layer.effect == CoatEffect::Paint ? std::max(worn, materialGlow(looks, layer.material)) : worn;
	}
	return worn + added;
}

double stepsGlow(const std::vector<Step>& steps, const std::vector<double>& leaves) {
	double brightest = 0;
	for (const Step& step : steps) {
		brightest = step.leaf == noLeaf ? brightest : std::max(brightest, leaves[step.leaf]);
	}
	return brightest;
}

std::vector<double> programGlows(const std::vector<Program>& programs, const std::vector<double>& leaves) {
	std::vector<double> glows;
	for (const Program& program : programs) {
		glows.push_back(stepsGlow(program.steps, leaves));
	}
	return glows;
}

std::vector<double> cutGlows(const Geometry& geometry, const std::vector<double>& leaves) {
	std::vector<double> glows;
	for (std::uint32_t cut = 0; cut < geometry.tree.cuts.size(); ++cut) {
		glows.push_back(stepsGlow(keptStepsOf(geometry.scene, geometry.tree, cut), leaves));
	}
	return glows;
}

GlowSources glowSources(const Geometry& geometry, const Looks& looks) {
	std::vector<double> leaves;
	for (const Leaf& leaf : geometry.scene.leaves) {
		leaves.push_back(sceneLeafGlow(looks, leaf));
	}
	return GlowSources {
		.leaves  = leaves,
		.objects = programGlows(geometry.scene.objectPrograms, leaves),
		.cuts    = cutGlows(geometry, leaves),
	};
}

bool anyGlow(const GlowSources& sources) {
	return std::any_of(sources.leaves.begin(), sources.leaves.end(), [](double glow) {
		return glow > 0;
	});
}

double listGlow(const Geometry& geometry, const GlowSources& sources, std::uint32_t list) {
	const ProgramList& programs = geometry.tree.lists[list];
	double brightest            = 0;
	for (std::uint32_t k = 0; k < programs.count; ++k) {
		const ProgramReference& reference = geometry.tree.references[programs.first + k];
		const std::vector<double>& glows  = reference.source == ProgramSource::Object ? sources.objects : sources.cuts;
		brightest                         = std::max(brightest, glows[reference.index]);
	}
	return brightest;
}

double sourcedGlow(const GlowSources& sources, const Sourced& sourced) {
	const double near = sourced.leaf == noLeaf ? 0 : sources.leaves[sourced.leaf];
	const bool seam   = sourced.other != noLeaf && sourced.share > 0;
	return seam ? std::max(near, sources.leaves[sourced.other]) : near;
}

CellBox gridBox(const GlowCells& glows, const std::uint32_t* place) {
	const double cells        = std::ldexp(1.0, glows.depth);
	const Region& region      = glows.region;
	const math::Vector half   = { 0.5 * region.size[0] / cells, 0.5 * region.size[1] / cells, 0.5 * region.size[2] / cells };
	const math::Vector middle = { region.lower[0] + (2 * place[0] + 1) * half.x, region.lower[1] + (2 * place[1] + 1) * half.y, region.lower[2] + (2 * place[2] + 1) * half.z };

	return CellBox {
		.middle = middle,
		.half   = half,
	};
}

std::uint64_t gridPlace(int depth, const std::uint32_t* place) {
	const std::uint64_t cells = std::uint64_t(1) << depth;
	return place[0] + cells * (place[1] + cells * std::uint64_t(place[2]));
}

GlowCells gridOf(const Geometry& geometry, int depth) {
	GlowCells glows = noGlowCells();
	glows.region    = geometry.tree.region;
	glows.depth     = depth;
	return glows;
}

void keepGridCell(const GridGathering& gathering, std::uint32_t list, const std::uint32_t* place) {
	const CellBox box         = gridBox(gridOf(gathering.geometry, gathering.depth), place);
	const double point[3]     = { box.middle.x, box.middle.y, box.middle.z };
	const math::Vector toward = { 0, 0, 1 };

	const FieldQuery query = {
		.geometry = gathering.geometry,
		.list     = list,
		.point    = point,
		.toward   = toward,
		.inset    = 0,
		.beyond   = true,
	};

	const Sourced sourced = surfaceAt(query);
	const double guess    = sourcedGlow(gathering.sources, sourced);
	if (!(std::abs(sourced.value) <= math::length(box.half)) || !(guess > 0)) {
		return;
	}

	const FoundCell found = {
		.place  = gridPlace(gathering.depth, place),
		.guess  = guess,
		.weight = guess,
		.middle = box.middle,
		.half   = box.half,
	};

	gathering.found.push_back(found);
}

void keepCoveredCells(const GridGathering& gathering, std::uint32_t list, const PlacedLeaf& leaf) {
	const int shift          = gathering.depth - leaf.level;
	const std::uint32_t side = std::uint32_t(1) << shift;
	for (std::uint32_t k = 0; k < side * side * side; ++k) {
		const std::uint32_t place[3] = { (leaf.place[0] << shift) + k % side, (leaf.place[1] << shift) + k / side % side, (leaf.place[2] << shift) + k / (side * side) };
		keepGridCell(gathering, list, place);
	}
}

void gatherGridLeaf(const GridGathering& gathering, const PlacedLeaf& leaf) {
	if (leaf.entry.kind != CellKind::Surface) {
		return;
	}
	const std::uint32_t list = gathering.geometry.tree.surfaces[leaf.entry.payload].programs;
	if (!(listGlow(gathering.geometry, gathering.sources, list) > 0)) {
		return;
	}
	if (leaf.level < gathering.depth) {
		keepCoveredCells(gathering, list, leaf);
		return;
	}
	const int shift              = leaf.level - gathering.depth;
	const std::uint32_t place[3] = { leaf.place[0] >> shift, leaf.place[1] >> shift, leaf.place[2] >> shift };
	keepGridCell(gathering, list, place);
}

std::vector<FoundCell> mergedCells(std::vector<FoundCell> found) {
	std::stable_sort(found.begin(), found.end(), [](const FoundCell& a, const FoundCell& b) {
		return a.place < b.place;
	});

	std::vector<FoundCell> merged;
	for (const FoundCell& cell : found) {
		if (merged.empty() || merged.back().place != cell.place) {
			merged.push_back(cell);
			continue;
		}
		merged.back().guess  = std::max(merged.back().guess, cell.guess);
		merged.back().weight = std::max(merged.back().weight, cell.weight);
	}
	return merged;
}

GlowCells keptCells(const GlowCells& empty, const std::vector<FoundCell>& found) {
	GlowCells glows = empty;
	double total    = 0;
	for (const FoundCell& cell : mergedCells(found)) {
		total += cell.weight;
		glows.places.push_back(cell.place);
		glows.middles.push_back(cell.middle);
		glows.halves.push_back(cell.half);
		glows.guesses.push_back(cell.guess);
		glows.running.push_back(total);
	}
	return glows;
}

GlowCells gatheredGlowGrid(const Geometry& geometry, const Looks& looks, int depth) {
	const GlowSources sources = glowSources(geometry, looks);
	if (!anyGlow(sources)) {
		return noGlowCells();
	}
	std::vector<FoundCell> found;

	const GridGathering gathering = {
		.geometry = geometry,
		.sources  = sources,
		.depth    = depth,
		.found    = found,
	};

	for (const PlacedLeaf& leaf : placedLeaves(geometry.tree)) {
		gatherGridLeaf(gathering, leaf);
	}
	return keptCells(gridOf(geometry, depth), found);
}

std::uint32_t placeIndex(const GlowCells& glows, std::uint64_t place) {
	const auto found = std::lower_bound(glows.places.begin(), glows.places.end(), place);
	return found != glows.places.end() && *found == place ? std::uint32_t(found - glows.places.begin()) : noCell;
}

std::uint32_t gridCellAt(const GlowCells& glows, const math::Vector& point) {
	const double cells = std::ldexp(1.0, glows.depth);
	std::uint32_t place[3];
	for (int axis = 0; axis < 3; ++axis) {
		const double across = (math::component(point, axis) - glows.region.lower[axis]) / glows.region.size[axis] * cells;
		if (!(across >= 0) || !(across < cells)) {
			return noCell;
		}
		place[axis] = std::uint32_t(across);
	}
	return placeIndex(glows, gridPlace(glows.depth, place));
}

std::uint32_t cellAt(const GlowCells& glows, const math::Vector& place) {
	return glows.places.empty() ? noCell : gridCellAt(glows, place);
}

double coneFall(double radius, double distance) {
	const double sideways = radius / distance;
	return sideways * sideways / (1 + std::sqrt(std::max(1 - sideways * sideways, 0.0)));
}

BoxSpan noSpan() {
	return BoxSpan {
		.enter = 0,
		.leave = -1,
	};
}

BoxSpan cellSpan(const GlowCells& glows, std::uint32_t cell, const math::Vector& origin, const math::Vector& direction) {
	BoxSpan span = {
		.enter = -std::numeric_limits<double>::infinity(),
		.leave = std::numeric_limits<double>::infinity(),
	};

	for (int axis = 0; axis < 3; ++axis) {
		const double low     = math::component(glows.middles[cell] - glows.halves[cell], axis) - math::component(origin, axis);
		const double high    = math::component(glows.middles[cell] + glows.halves[cell], axis) - math::component(origin, axis);
		const double heading = math::component(direction, axis);
		if (heading == 0 && (low > 0 || high < 0)) {
			return noSpan();
		}
		if (heading == 0) {
			continue;
		}
		span.enter = std::max(span.enter, std::min(low / heading, high / heading));
		span.leave = std::min(span.leave, std::max(low / heading, high / heading));
	}
	return span;
}

bool spanAhead(const BoxSpan& span) {
	return span.leave > std::max(span.enter, 0.0);
}

double drawShare(const GlowCells& glows, std::uint32_t cell) {
	const double before = cell > 0 ? glows.running[cell - 1] : 0;
	return (glows.running[cell] - before) / glows.running.back();
}

double cellDensity(const GlowCells& glows, std::uint32_t cell, const math::Vector& from, const math::Vector& toward) {
	const math::Vector offset = glows.middles[cell] - from;
	const double distance     = math::length(offset);
	const double radius       = math::length(glows.halves[cell]);
	if (distance <= radius) {
		return 0;
	}
	const double fall = coneFall(radius, distance);
	if (1 - math::dot(toward, offset) / distance > fall || !spanAhead(cellSpan(glows, cell, from, toward))) {
		return 0;
	}
	return drawShare(glows, cell) / (2 * math::pi * fall);
}

std::uint32_t drawnCell(const GlowCells& glows, double part) {
	const double target    = part * glows.running.back();
	const std::size_t last = glows.running.size() - 1;
	const std::size_t cell = std::size_t(std::upper_bound(glows.running.begin(), glows.running.end(), target) - glows.running.begin());
	return std::uint32_t(std::min(cell, last));
}

math::Vector inCone(const math::Vector& axis, double fall, const math::UnitPair& pair) {
	const double drop        = pair.u * fall;
	const double ring        = std::sqrt(std::max(drop * (2 - drop), 0.0));
	const double angle       = 2 * math::pi * pair.v;
	const math::Vector local = { ring * std::cos(angle), ring * std::sin(angle), 1 - drop };
	return math::normalized(math::toWorld(math::basisAround(axis), local));
}

LightAim missedGlow() {
	return LightAim {
		.found    = false,
		.toward   = { 0, 0, 1 },
		.distance = 0,
		.radiance = flatSpectrum(0),
		.raw      = flatSpectrum(0),
		.density  = 0,
		.cell     = noCell,
	};
}

LightAim glowAim(const GlowCells& glows, const GlowDraw& draw) {
	const std::uint32_t cell  = drawnCell(glows, draw.part);
	const math::Vector offset = glows.middles[cell] - draw.from;
	const double distance     = math::length(offset);
	const double radius       = math::length(glows.halves[cell]);
	if (distance <= radius) {
		return missedGlow();
	}
	const double fall         = coneFall(radius, distance);
	const math::Vector toward = inCone(offset / distance, fall, draw.pair);
	const BoxSpan span        = cellSpan(glows, cell, draw.from, toward);
	const double density      = drawShare(glows, cell) / (2 * math::pi * fall);
	if (!spanAhead(span) || !(density > 0)) {
		return missedGlow();
	}
	return LightAim {
		.found    = true,
		.toward   = toward,
		.distance = span.leave,
		.radiance = flatSpectrum(glows.guesses[cell] / density),
		.raw      = flatSpectrum(glows.guesses[cell] / density),
		.density  = density,
		.cell     = cell,
	};
}

double glowDensity(const GlowCells& glows, const GlowSeen& seen) {
	const std::uint32_t cell = cellAt(glows, seen.place);
	return cell == noCell ? 0 : cellDensity(glows, cell, seen.from, seen.toward);
}

double glowCounted(const GlowCells& glows, std::uint32_t cell, const math::Vector& place) {
	return cell != noCell && cellAt(glows, place) == cell ? 1 / glows.guesses[cell] : 0;
}

}
