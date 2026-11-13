#include "plugins/execution/packets/answer.h"

#include "plugins/geometry/tree/hit.h"
#include "plugins/geometry/tree/pass.h"
#include "plugins/geometry/tree/span.h"

#include "sdf/numbers.h"
#include "sdf/program.h"

#include <bit>
#include <cstddef>
#include <vector>

namespace kodanuki {

Hit bareHit(const Met& met) {
	Hit hit  = missedHit();
	hit.kind = met.kind == MetKind::Solid ? HitKind::Solid : HitKind::Surface;
	return hit;
}

constexpr int groupedHitsFrom = 3;
constexpr int dualBatchFrom   = 4;

Walker answerWalker(const Geometry& geometry, const Packet& packet, int lane) {
	return Walker {
		.geometry   = geometry,
		.ray        = packet.rays[std::size_t(lane)],
		.descending = packet.descending,
	};
}

struct SurfaceGroup {
	const Geometry& geometry;
	const Packet& packet;
	const PacketMet& met;
	unsigned lanes;
};

struct GroupPoints {
	std::array<std::array<double, 3>, packetWidth> at;
	Inputs<math::Batch> inputs;
};

LandedFace laneLanded(const Geometry& geometry, const Met& met) {
	return passLandedFace(treeOf(geometry).runs.data() + treeOf(geometry).surfaces[met.payload].run, met.face, nullptr);
}

std::uint32_t laneOwner(const Geometry& geometry, const Met& met) {
	const std::uint32_t owner = laneLanded(geometry, met).owner;
	return owner < treeOf(geometry).lists[treeOf(geometry).surfaces[met.payload].programs].count ? owner : noFaceOwner;
}

bool laneFaced(const Geometry& geometry, const Met& met) {
	return met.kind == MetKind::Surface && laneLanded(geometry, met).exact;
}

GroupPoints groupPoints(const SurfaceGroup& group) {
	GroupPoints points = {
		.at = {},

		.inputs = {
			.arguments = nullptr,
			.p         = {},
			.n         = { math::constantAs<math::Batch>(0), math::constantAs<math::Batch>(0), math::constantAs<math::Batch>(0) },
		},
	};

	const int first = std::countr_zero(group.lanes);
	for (int lane = 0; lane < packetWidth; ++lane) {
		const int from           = ((group.lanes >> lane) & 1) != 0 ? lane : first;
		const Walker walker      = answerWalker(group.geometry, group.packet, from);
		const std::size_t k      = std::size_t(lane);
		points.at[k]             = pointAlong(hitQueryOf(walker, group.met.lanes[std::size_t(from)]));
		points.inputs.p[0].at[k] = points.at[k][0];
		points.inputs.p[1].at[k] = points.at[k][1];
		points.inputs.p[2].at[k] = points.at[k][2];
	}
	return points;
}

const std::array<std::vector<double>, packetWidth>& groupValues(const FieldQuery& query, const GroupPoints& points) {
	thread_local std::vector<math::Batch> registers;
	thread_local std::array<std::vector<double>, packetWidth> values;
	for (std::vector<double>& lane : values) {
		lane.clear();
	}
	const std::uint32_t count = treeOf(query.geometry).lists[query.list].count;
	for (std::uint32_t k = 0; k < count; ++k) {
		const math::Batch value = evaluateKept(listedProgram(query, k), points.inputs, registers);
		for (int lane = 0; lane < packetWidth; ++lane) {
			values[std::size_t(lane)].push_back(value.at[lane]);
		}
	}
	return values;
}

struct GroupHits {
	const SurfaceGroup& group;
	const GroupPoints& points;
	std::uint32_t surface;
	std::uint32_t list;
};

HitQuery laneHitQuery(const SurfaceGroup& group, int lane) {
	return hitQueryOf(answerWalker(group.geometry, group.packet, lane), group.met.lanes[std::size_t(lane)]);
}

Inputs<math::DualBatch> dualInputs(const GroupPoints& points) {
	Inputs<math::DualBatch> inputs = {
		.arguments = nullptr,
		.p         = {},
		.n         = { math::constantAs<math::DualBatch>(0), math::constantAs<math::DualBatch>(0), math::constantAs<math::DualBatch>(0) },
	};

	for (int lane = 0; lane < packetWidth; ++lane) {
		for (int axis = 0; axis < 3; ++axis) {
			inputs.p[axis].at[lane] = math::coordinateAs<math::Dual>(points.at[std::size_t(lane)][std::size_t(axis)], axis);
		}
	}
	return inputs;
}

Sourced laneSourced(const KeptSteps& program, const std::vector<math::DualBatch>& registers, int lane) {
	const math::Dual& value = registers[program.result].at[lane];
	const Source source     = keptBatchSourceOf(program, registers, lane);
	return Sourced {
		.value    = value.value,
		.gradient = { value.gradient[0], value.gradient[1], value.gradient[2] },
		.leaf     = source.leaf,
		.other    = source.other,
		.share    = source.share,
	};
}

void winnerHits(const GroupHits& asked, const NearestProgram& nearest, unsigned lanes, std::array<Hit, packetWidth>& hits) {
	const int first = std::countr_zero(lanes);
	if (std::popcount(lanes) < dualBatchFrom) {
		for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
			const int lane          = std::countr_zero(bits);
			hits[std::size_t(lane)] = nearestSurfaceHit(laneHitQuery(asked.group, lane), nearest);
		}
		return;
	}
	thread_local std::vector<math::DualBatch> registers;
	const HitQuery firstQuery = laneHitQuery(asked.group, first);
	const KeptSteps program   = listedProgram(fieldQuery(firstQuery, asked.list, asked.points.at[std::size_t(first)]), nearest.program);
	evaluateKept(program, dualInputs(asked.points), registers);
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane      = std::countr_zero(bits);
		const Sourced field = laneSourced(program, registers, lane);

		const LeafField read = {
			.surface = asked.surface,
			.field   = field,
		};

		hits[std::size_t(lane)] = leafHit(laneHitQuery(asked.group, lane), read);
	}
}

unsigned ownedLanes(const GroupHits& asked, std::array<std::uint32_t, packetWidth>& winners) {
	unsigned owned = 0;
	for (unsigned bits = asked.group.lanes; bits != 0; bits &= bits - 1) {
		const int lane             = std::countr_zero(bits);
		const std::uint32_t owner  = laneOwner(asked.group.geometry, asked.group.met.lanes[std::size_t(lane)]);
		winners[std::size_t(lane)] = owner;
		owned |= owner != noFaceOwner ? 1u << lane : 0u;
	}
	return owned;
}

std::array<std::uint32_t, packetWidth> laneWinners(const GroupHits& asked) {
	std::array<std::uint32_t, packetWidth> winners = {};
	const int first                                = std::countr_zero(asked.group.lanes);
	const FieldQuery listed                        = fieldQuery(laneHitQuery(asked.group, first), asked.list, asked.points.at[std::size_t(first)]);
	if (treeOf(asked.group.geometry).lists[asked.list].count == 1) {
		return winners;
	}
	const unsigned owned = ownedLanes(asked, winners);
	if (owned == asked.group.lanes) {
		return winners;
	}
	const std::array<std::vector<double>, packetWidth>& values = groupValues(listed, asked.points);
	for (unsigned bits = asked.group.lanes & ~owned; bits != 0; bits &= bits - 1) {
		const int lane       = std::countr_zero(bits);
		const std::size_t k  = std::size_t(lane);
		const HitQuery query = laneHitQuery(asked.group, lane);
		winners[k]           = nearestOfValues(fieldQuery(query, asked.list, asked.points.at[k]), values[k]);
	}
	return winners;
}

unsigned sameWinnerLanes(const std::array<std::uint32_t, packetWidth>& winners, unsigned lanes, std::uint32_t winner) {
	unsigned same = 0;
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane = std::countr_zero(bits);
		same |= winners[std::size_t(lane)] == winner ? 1u << lane : 0u;
	}
	return same;
}

void groupedHits(const SurfaceGroup& group, std::array<Hit, packetWidth>& hits) {
	const std::uint32_t surface = group.met.lanes[std::size_t(std::countr_zero(group.lanes))].payload;
	const GroupPoints points    = groupPoints(group);

	const GroupHits asked = {
		.group   = group,
		.points  = points,
		.surface = surface,
		.list    = treeOf(group.geometry).surfaces[surface].programs,
	};

	const std::array<std::uint32_t, packetWidth> winners = laneWinners(asked);
	for (unsigned left = group.lanes; left != 0;) {
		const std::uint32_t winner = winners[std::size_t(std::countr_zero(left))];
		const unsigned lanes       = sameWinnerLanes(winners, left, winner);

		const NearestProgram nearest = {
			.surface = surface,
			.program = winner,
		};

		winnerHits(asked, nearest, lanes, hits);
		left &= ~lanes;
	}
}

unsigned facedLanes(const Geometry& geometry, const PacketMet& met) {
	unsigned faced = 0;
	for (unsigned bits = met.found; bits != 0; bits &= bits - 1) {
		const int lane = std::countr_zero(bits);
		faced |= laneFaced(geometry, met.lanes[std::size_t(lane)]) ? 1u << lane : 0u;
	}
	return faced;
}

unsigned surfaceLanesOf(const PacketMet& met, unsigned lanes, std::uint32_t surface) {
	unsigned same = 0;
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane = std::countr_zero(bits);
		const Met& at  = met.lanes[std::size_t(lane)];
		same |= at.kind == MetKind::Surface && at.payload == surface ? 1u << lane : 0u;
	}
	return same;
}

bool isGrouped(const Geometry& geometry, std::uint32_t surface, unsigned lanes) {
	return std::popcount(lanes) >= groupedHitsFrom && treeOf(geometry).lists[treeOf(geometry).surfaces[surface].programs].count >= 1;
}

unsigned groupedSurfaces(const Geometry& geometry, const Packet& packet, const PacketMet& met, std::array<Hit, packetWidth>& hits) {
	const unsigned faced = facedLanes(geometry, met);
	unsigned done        = 0;
	for (unsigned bits = met.found & ~faced; bits != 0; bits &= bits - 1) {
		const Met& at = met.lanes[std::size_t(std::countr_zero(bits))];
		if (at.kind != MetKind::Surface || ((done >> std::countr_zero(bits)) & 1) != 0) {
			continue;
		}
		const unsigned lanes = surfaceLanesOf(met, met.found & ~done & ~faced, at.payload);
		if (lanes == 0 || !isGrouped(geometry, at.payload, lanes)) {
			continue;
		}

		const SurfaceGroup group = {
			.geometry = geometry,
			.packet   = packet,
			.met      = met,
			.lanes    = lanes,
		};

		groupedHits(group, hits);
		done |= lanes;
	}
	return done;
}

constexpr std::uint64_t noReference = ~std::uint64_t(0);

std::uint64_t ownedReference(const Geometry& geometry, const Met& met) {
	const SurfaceLeaf& leaf   = treeOf(geometry).surfaces[met.payload];
	const ProgramList& list   = treeOf(geometry).lists[leaf.programs];
	const std::uint32_t owner = list.count == 1 ? 0 : laneOwner(geometry, met);
	if (list.count == 0 || owner == noFaceOwner) {
		return noReference;
	}
	const ProgramReference& reference = treeOf(geometry).references[list.first + owner];
	return (std::uint64_t(reference.index) << 1) | (reference.source == ProgramSource::Cut ? 1u : 0u);
}

std::array<std::uint64_t, packetWidth> laneReferences(const Geometry& geometry, const PacketMet& met) {
	std::array<std::uint64_t, packetWidth> references;
	references.fill(noReference);
	for (unsigned bits = met.found; bits != 0; bits &= bits - 1) {
		const int lane                = std::countr_zero(bits);
		const Met& at                 = met.lanes[std::size_t(lane)];
		references[std::size_t(lane)] = at.kind == MetKind::Surface && !laneFaced(geometry, at) ? ownedReference(geometry, at) : noReference;
	}
	return references;
}

unsigned sameReferenceLanes(const std::array<std::uint64_t, packetWidth>& references, unsigned lanes, std::uint64_t reference) {
	unsigned same = 0;
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane = std::countr_zero(bits);
		same |= references[std::size_t(lane)] == reference ? 1u << lane : 0u;
	}
	return same;
}

void referencedHits(const SurfaceGroup& group, std::array<Hit, packetWidth>& hits) {
	thread_local std::vector<math::DualBatch> registers;
	const int first                   = std::countr_zero(group.lanes);
	const Met& at                     = group.met.lanes[std::size_t(first)];
	const SurfaceLeaf& leaf           = treeOf(group.geometry).surfaces[at.payload];
	const ProgramList& list           = treeOf(group.geometry).lists[leaf.programs];
	const std::uint32_t owner         = list.count == 1 ? 0 : laneOwner(group.geometry, at);
	const ProgramReference& reference = treeOf(group.geometry).references[list.first + owner];
	const KeptSteps program           = referencedSteps(group.geometry.scene, treeOf(group.geometry), reference);
	const GroupPoints points          = groupPoints(group);
	evaluateKept(program, dualInputs(points), registers);
	for (unsigned bits = group.lanes; bits != 0; bits &= bits - 1) {
		const int lane      = std::countr_zero(bits);
		const Sourced field = laneSourced(program, registers, lane);

		const LeafField read = {
			.surface = group.met.lanes[std::size_t(lane)].payload,
			.field   = field,
		};

		hits[std::size_t(lane)] = leafHit(laneHitQuery(group, lane), read);
	}
}

unsigned referencedSurfaces(const Geometry& geometry, const Packet& packet, const PacketMet& met, std::array<Hit, packetWidth>& hits) {
	const std::array<std::uint64_t, packetWidth> references = laneReferences(geometry, met);
	unsigned done                                           = 0;
	for (unsigned bits = met.found; bits != 0; bits &= bits - 1) {
		const int lane                = std::countr_zero(bits);
		const std::uint64_t reference = references[std::size_t(lane)];
		if (reference == noReference || ((done >> lane) & 1) != 0) {
			continue;
		}
		const unsigned lanes = sameReferenceLanes(references, met.found & ~done, reference);
		if (std::popcount(lanes) < dualBatchFrom) {
			continue;
		}

		const SurfaceGroup group = {
			.geometry = geometry,
			.packet   = packet,
			.met      = met,
			.lanes    = lanes,
		};

		referencedHits(group, hits);
		done |= lanes;
	}
	return done;
}

unsigned groupedLanes(const Geometry& geometry, const Packet& packet, const PacketMet& met, std::array<Hit, packetWidth>& hits) {
	const unsigned referenced = referencedSurfaces(geometry, packet, met, hits);
	PacketMet rest            = met;
	rest.found &= ~referenced;
	return referenced | groupedSurfaces(geometry, packet, rest, hits);
}

std::array<Hit, packetWidth> packetHits(const Geometry& geometry, const Packet& packet, const PacketAnswer& answer) {
	std::array<Hit, packetWidth> hits;
	for (unsigned bits = packet.present & ~answer.met.found; bits != 0; bits &= bits - 1) {
		hits[std::size_t(std::countr_zero(bits))] = missedHit();
	}
	const bool bare     = answer.tricks.bareShadows && packet.kind == RayKind::Shadow;
	const bool grouping = answer.tricks.groupedHits && !bare;
	const unsigned done = grouping ? groupedLanes(geometry, packet, answer.met, hits) : 0u;
	for (unsigned bits = answer.met.found & ~done; bits != 0; bits &= bits - 1) {
		const int lane          = std::countr_zero(bits);
		const Met& met          = answer.met.lanes[std::size_t(lane)];
		const Walker walker     = answerWalker(geometry, packet, lane);
		hits[std::size_t(lane)] = bare ? bareHit(met) : hitOf(walker, met);
	}
	return hits;
}

}
