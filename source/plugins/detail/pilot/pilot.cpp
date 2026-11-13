#include "plugins/detail/pilot/pilot.h"
#include "plugins/geometry/tree/quadric.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace kodanuki {

constexpr int coordinateBits                = 19;
constexpr std::uint64_t coordinateMask      = (std::uint64_t(1) << coordinateBits) - 1;
constexpr int pilotDepth                    = 16;
constexpr double pilotReach                 = 8;
constexpr std::size_t leastWidthSlots       = 1024;
constexpr std::uint64_t keyMultiplier       = 0xFF51AFD7ED558CCDull;
constexpr std::uint64_t keySecondMultiplier = 0xC4CEB9FE1A85EC53ull;

struct CellKey {
	int level;
	std::int64_t at[3];
};

std::uint64_t packedKey(const CellKey& key) {
	const std::uint64_t level = std::uint64_t(key.level + 1) << (3 * coordinateBits);
	return level | std::uint64_t(key.at[0]) << (2 * coordinateBits) | std::uint64_t(key.at[1]) << coordinateBits | std::uint64_t(key.at[2]);
}

CellKey unpackedKey(std::uint64_t key) {
	return CellKey {
		.level = int(key >> (3 * coordinateBits)) - 1,
		.at    = { std::int64_t((key >> (2 * coordinateBits)) & coordinateMask), std::int64_t((key >> coordinateBits) & coordinateMask), std::int64_t(key & coordinateMask) },
	};
}

std::uint64_t mixedKey(std::uint64_t key) {
	key ^= key >> 33;
	key *= keyMultiplier;
	key ^= key >> 33;
	key *= keySecondMultiplier;
	return key ^ (key >> 33);
}

std::size_t keySlot(const std::vector<std::uint64_t>& keys, std::uint64_t key) {
	const std::size_t mask = keys.size() - 1;
	std::size_t slot       = std::size_t(mixedKey(key)) & mask;
	while (keys[slot] != 0 && keys[slot] != key) {
		slot = (slot + 1) & mask;
	}
	return slot;
}

WidthTable emptyWidths() {
	return WidthTable {
		.keys   = {},
		.widths = {},
		.taken  = 0,
	};
}

void widenedTable(WidthTable& table) {
	if (table.taken * 2 < table.keys.size()) {
		return;
	}

	WidthTable wider = {
		.keys   = std::vector<std::uint64_t>(std::max(table.keys.size() * 2, leastWidthSlots), 0),
		.widths = {},
		.taken  = table.taken,
	};

	wider.widths.assign(wider.keys.size(), 0);
	for (std::size_t i = 0; i < table.keys.size(); ++i) {
		if (table.keys[i] != 0) {
			const std::size_t slot = keySlot(wider.keys, table.keys[i]);
			wider.keys[slot]       = table.keys[i];
			wider.widths[slot]     = table.widths[i];
		}
	}
	table = wider;
}

bool narrowedWidth(WidthTable& table, std::uint64_t key, float width) {
	widenedTable(table);
	const std::size_t slot = keySlot(table.keys, key);
	if (table.keys[slot] == 0) {
		table.keys[slot]   = key;
		table.widths[slot] = width;
		table.taken += 1;
		return true;
	}
	if (width < table.widths[slot]) {
		table.widths[slot] = width;
		return true;
	}
	return false;
}

double widthOf(const WidthTable& table, std::uint64_t key) {
	if (table.keys.empty()) {
		return std::numeric_limits<double>::infinity();
	}
	const std::size_t slot = keySlot(table.keys, key);
	return table.keys[slot] == key ? double(table.widths[slot]) : std::numeric_limits<double>::infinity();
}

double levelSide(const Region& region, int level, int axis) {
	return region.size[axis] / double(std::uint64_t(1) << level);
}

std::int64_t indexAlong(const Region& region, int level, const math::Vector& p, int axis) {
	return std::int64_t(std::floor((math::component(p, axis) - region.lower[axis]) / levelSide(region, level, axis)));
}

CellKey keyAt(const Region& region, int level, const math::Vector& p) {
	return CellKey {
		.level = level,
		.at    = { indexAlong(region, level, p, 0), indexAlong(region, level, p, 1), indexAlong(region, level, p, 2) },
	};
}

bool withinLevel(const CellKey& key) {
	const std::int64_t across = std::int64_t(1) << key.level;
	return key.at[0] >= 0 && key.at[1] >= 0 && key.at[2] >= 0 && key.at[0] < across && key.at[1] < across && key.at[2] < across;
}

CellKey besideKey(const CellKey& key, int beside) {
	return CellKey {
		.level = key.level,
		.at    = { key.at[0] + beside % 3 - 1, key.at[1] + beside / 3 % 3 - 1, key.at[2] + beside / 9 - 1 },
	};
}

int needLevel(const PilotInput& input, double floor) {
	const double coarsest = std::floor(std::log2(input.region.size[0] / (pilotReach * floor)));
	return int(std::clamp(coarsest, 0.0, double(pilotDepth)));
}

std::vector<std::uint64_t> heldNeed(Pilot& pilot, const Need& need, int level) {
	const CellKey centre = keyAt(pilot.region, level, need.place);
	pilot.levels |= 1u << level;
	std::vector<std::uint64_t> placed;
	for (int beside = 0; beside < 27; ++beside) {
		const CellKey key = besideKey(centre, beside);
		if (withinLevel(key) && narrowedWidth(pilot.held, packedKey(key), float(need.footprint))) {
			placed.push_back(packedKey(key));
		}
	}
	return placed;
}

CellKey parentKey(const CellKey& key) {
	return CellKey {
		.level = key.level - 1,
		.at    = { key.at[0] >> 1, key.at[1] >> 1, key.at[2] >> 1 },
	};
}

void toldAbove(Pilot& pilot, std::uint64_t key) {
	const float width = float(widthOf(pilot.held, key));
	CellKey at        = unpackedKey(key);
	while (at.level >= 0 && narrowedWidth(pilot.inside, packedKey(at), width)) {
		at = parentKey(at);
	}
}

Pilot unmeasuredPilot(const Region& region) {
	return Pilot {
		.region = region,
		.depth  = pilotDepth,
		.held   = emptyWidths(),
		.inside = emptyWidths(),
		.levels = 0,
	};
}

Pilot pilotOf(const PilotInput& input) {
	Pilot pilot = unmeasuredPilot(input.region);
	std::vector<std::uint64_t> placed;
	for (const Need& need : input.needs) {
		const double floor = footprintAt(input.view, need.place);
		if (need.footprint < floor) {
			const std::vector<std::uint64_t> held = heldNeed(pilot, need, needLevel(input, floor));
			placed.insert(placed.end(), held.begin(), held.end());
		}
	}
	std::sort(placed.begin(), placed.end());
	placed.erase(std::unique(placed.begin(), placed.end()), placed.end());
	for (const std::uint64_t key : placed) {
		toldAbove(pilot, key);
	}
	return pilot;
}

double narrowestAt(const Pilot& pilot, const CellKey& cell) {
	if (pilot.inside.taken == 0 || !withinLevel(cell)) {
		return std::numeric_limits<double>::infinity();
	}
	double narrowest = widthOf(pilot.inside, packedKey(cell));
	for (CellKey above = parentKey(cell); above.level >= 0; above = parentKey(above)) {
		const bool holds = ((pilot.levels >> above.level) & 1u) != 0;
		narrowest        = holds ? std::min(narrowest, widthOf(pilot.held, packedKey(above))) : narrowest;
	}
	return narrowest;
}

double pilotLimit(const Pilot& pilot, const Box& box, double share) {
	const double ratio   = pilot.region.size[0] / (box.axes[0].upper - box.axes[0].lower);
	const int level      = std::min(int(std::lround(std::log2(ratio))), pilot.depth);
	const math::Vector p = { boxCentre(box, 0), boxCentre(box, 1), boxCentre(box, 2) };
	const double narrow  = narrowestAt(pilot, keyAt(pilot.region, level, p));
	return std::isinf(narrow) ? narrow : share * narrow;
}

}
