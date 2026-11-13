#include "plugins/sampler/toward_mirrors/toward_mirrors.h"

#include "plugins/geometry/tree/cut.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace kodanuki {

constexpr double strayMargin  = 4;
constexpr int facedLevels     = 3;
constexpr double leastOfRange = 1e-4;

struct Relaying {
	const Geometry& geometry;
	std::vector<Program> programs;
	std::vector<std::vector<std::uint32_t>> objects;
	std::vector<std::vector<std::uint32_t>> cuts;
	double blend;
	double least;
	double widest;
};

struct Grown {
	std::vector<Box> boxes;
	std::vector<bool> grown;
};

struct RelayCell {
	std::uint32_t leaf;
	double stray;
	int finer;
};

struct RelayCone {
	math::Vector axis;
	double fall;
};

MirrorBalls noMirrorBalls() {
	return MirrorBalls {
		.balls = {},
	};
}

bool sharpOrLaddered(const Looks& looks, const Materials& materials, std::uint32_t material) {
	const bool laddered = sceneRoughens(looks) && looks.materials[material].kind == MaterialKind::Metal;
	return looks.materials[material].relays && (isSharp(materials, material) || laddered);
}

bool mayRelay(const Looks& looks, const Materials& materials, const Leaf& leaf) {
	if ((leaf.rules & std::uint8_t(Rule::Causticless)) != 0) {
		return false;
	}
	if (leaf.coats.empty()) {
		return looks.materials[leaf.material].relays && isSharp(materials, leaf.material);
	}
	if (sharpOrLaddered(looks, materials, leaf.material)) {
		return true;
	}
	for (const std::uint32_t coat : leaf.coats) {
		const Coat& layer = looks.coats[coat];
		if (layer.effect == CoatEffect::Paint && sharpOrLaddered(looks, materials, layer.material)) {
			return true;
		}
	}
	return false;
}

std::vector<std::uint32_t> namedRelays(const std::vector<Step>& steps, const std::vector<bool>& relays) {
	std::vector<std::uint32_t> named;
	for (const Step& step : steps) {
		if (step.leaf != noLeaf && relays[step.leaf]) {
			named.push_back(step.leaf);
		}
	}
	std::sort(named.begin(), named.end());
	named.erase(std::unique(named.begin(), named.end()), named.end());
	return named;
}

std::vector<std::vector<std::uint32_t>> namedByPrograms(const std::vector<Program>& programs, const std::vector<bool>& relays) {
	std::vector<std::vector<std::uint32_t>> named;
	for (const Program& program : programs) {
		named.push_back(namedRelays(program.steps, relays));
	}
	return named;
}

std::vector<std::vector<std::uint32_t>> namedByCuts(const Geometry& geometry, const std::vector<bool>& relays) {
	std::vector<std::vector<std::uint32_t>> named;
	for (std::uint32_t cut = 0; cut < treeOf(geometry).cuts.size(); ++cut) {
		named.push_back(namedRelays(keptStepsOf(geometry.scene, treeOf(geometry), cut), relays));
	}
	return named;
}

double blendRadius(const Scene& scene) {
	double blend = 0;
	for (const Node& node : scene.nodes) {
		blend = std::max(blend, node.radius);
	}
	return blend;
}

double widestStray(const Tree& tree) {
	double widest = 0;
	for (const SurfaceLeaf& leaf : tree.surfaces) {
		widest = std::max(widest, loosenedOf(leaf));
	}
	return widest;
}

double leastTolerance(const Region& region) {
	return leastOfRange * std::max({ region.size[0], region.size[1], region.size[2] });
}

Program noProgram() {
	return Program {
		.name      = {},
		.steps     = {},
		.result    = 0,
		.arguments = 0,
	};
}

std::vector<Program> relayPrograms(const Scene& scene, const std::vector<bool>& relays) {
	std::vector<Program> programs(relays.size(), noProgram());
	for (std::uint32_t leaf = 0; leaf < relays.size(); ++leaf) {
		programs[leaf] = relays[leaf] ? leafProgram(scene, leaf) : programs[leaf];
	}
	return programs;
}

Relaying relayingOf(const Geometry& geometry, const std::vector<bool>& relays) {
	return Relaying {
		.geometry = geometry,
		.programs = relayPrograms(geometry.scene, relays),
		.objects  = namedByPrograms(geometry.scene.objectPrograms, relays),
		.cuts     = namedByCuts(geometry, relays),
		.blend    = blendRadius(geometry.scene),
		.least    = leastTolerance(treeOf(geometry).region),
		.widest   = widestStray(treeOf(geometry)),
	};
}

bool holdsBox(const Box& outer, const Box& inner) {
	for (int axis = 0; axis < 3; ++axis) {
		if (inner.axes[axis].lower < outer.axes[axis].lower || inner.axes[axis].upper > outer.axes[axis].upper) {
			return false;
		}
	}
	return true;
}

Box joinedBox(const Box& a, const Box& b) {
	Box joined = a;
	for (int axis = 0; axis < 3; ++axis) {
		joined.axes[axis].lower = std::min(a.axes[axis].lower, b.axes[axis].lower);
		joined.axes[axis].upper = std::max(a.axes[axis].upper, b.axes[axis].upper);
	}
	return joined;
}

bool nearSurface(const Relaying& relaying, const RelayCell& cell, const Box& box) {
	const Program& program                     = relaying.programs[cell.leaf];
	const std::vector<math::Interval> interval = intervalsOver(program, box);
	const double slack                         = relaying.blend + strayMargin * (relaying.least + cell.stray);
	return interval[program.result].lower <= slack && interval[program.result].upper >= -slack;
}

void growBox(Grown& grown, std::uint32_t leaf, const Box& box) {
	grown.boxes[leaf] = grown.grown[leaf] ? joinedBox(grown.boxes[leaf], box) : box;
	grown.grown[leaf] = true;
}

void growNear(const Relaying& relaying, const RelayCell& cell, const Box& box, Grown& grown) {
	if (grown.grown[cell.leaf] && holdsBox(grown.boxes[cell.leaf], box)) {
		return;
	}
	if (!nearSurface(relaying, cell, box)) {
		return;
	}
	if (cell.finer == 0) {
		growBox(grown, cell.leaf, box);
		return;
	}

	const RelayCell finer = {
		.leaf  = cell.leaf,
		.stray = cell.stray,
		.finer = cell.finer - 1,
	};

	for (int octant = 0; octant < 8; ++octant) {
		growNear(relaying, finer, octantBox(box, octant), grown);
	}
}

std::uint32_t leafList(const Tree& tree, const CellEntry& entry) {
	return entry.kind == CellKind::Surface ? tree.surfaces[entry.payload].programs : tree.solids[entry.payload].programs;
}

RelayCell relayCellOf(const Relaying& relaying, const CellEntry& entry, std::uint32_t leaf) {
	if (entry.kind != CellKind::Surface) {
		return RelayCell {
			.leaf  = leaf,
			.stray = relaying.widest,
			.finer = 0,
		};
	}
	const SurfaceLeaf& surface = treeOf(relaying.geometry).surfaces[entry.payload];
	const bool faced           = runHeader(treeOf(relaying.geometry).runs[surface.run]).patches == 0;
	return RelayCell {
		.leaf  = leaf,
		.stray = loosenedOf(surface),
		.finer = faced ? facedLevels : 0,
	};
}

void growPlaced(const Relaying& relaying, const PlacedLeaf& placed, Grown& grown) {
	const Tree& tree            = treeOf(relaying.geometry);
	const ProgramList& programs = tree.lists[leafList(tree, placed.entry)];
	const Box box               = placedBox(tree.region, placed);
	for (std::uint32_t k = 0; k < programs.count; ++k) {
		const ProgramReference& reference       = tree.references[programs.first + k];
		const std::vector<std::uint32_t>& named = reference.source == ProgramSource::Object ? relaying.objects[reference.index] : relaying.cuts[reference.index];
		for (const std::uint32_t leaf : named) {
			growNear(relaying, relayCellOf(relaying, placed.entry, leaf), box, grown);
		}
	}
}

Box emptyBox() {
	const math::Interval none = {
		.lower = 0,
		.upper = 0,
	};

	return Box {
		.axes = { none, none, none },
	};
}

Ball ballAround(const Box& box) {
	const math::Vector low  = { box.axes[0].lower, box.axes[1].lower, box.axes[2].lower };
	const math::Vector high = { box.axes[0].upper, box.axes[1].upper, box.axes[2].upper };
	return Ball {
		.middle = (low + high) * 0.5,
		.radius = 0.5 * math::length(high - low),
	};
}

MirrorBalls ballsOf(const Grown& grown) {
	MirrorBalls mirrors = noMirrorBalls();
	for (std::size_t leaf = 0; leaf < grown.boxes.size(); ++leaf) {
		if (grown.grown[leaf]) {
			mirrors.balls.push_back(ballAround(grown.boxes[leaf]));
		}
	}
	return mirrors;
}

MirrorBalls relayingBalls(const Geometry& geometry, const Looks& looks, const Materials& materials) {
	std::vector<bool> relays(geometry.scene.leaves.size(), false);
	for (std::uint32_t leaf = 0; leaf < relays.size(); ++leaf) {
		relays[leaf] = mayRelay(looks, materials, geometry.scene.leaves[leaf]);
	}
	if (std::find(relays.begin(), relays.end(), true) == relays.end()) {
		return noMirrorBalls();
	}
	const Relaying relaying = relayingOf(geometry, relays);

	Grown grown = {
		.boxes = std::vector<Box>(relays.size(), emptyBox()),
		.grown = std::vector<bool>(relays.size(), false),
	};

	for (const PlacedLeaf& placed : placedLeaves(treeOf(geometry))) {
		growPlaced(relaying, placed, grown);
	}
	return ballsOf(grown);
}

RelayCone coneOf(const Ball& ball, const math::Vector& origin) {
	const math::Vector apart = ball.middle - origin;
	const double square      = math::dot(apart, apart);
	const double near        = ball.radius * ball.radius;
	if (square <= near) {
		return RelayCone {
			.axis = { 0, 0, 1 },
			.fall = 2,
		};
	}
	const double sideways = near / square;
	return RelayCone {
		.axis = apart / std::sqrt(square),
		.fall = sideways / (1 + std::sqrt(1 - sideways)),
	};
}

bool facesPoint(const RelayCone& cone, const LampPoint& point) {
	const bool round = point.normal.x == 0 && point.normal.y == 0 && point.normal.z == 0;
	return round || cone.fall >= 2 || math::dot(cone.axis, point.normal) >= -std::sqrt(cone.fall * (2 - cone.fall));
}

bool holdsDirection(const RelayCone& cone, const math::Vector& direction) {
	return cone.fall >= 2 || math::dot(cone.axis, direction) >= 1 - cone.fall;
}

double facingAngle(const MirrorBalls& mirrors, const LampPoint& point) {
	double total = 0;
	for (const Ball& ball : mirrors.balls) {
		const RelayCone cone = coneOf(ball, point.point);
		total += facesPoint(cone, point) ? 2 * math::pi * cone.fall : 0;
	}
	return total;
}

RelayCone pickedCone(const MirrorBalls& mirrors, const MirrorDraw& draw, double total) {
	const double choice = draw.pick * total;
	double running      = 0;
	RelayCone picked    = coneOf(mirrors.balls.front(), draw.point.point);
	for (const Ball& ball : mirrors.balls) {
		const RelayCone cone = coneOf(ball, draw.point.point);
		if (!facesPoint(cone, draw.point)) {
			continue;
		}
		picked = cone;
		running += 2 * math::pi * cone.fall;
		if (choice < running) {
			break;
		}
	}
	return picked;
}

math::Vector inMirrorCone(const RelayCone& cone, const math::UnitPair& pair) {
	const double height      = 1 - pair.u * cone.fall;
	const double ring        = std::sqrt(std::max(1 - height * height, 0.0));
	const double angle       = 2 * math::pi * pair.v;
	const math::Vector local = { ring * std::cos(angle), ring * std::sin(angle), height };
	return math::normalized(math::toWorld(math::basisAround(cone.axis), local));
}

MirrorDirection mirrorDirection(const MirrorBalls& mirrors, const MirrorDraw& draw) {
	const double total = facingAngle(mirrors, draw.point);
	if (!(total > 0)) {
		return MirrorDirection {
			.found     = false,
			.direction = { 0, 0, 1 },
		};
	}
	return MirrorDirection {
		.found     = true,
		.direction = inMirrorCone(pickedCone(mirrors, draw, total), draw.pair),
	};
}

double mirrorDensity(const MirrorBalls& mirrors, const MirrorAsk& ask) {
	double total    = 0;
	double covering = 0;
	for (const Ball& ball : mirrors.balls) {
		const RelayCone cone = coneOf(ball, ask.point.point);
		if (!facesPoint(cone, ask.point)) {
			continue;
		}
		total += 2 * math::pi * cone.fall;
		covering += holdsDirection(cone, ask.direction) ? 1 : 0;
	}
	return total > 0 ? covering / total : 0;
}

std::shared_ptr<void> openedMirrorBalls() {
	return std::make_shared<MirrorBalls>(noMirrorBalls());
}

MirrorBalls& mirrorBallsState(States& states) {
	return *static_cast<MirrorBalls*>(heldState(states, "sampler/toward_mirrors", openedMirrorBalls));
}

}
