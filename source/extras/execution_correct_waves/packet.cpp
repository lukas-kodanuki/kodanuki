#include "extras/execution_correct_waves/packet.h"

#include "extras/execution_correct_waves/leaf_lanes.h"
#include "walk/span.h"

#include <array>
#include <bit>
#include <cstdint>
#include <immintrin.h>
#include <simd>

namespace kodanuki {

// The fewest lanes a surface leaf is solved for across lanes; a lone lane
// is solved on its own (a bare shadow lane only asks whether it reaches).
constexpr int verdictsFrom     = 1;
constexpr int bareVerdictsFrom = 2;

struct PacketSpan {
	Lanes near[3];
	Lanes far[3];
	Lanes lower;
	Lanes upper;
};

struct PacketMiddles {
	Lanes at[3];
};

struct PacketCell {
	const PacketSpan& span;
	unsigned lanes;
};

struct PacketBounds {
	Lanes lower[8];
	Lanes upper[8];
	std::uint64_t crossing;
};

struct LaneSlopes {
	alignas(32) double at[3][packetWidth];
};

struct PacketWalker {
	const Geometry& geometry;
	const Packet& packet;
	const WaveTricks& tricks;
	unsigned outside;
	int verdictsFrom;
	bool bare;
	const LaneSlopes& slopes;
};

struct PacketCoarse {
	const PackedNode& node;
	int octant;
};

struct LaneLeaf {
	const SurfaceRun& leaf;
	int lane;
};

[[gnu::always_inline]] inline Lanes largerLanes(Lanes a, Lanes b) {
	return std::simd::select(b < a, a, b);
}

[[gnu::always_inline]] inline Lanes smallerLanes(Lanes a, Lanes b) {
	return std::simd::select(b < a, b, a);
}

[[gnu::always_inline]] inline unsigned belowLanes(Lanes a, Lanes b) {
	return unsigned(_mm256_movemask_ps(std::bit_cast<__m256>(a < b)));
}

[[gnu::always_inline]] inline bool isUpperStep(int step, int axis) {
	return ((step >> axis) & 1) != 0;
}

[[gnu::always_inline]] inline PacketBounds packetBounds(const PacketSpan& span, const PacketMiddles& middles) {
	PacketBounds bounds;
	bounds.lower[0] = span.lower;
	bounds.upper[7] = span.upper;
	for (int step = 1; step < 8; ++step) {
		const int top          = std::bit_width(unsigned(step)) - 1;
		bounds.lower[step]     = largerLanes(bounds.lower[step ^ (1 << top)], middles.at[top]);
		bounds.upper[7 ^ step] = smallerLanes(bounds.upper[7 ^ step ^ (1 << top)], middles.at[top]);
	}
	bounds.crossing = 0;
	for (int step = 0; step < 8; ++step) {
		bounds.crossing |= std::uint64_t(belowLanes(bounds.lower[step], bounds.upper[step])) << (8 * step);
	}
	return bounds;
}

[[gnu::always_inline]] inline unsigned crossingLanes(const PacketBounds& bounds, int step) {
	return unsigned(bounds.crossing >> (8 * step)) & 0xFFu;
}

[[gnu::always_inline]] inline unsigned crossedSteps(const PacketBounds& bounds, unsigned lanes) {
	const std::uint64_t kept = bounds.crossing & (std::uint64_t(lanes) * 0x0101010101010101ull);
	const std::uint64_t high = ((kept & 0x7F7F7F7F7F7F7F7Full) + 0x7F7F7F7F7F7F7F7Full) | kept;
	return unsigned(_pext_u64(high, 0x8080808080808080ull));
}

[[gnu::always_inline]] inline unsigned stepOrder(unsigned octants, int descending) {
	unsigned bits = octants;
	bits          = (descending & 1) != 0 ? ((bits & 0x55u) << 1) | ((bits >> 1) & 0x55u) : bits;
	bits          = (descending & 2) != 0 ? ((bits & 0x33u) << 2) | ((bits >> 2) & 0x33u) : bits;
	bits          = (descending & 4) != 0 ? ((bits & 0x0Fu) << 4) | ((bits >> 4) & 0x0Fu) : bits;
	return bits;
}

[[gnu::always_inline]] inline unsigned occupiedOctants(const PackedNode& node, int octant) {
	return unsigned((node.lowerKinds | node.upperKinds) >> (octant * 8)) & 0xFFu;
}

PacketMet nothingMet() {
	PacketMet met = {
		.lanes = {},
		.found = 0,
	};

	met.lanes.fill(missedMet());
	return met;
}

[[gnu::always_inline]] inline PacketMiddles packetMiddles(const PacketSpan& span) {
	const Lanes half = Lanes(0.5f);
	return PacketMiddles {
		.at = { half * (span.near[0] + span.far[0]), half * (span.near[1] + span.far[1]), half * (span.near[2] + span.far[2]) },
	};
}

[[gnu::always_inline]] inline Lanes blendedLanes(Lanes a, Lanes b, __m256 takeB) {
	return std::bit_cast<Lanes>(_mm256_blendv_ps(std::bit_cast<__m256>(a), std::bit_cast<__m256>(b), takeB));
}

struct ChildPlanes {
	const PacketMiddles& middles;
	const PacketBounds& bounds;
	bool coherent;
};

[[gnu::always_inline]] inline PacketSpan halvedPacket(const PacketSpan& span, const ChildPlanes& planes, int step) {
	PacketSpan half = span;
	for (int axis = 0; axis < 3; ++axis) {
		const bool upper    = isUpperStep(step, axis);
		const __m256 up     = _mm256_castsi256_ps(_mm256_set1_epi32(-int(upper)));
		const Lanes& middle = planes.middles.at[axis];
		half.near[axis]     = planes.coherent ? (upper ? middle : span.near[axis]) : blendedLanes(span.near[axis], middle, up);
		half.far[axis]      = planes.coherent ? (upper ? span.far[axis] : middle) : blendedLanes(middle, span.far[axis], up);
	}
	half.lower = planes.bounds.lower[step];
	half.upper = planes.bounds.upper[step];
	return half;
}

CellSpan laneSpan(const PacketSpan& span, int lane) {
	return CellSpan {
		.near  = { span.near[0][lane], span.near[1][lane], span.near[2][lane] },
		.lower = span.lower[lane],
		.far   = { span.far[0][lane], span.far[1][lane], span.far[2][lane] },
		.upper = span.upper[lane],
	};
}

Walker laneWalker(const PacketWalker& walker, int lane) {
	return Walker {
		.geometry   = walker.geometry,
		.ray        = walker.packet.rays[std::size_t(lane)],
		.descending = walker.packet.descending,
	};
}

void keptMet(PacketMet& met, int lane, const Met& stop) {
	if (stop.kind == MetKind::Missed) {
		return;
	}
	met.lanes[std::size_t(lane)] = stop;
	met.found                    = std::uint8_t(met.found | (1u << lane));
}

void solidLanes(const PacketWalker& walker, std::uint32_t solid, const PacketCell& cell, PacketMet& met) {
	for (unsigned bits = cell.lanes; bits != 0; bits &= bits - 1) {
		const int lane      = std::countr_zero(bits);
		const Walker scalar = laneWalker(walker, lane);
		if (!scalar.ray.inside) {
			keptMet(met, lane, solidMet(scalar, solid, laneSpan(cell.span, lane)));
		}
	}
}

bool isBare(const PacketWalker& walker, const Walker& lane) {
	return walker.tricks.bareShadows && walker.packet.kind == RayKind::Shadow && !lane.ray.inside;
}

Met reachedMet(std::uint32_t surface) {
	return Met {
		.kind     = MetKind::Surface,
		.distance = 0,
		.payload  = surface,
		.entered  = -1,
		.side     = 0,
		.face     = -1,
		.unit     = { 0, 0, 0 },
	};
}

Met bareSurfaceMet(const Walker& lane, const SurfaceRun& leaf, const CellSpan& span) {
	return surfaceReached(lane, leaf, span) ? reachedMet(leaf.surface) : missedMet();
}

Met laneSurfaceMet(const PacketWalker& walker, const LaneLeaf& leaf, const CellSpan& span) {
	const Walker lane = laneWalker(walker, leaf.lane);
	if (isBare(walker, lane)) {
		return bareSurfaceMet(lane, leaf.leaf, span);
	}
	return surfaceRunMet(lane, leaf.leaf, span);
}

LegLanes legLanes(const PacketSpan& span, int descending) {
	LegLanes legs = {
		.base = {},
		.rate = {},
	};

	for (int axis = 0; axis < 3; ++axis) {
		const bool falling  = isUpperStep(descending, axis);
		const Lanes lower   = falling ? span.far[axis] : span.near[axis];
		const Lanes upper   = falling ? span.near[axis] : span.far[axis];
		const Lanes gap     = upper - lower;
		const Lanes slope   = std::simd::select(gap != Lanes(0.0f), laneQuotient(Lanes(1.0f), gap), Lanes(0.0f));
		const Lanes atEntry = (span.lower - lower) * slope;
		legs.base[axis]     = atEntry;
		legs.rate[axis]     = (span.upper - lower) * slope - atEntry;
	}
	return legs;
}

LaneVerdicts settledLanes(const PacketWalker& walker, const LeafRun& run, const PacketCell& cell) {
	if (std::popcount(cell.lanes) < walker.verdictsFrom) {
		return LaneVerdicts {
			.missed = 0,
			.landed = 0,
			.at     = {},
			.face   = {},
		};
	}
	const LegLanes legs = legLanes(cell.span, walker.packet.descending);

	const LeafLanes asked = {
		.run    = run,
		.legs   = legs,
		.lanes  = cell.lanes,
		.inside = cell.lanes & ~walker.outside,
	};

	return laneVerdicts(asked);
}

struct LandedLanes {
	const SurfaceRun& leaf;
	const PacketCell& cell;
	const LaneVerdicts& verdicts;
};

[[gnu::always_inline]] inline __m256d widenedQuad(Lanes lanes, int at) {
	const __m256 all = std::bit_cast<__m256>(lanes);
	return _mm256_cvtps_pd(at == 0 ? _mm256_castps256_ps128(all) : _mm256_extractf128_ps(all, 1));
}

// As landingMet for lanes the verdicts landed, all lanes at once with the
// same bits: the leg is legLanes', the side smallestSide's from the root's
// slopes.
void landedLanes(const PacketWalker& walker, const LandedLanes& landed, unsigned lanes, PacketMet& met) {
	const PacketSpan& span = landed.cell.span;
	const LegLanes legs    = legLanes(span, walker.packet.descending);
	const Lanes found      = std::simd::unchecked_load<Lanes>(landed.verdicts.at.data(), packetWidth);
	const Lanes at         = std::simd::select(found < Lanes(0.0f), Lanes(0.0f), found);
	const __m256d sign     = _mm256_set1_pd(-0.0);
	alignas(32) float unit[3][packetWidth];
	alignas(32) double distance[packetWidth];
	alignas(32) double side[packetWidth];
	for (int axis = 0; axis < 3; ++axis) {
		_mm256_store_ps(unit[axis], std::bit_cast<__m256>(legs.base[axis] + at * legs.rate[axis]));
	}
	for (int half = 0; half < packetWidth; half += 4) {
		const __m256d entered = widenedQuad(span.lower, half);
		_mm256_store_pd(distance + half, _mm256_add_pd(entered, _mm256_mul_pd(widenedQuad(at, half), _mm256_sub_pd(widenedQuad(span.upper, half), entered))));
		__m256d least = _mm256_set1_pd(__builtin_inf());
		for (int axis = 0; axis < 3; ++axis) {
			const __m256d width = _mm256_andnot_pd(sign, _mm256_sub_pd(widenedQuad(span.far[axis], half), widenedQuad(span.near[axis], half)));
			const __m256d slope = _mm256_andnot_pd(sign, _mm256_load_pd(walker.slopes.at[axis] + half));
			least               = _mm256_min_pd(_mm256_div_pd(width, slope), least);
		}
		_mm256_store_pd(side + half, least);
	}
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane      = std::countr_zero(bits);
		const std::size_t k = std::size_t(lane);
		met.lanes[k]        = Met {
			.kind     = MetKind::Surface,
			.distance = distance[k],
			.payload  = landed.leaf.surface,
			.entered  = -1,
			.side     = side[k],
			.face     = landed.verdicts.face[k],
			.unit     = { unit[0][k], unit[1][k], unit[2][k] },
		};
		met.found = std::uint8_t(met.found | (1u << lane));
	}
}

void surfaceLanes(const PacketWalker& walker, std::uint32_t surface, const PacketCell& cell, PacketMet& met) {
	const LeafRun run = surfaceLeafRun(walker.geometry.tree, surface);

	const SurfaceRun leaf = {
		.surface = surface,
		.run     = run,
	};

	const LaneVerdicts verdicts = settledLanes(walker, run, cell);

	const LandedLanes landed = {
		.leaf     = leaf,
		.cell     = cell,
		.verdicts = verdicts,
	};

	const unsigned bare = walker.bare ? verdicts.landed & walker.outside : 0;
	if ((verdicts.landed & ~bare) != 0) {
		landedLanes(walker, landed, verdicts.landed & ~bare, met);
	}
	for (unsigned bits = bare; bits != 0; bits &= bits - 1) {
		keptMet(met, std::countr_zero(bits), reachedMet(surface));
	}
	for (unsigned bits = cell.lanes & ~verdicts.missed & ~verdicts.landed; bits != 0; bits &= bits - 1) {
		const LaneLeaf lane = {
			.leaf = leaf,
			.lane = std::countr_zero(bits),
		};

		keptMet(met, lane.lane, laneSurfaceMet(walker, lane, laneSpan(cell.span, lane.lane)));
	}
}

void packetNode(const PacketWalker& walker, const PackedNode& node, const PacketCell& cell, PacketMet& met);

// A lone lane walks a branch on its own: the scalar walk takes the same
// steps and costs one lane, not eight. Bare shadow packets stay packets.
[[gnu::always_inline]] inline void packetBranch(const PacketWalker& walker, const CellEntry& entry, const PacketCell& cell, PacketMet& met) {
	if (!walker.bare && std::has_single_bit(cell.lanes)) {
		const int lane = std::countr_zero(cell.lanes);
		keptMet(met, lane, walkSlot(laneWalker(walker, lane), entry, laneSpan(cell.span, lane)));
		return;
	}
	packetNode(walker, walker.geometry.tree.nodes[entry.payload], cell, met);
}

[[gnu::always_inline]] inline void packetSlot(const PacketWalker& walker, const CellEntry& entry, const PacketCell& cell, PacketMet& met) {
	switch (entry.kind) {
		case CellKind::Empty: return;
		case CellKind::Filled: solidLanes(walker, entry.payload, cell, met); return;
		case CellKind::Surface: surfaceLanes(walker, entry.payload, cell, met); return;
		case CellKind::Branch: packetBranch(walker, entry, cell, met); return;
	}
}

[[gnu::always_inline]] inline void packetCoarse(const PacketWalker& walker, const PacketCoarse& coarse, const PacketCell& cell, PacketMet& met) {
	const PacketMiddles middles = packetMiddles(cell.span);
	const PacketBounds bounds   = packetBounds(cell.span, middles);

	const ChildPlanes planes = {
		.middles  = middles,
		.bounds   = bounds,
		.coherent = walker.packet.kind == RayKind::Camera,
	};

	for (unsigned steps = stepOrder(occupiedOctants(coarse.node, coarse.octant), walker.packet.descending) & crossedSteps(bounds, cell.lanes); steps != 0; steps &= steps - 1) {
		const unsigned live = cell.lanes & ~unsigned(met.found);
		if (live == 0) {
			return;
		}
		const int step       = std::countr_zero(steps);
		const unsigned lanes = live & crossingLanes(bounds, step);
		if (lanes == 0) {
			continue;
		}

		const PacketSpan span = halvedPacket(cell.span, planes, step);

		const PacketCell fine = {
			.span  = span,
			.lanes = lanes,
		};

		packetSlot(walker, slotEntry(coarse.node, coarse.octant * 8 + (step ^ walker.packet.descending)), fine, met);
	}
}

[[gnu::always_inline]] inline void packetChild(const PacketWalker& walker, const PacketCoarse& coarse, const PacketCell& cell, PacketMet& met) {
	if (((coarse.node.coarse >> coarse.octant) & 1) != 0) {
		packetSlot(walker, slotEntry(coarse.node, coarse.octant * 8), cell, met);
		return;
	}
	packetCoarse(walker, coarse, cell, met);
}

void packetNode(const PacketWalker& walker, const PackedNode& node, const PacketCell& cell, PacketMet& met) {
	const PacketMiddles middles = packetMiddles(cell.span);
	const PacketBounds bounds   = packetBounds(cell.span, middles);

	const ChildPlanes planes = {
		.middles  = middles,
		.bounds   = bounds,
		.coherent = walker.packet.kind == RayKind::Camera,
	};

	for (unsigned steps = stepOrder(node.busy, walker.packet.descending) & crossedSteps(bounds, cell.lanes); steps != 0; steps &= steps - 1) {
		const unsigned live = cell.lanes & ~unsigned(met.found);
		if (live == 0) {
			return;
		}
		const int step       = std::countr_zero(steps);
		const unsigned lanes = live & crossingLanes(bounds, step);
		if (lanes == 0) {
			continue;
		}

		const PacketSpan span = halvedPacket(cell.span, planes, step);

		const PacketCell child = {
			.span  = span,
			.lanes = lanes,
		};

		const PacketCoarse coarse = {
			.node   = node,
			.octant = step ^ walker.packet.descending,
		};

		packetChild(walker, coarse, child, met);
	}
}

struct RootPacket {
	PacketSpan span;
	unsigned lanes;
};

struct RootRays {
	alignas(32) double origin[3][packetWidth];
	alignas(32) double direction[3][packetWidth];
	alignas(32) double far[packetWidth];
};

// As reciprocalOf, four parts at once: a packed division rounds as the
// scalar one does.
[[gnu::always_inline]] inline __m256d reciprocalQuad(__m256d part) {
	const __m256d sign  = _mm256_set1_pd(-0.0);
	const __m256d whole = _mm256_div_pd(_mm256_set1_pd(1.0), part);
	const __m256d least = _mm256_or_pd(_mm256_and_pd(part, sign), _mm256_set1_pd(1.0 / leastPart));
	return _mm256_blendv_pd(least, whole, _mm256_cmp_pd(_mm256_andnot_pd(sign, part), _mm256_set1_pd(leastPart), _CMP_GE_OQ));
}

RootRays rootRays(const Packet& packet) {
	RootRays rays = {};
	for (unsigned bits = packet.present; bits != 0; bits &= bits - 1) {
		const std::size_t k = std::size_t(std::countr_zero(bits));
		const Ray& ray      = packet.rays[k];
		for (int axis = 0; axis < 3; ++axis) {
			rays.origin[axis][k]    = math::component(ray.origin, axis);
			rays.direction[axis][k] = math::component(ray.direction, axis);
		}
		rays.far[k] = ray.far;
	}
	return rays;
}

LaneSlopes laneSlopes(const RootRays& rays) {
	LaneSlopes slopes;
	for (int axis = 0; axis < 3; ++axis) {
		for (int at = 0; at < packetWidth; at += 4) {
			_mm256_store_pd(slopes.at[axis] + at, reciprocalQuad(_mm256_load_pd(rays.direction[axis] + at)));
		}
	}
	return slopes;
}

[[gnu::always_inline]] inline Lanes narrowedLanes(const double* values) {
	return std::bit_cast<Lanes>(_mm256_set_m128(_mm256_cvtpd_ps(_mm256_load_pd(values + 4)), _mm256_cvtpd_ps(_mm256_load_pd(values))));
}

// As rootSpan, eight lanes at once, with the same bits.
RootPacket rootPacket(const PacketWalker& walker, const RootRays& rays) {
	alignas(32) double planes[2][packetWidth];
	PacketSpan span;
	span.lower = Lanes(0.0f);
	span.upper = narrowedLanes(rays.far);
	for (int axis = 0; axis < 3; ++axis) {
		const __m256d start = _mm256_set1_pd(walker.geometry.tree.region.lower[axis]);
		const __m256d end   = _mm256_set1_pd(walker.geometry.tree.region.lower[axis] + walker.geometry.tree.region.size[axis]);
		const bool falling  = isUpperStep(walker.packet.descending, axis);
		for (int at = 0; at < packetWidth; at += 4) {
			const __m256d origin = _mm256_load_pd(rays.origin[axis] + at);
			const __m256d slope  = _mm256_load_pd(walker.slopes.at[axis] + at);
			_mm256_store_pd(planes[0] + at, _mm256_mul_pd(_mm256_sub_pd(start, origin), slope));
			_mm256_store_pd(planes[1] + at, _mm256_mul_pd(_mm256_sub_pd(end, origin), slope));
		}
		span.near[axis] = narrowedLanes(planes[falling ? 1 : 0]);
		span.far[axis]  = narrowedLanes(planes[falling ? 0 : 1]);
		span.lower      = largerLanes(span.lower, span.near[axis]);
		span.upper      = smallerLanes(span.upper, span.far[axis]);
	}
	return RootPacket {
		.span  = span,
		.lanes = walker.packet.present & ~belowLanes(span.upper, span.lower),
	};
}

unsigned outsideLanes(const Packet& packet) {
	unsigned lanes = 0;
	for (int lane = 0; lane < packetWidth; ++lane) {
		lanes |= packet.rays[std::size_t(lane)].inside ? 0u : 1u << lane;
	}
	return lanes;
}

PacketMet walkedPacket(const Geometry& geometry, const Packet& packet, const WaveTricks& tricks) {
	const RootRays rays     = rootRays(packet);
	const LaneSlopes slopes = laneSlopes(rays);

	const PacketWalker walker = {
		.geometry     = geometry,
		.packet       = packet,
		.tricks       = tricks,
		.outside      = outsideLanes(packet),
		.verdictsFrom = !tricks.missedLanes ? packetWidth + 1 : tricks.bareShadows && packet.kind == RayKind::Shadow ? bareVerdictsFrom
																													 : verdictsFrom,
		.bare         = tricks.bareShadows && packet.kind == RayKind::Shadow,
		.slopes       = slopes,
	};

	const RootPacket rooted = rootPacket(walker, rays);

	const PacketCell root = {
		.span  = rooted.span,
		.lanes = rooted.lanes,
	};

	PacketMet met = nothingMet();
	if (root.lanes != 0) {
		packetSlot(walker, geometry.tree.root, root, met);
	}
	return met;
}

WaveTricks waveTricks() {
	return WaveTricks {
		.bareShadows = true,
		.missedLanes = true,
		.groupedHits = true,
	};
}

}
