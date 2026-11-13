#include "plugins/execution/packets/leaf_lanes.h"

#include "plugins/geometry/tree/patch.h"
#include "plugins/geometry/tree/tree.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <immintrin.h>

namespace kodanuki {

struct QuarticLanes {
	Lanes fourth;
	Lanes cube;
	Lanes square;
	Lanes linear;
	Lanes constant;
};

struct RootLanes {
	unsigned one;
	unsigned two;
	unsigned unknown;
	Lanes first;
	Lanes second;
};

struct QuadraticLanes {
	Lanes a;
	Lanes b;
	Lanes c;
};

struct FaceLanes {
	QuarticLanes along;
	RootLanes roots;
};

struct LaneFaces {
	unsigned below[8];
	unsigned rooted;
	unsigned unknown;
	unsigned blocked;
};

struct CrossedFaces {
	QuarticLanes along[8];
	RootLanes roots[8];
};

struct LaneLanding {
	bool settled;
	bool found;
	float at;
	int face;
};

struct LaneAnswer {
	const LeafLanes& asked;
	const LaneFaces& faces;
	const unsigned* below;
};

unsigned lessLanes(Lanes a, Lanes b) {
	return unsigned(_mm256_movemask_ps(std::bit_cast<__m256>(a < b)));
}

unsigned atMostLanes(Lanes a, Lanes b) {
	return unsigned(_mm256_movemask_ps(std::bit_cast<__m256>(a <= b)));
}

unsigned unequalLanes(Lanes a, Lanes b) {
	return unsigned(_mm256_movemask_ps(std::bit_cast<__m256>(a != b)));
}

Lanes laneQuotient(Lanes a, Lanes b) {
	return std::bit_cast<Lanes>(_mm256_div_ps(std::bit_cast<__m256>(a), std::bit_cast<__m256>(b)));
}

Lanes laneSqrt(Lanes value) {
	return std::bit_cast<Lanes>(_mm256_sqrt_ps(std::bit_cast<__m256>(value)));
}

Lanes laneAbs(Lanes value) {
	return std::bit_cast<Lanes>(_mm256_andnot_ps(_mm256_set1_ps(-0.0f), std::bit_cast<__m256>(value)));
}

Lanes pickedLanes(unsigned bits, Lanes a, Lanes b) {
	const __m256i lane = _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128);
	const __m256i mask = _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_set1_epi32(int(bits)), lane), lane);
	return std::bit_cast<Lanes>(_mm256_blendv_ps(std::bit_cast<__m256>(b), std::bit_cast<__m256>(a), _mm256_castsi256_ps(mask)));
}

unsigned windowLanes(Lanes root) {
	return atMostLanes(Lanes(-rootSlack), root) & atMostLanes(root, Lanes(1.0f + rootSlack));
}

QuarticLanes quadraticLanes(Lanes a, Lanes b, Lanes c) {
	return QuarticLanes {
		.fourth   = Lanes(0.0f),
		.cube     = Lanes(0.0f),
		.square   = a,
		.linear   = b,
		.constant = c,
	};
}

RootLanes linearRootLanes(Lanes b, Lanes c) {
	const Lanes root = laneQuotient(-c, b);
	return RootLanes {
		.one     = unequalLanes(b, Lanes(0.0f)) & windowLanes(root),
		.two     = 0,
		.unknown = 0,
		.first   = root,
		.second  = root,
	};
}

FaceLanes planeLanes(const float* f, const LegLanes& leg) {
	const Lanes* p = leg.base;
	const Lanes* d = leg.rate;
	const Lanes b  = Lanes(f[0]) * d[0] + Lanes(f[1]) * d[1] + Lanes(f[2]) * d[2];
	const Lanes c  = Lanes(f[0]) * p[0] + Lanes(f[1]) * p[1] + Lanes(f[2]) * p[2] + Lanes(f[3]);
	return FaceLanes {
		.along = quadraticLanes(Lanes(0.0f), b, c),
		.roots = linearRootLanes(b, c),
	};
}

RootLanes quadraticRootLanes(const QuarticLanes& q) {
	const Lanes a            = q.square;
	const Lanes b            = q.linear;
	const Lanes c            = q.constant;
	const unsigned flat      = ~unequalLanes(a, Lanes(0.0f));
	const RootLanes linear   = linearRootLanes(b, c);
	const Lanes discriminant = b * b - Lanes(4.0f) * a * c;
	const Lanes root         = laneSqrt(discriminant);
	const Lanes shared       = Lanes(-0.5f) * (b + std::simd::select(b >= Lanes(0.0f), root, -root));
	const Lanes first        = laneQuotient(shared, a);
	const Lanes second       = std::simd::select(shared != Lanes(0.0f), laneQuotient(c, shared), first);
	const Lanes lower        = std::simd::select(second < first, second, first);
	const Lanes upper        = std::simd::select(first < second, second, first);
	const unsigned real      = ~flat & ~lessLanes(discriminant, Lanes(0.0f));
	const unsigned inLower   = real & windowLanes(lower);
	const unsigned inUpper   = real & windowLanes(upper);
	return RootLanes {
		.one     = (flat & linear.one) | (inLower ^ inUpper),
		.two     = inLower & inUpper,
		.unknown = 0,
		.first   = pickedLanes(flat, linear.first, pickedLanes(inLower, lower, upper)),
		.second  = upper,
	};
}

FaceLanes quadricLanes(const float* f, const LegLanes& leg) {
	const Lanes* p           = leg.base;
	const Lanes* d           = leg.rate;
	const Lanes a            = Lanes(f[0]) * d[0] * d[0] + Lanes(f[1]) * d[1] * d[1] + Lanes(f[2]) * d[2] * d[2] + Lanes(f[3]) * d[0] * d[1] + Lanes(f[4]) * d[0] * d[2] + Lanes(f[5]) * d[1] * d[2];
	const Lanes b            = Lanes(2.0f) * (Lanes(f[0]) * p[0] * d[0] + Lanes(f[1]) * p[1] * d[1] + Lanes(f[2]) * p[2] * d[2]) + Lanes(f[3]) * (p[0] * d[1] + p[1] * d[0]) + Lanes(f[4]) * (p[0] * d[2] + p[2] * d[0]) + Lanes(f[5]) * (p[1] * d[2] + p[2] * d[1]) + Lanes(f[6]) * d[0] + Lanes(f[7]) * d[1] + Lanes(f[8]) * d[2];
	const Lanes c            = Lanes(f[0]) * p[0] * p[0] + Lanes(f[1]) * p[1] * p[1] + Lanes(f[2]) * p[2] * p[2] + Lanes(f[3]) * p[0] * p[1] + Lanes(f[4]) * p[0] * p[2] + Lanes(f[5]) * p[1] * p[2] + Lanes(f[6]) * p[0] + Lanes(f[7]) * p[1] + Lanes(f[8]) * p[2] + Lanes(f[9]);
	const QuarticLanes along = quadraticLanes(a, b, c);
	return FaceLanes {
		.along = along,
		.roots = quadraticRootLanes(along),
	};
}

QuarticLanes ringQuarticLanes(const float* ring, const LegLanes& leg) {
	Lanes p[3];
	Lanes d[3];
	for (int k = 0; k < 3; ++k) {
		p[k] = Lanes(ring[k]) + Lanes(ring[3 + k]) * leg.base[0] + Lanes(ring[6 + k]) * leg.base[1] + Lanes(ring[9 + k]) * leg.base[2];
		d[k] = Lanes(ring[3 + k]) * leg.rate[0] + Lanes(ring[6 + k]) * leg.rate[1] + Lanes(ring[9 + k]) * leg.rate[2];
	}
	const float major = ring[12];
	const float minor = ring[13];
	const Lanes dd    = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	const Lanes pd    = Lanes(2.0f) * (p[0] * d[0] + p[1] * d[1] + p[2] * d[2]);
	const Lanes pp    = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
	const Lanes q0    = pp + Lanes(major * major) - Lanes(minor * minor);
	const Lanes r2    = dd - d[2] * d[2];
	const Lanes r1    = pd - Lanes(2.0f) * p[2] * d[2];
	const Lanes r0    = pp - p[2] * p[2];
	const Lanes m     = Lanes(4.0f * major * major);
	const Lanes sign  = Lanes(minor < 0.0f ? -1.0f : 1.0f);

	return QuarticLanes {
		.fourth   = sign * (dd * dd),
		.cube     = sign * (Lanes(2.0f) * dd * pd),
		.square   = sign * (pd * pd + Lanes(2.0f) * dd * q0 - m * r2),
		.linear   = sign * (Lanes(2.0f) * pd * q0 - m * r1),
		.constant = sign * (q0 * q0 - m * r0),
	};
}

Lanes quarticLaneValue(const QuarticLanes& q, Lanes s) {
	return (((q.fourth * s + q.cube) * s + q.square) * s + q.linear) * s + q.constant;
}

QuarticLanes windowQuarticLanes(const QuarticLanes& q) {
	const Lanes l      = Lanes(-rootSlack);
	const Lanes w      = Lanes((1.0f + rootSlack) - -rootSlack);
	const Lanes square = w * w;

	const QuarticLanes shifted = {
		.fourth   = q.fourth,
		.cube     = q.cube + Lanes(4.0f) * q.fourth * l,
		.square   = q.square + (Lanes(3.0f) * q.cube + Lanes(6.0f) * q.fourth * l) * l,
		.linear   = q.linear + (Lanes(2.0f) * q.square + (Lanes(3.0f) * q.cube + Lanes(4.0f) * q.fourth * l) * l) * l,
		.constant = quarticLaneValue(q, l),
	};

	return QuarticLanes {
		.fourth   = shifted.fourth * (square * square),
		.cube     = shifted.cube * (square * w),
		.square   = shifted.square * square,
		.linear   = shifted.linear * w,
		.constant = shifted.constant,
	};
}

unsigned oneSignLanes(const QuarticLanes& q) {
	const QuarticLanes d = windowQuarticLanes(q);
	const Lanes slack    = Lanes(signSlack) * ((laneAbs(q.fourth) + laneAbs(q.cube)) + (laneAbs(q.square) + laneAbs(q.linear)) + laneAbs(q.constant));

	const Lanes b[5] = {
		d.constant,
		d.constant + Lanes(0.25f) * d.linear,
		d.constant + Lanes(0.5f) * d.linear + laneQuotient(d.square, Lanes(6.0f)),
		d.constant + Lanes(0.75f) * d.linear + Lanes(0.5f) * d.square + Lanes(0.25f) * d.cube,
		d.constant + d.linear + d.square + d.cube + d.fourth,
	};

	unsigned above = ~0u;
	unsigned below = ~0u;
	for (const Lanes& coefficient : b) {
		above &= lessLanes(slack, coefficient);
		below &= lessLanes(coefficient, -slack);
	}
	return above | below;
}

RootLanes unknownRoots(unsigned lanes) {
	return RootLanes {
		.one     = 0,
		.two     = 0,
		.unknown = lanes,
		.first   = Lanes(0.0f),
		.second  = Lanes(0.0f),
	};
}

FaceLanes quarticFaceLanes(const QuarticLanes& quartic) {
	return FaceLanes {
		.along = quartic,
		.roots = unknownRoots(~oneSignLanes(quartic)),
	};
}

QuarticLanes trilinearLanes(const float* c, const Lanes* u, const Lanes* d) {
	const Lanes p0      = Lanes(c[0]) + Lanes(c[1]) * u[0];
	const Lanes p1      = Lanes(c[1]) * d[0];
	const Lanes r0      = Lanes(c[4]) + Lanes(c[5]) * u[0];
	const Lanes r1      = Lanes(c[5]) * d[0];
	const Lanes q0      = Lanes(c[2]) + Lanes(c[3]) * u[0];
	const Lanes q1      = Lanes(c[3]) * d[0];
	const Lanes s0      = Lanes(c[6]) + Lanes(c[7]) * u[0];
	const Lanes s1      = Lanes(c[7]) * d[0];
	const Lanes first0  = p0 + q0 * u[1];
	const Lanes first1  = (p1 + q0 * d[1]) + q1 * u[1];
	const Lanes first2  = q1 * d[1];
	const Lanes second0 = r0 + s0 * u[1];
	const Lanes second1 = (r1 + s0 * d[1]) + s1 * u[1];
	const Lanes second2 = s1 * d[1];
	return QuarticLanes {
		.fourth   = Lanes(0.0f),
		.cube     = second2 * d[2],
		.square   = (first2 + second1 * d[2]) + second2 * u[2],
		.linear   = (first1 + second0 * d[2]) + second1 * u[2],
		.constant = first0 + second0 * u[2],
	};
}

QuadraticLanes squaredAxisLanes(Lanes base, Lanes rate) {
	return QuadraticLanes {
		.a = rate * rate,
		.b = Lanes(2.0f) * base * rate,
		.c = base * base,
	};
}

QuadraticLanes bilinearLanes(const float* w, const Lanes* first, const Lanes* second) {
	return QuadraticLanes {
		.a = Lanes(w[3]) * first[1] * second[1],
		.b = (Lanes(w[1]) * first[1] + Lanes(w[2]) * second[1]) + Lanes(w[3]) * (first[0] * second[1] + second[0] * first[1]),
		.c = (Lanes(w[0]) + Lanes(w[1]) * first[0] + Lanes(w[2]) * second[0]) + Lanes(w[3]) * first[0] * second[0],
	};
}

void addProductLanes(QuarticLanes& into, const QuadraticLanes& left, const QuadraticLanes& right) {
	into.fourth += left.a * right.a;
	into.cube += left.a * right.b + left.b * right.a;
	into.square += (left.a * right.c + left.c * right.a) + left.b * right.b;
	into.linear += left.b * right.c + left.c * right.b;
	into.constant += left.c * right.c;
}

QuarticLanes patchQuarticLanes(const Patch& patch, const LegLanes& leg) {
	const Lanes* u       = leg.base;
	const Lanes* d       = leg.rate;
	QuarticLanes quartic = trilinearLanes(patch.weights, u, d);
	if (!patch.bent) {
		return quartic;
	}
	const Lanes x[2] = { u[0], d[0] };
	const Lanes y[2] = { u[1], d[1] };
	const Lanes z[2] = { u[2], d[2] };
	addProductLanes(quartic, squaredAxisLanes(u[0], d[0]), bilinearLanes(patch.weights + 8, y, z));
	addProductLanes(quartic, squaredAxisLanes(u[1], d[1]), bilinearLanes(patch.weights + 12, x, z));
	addProductLanes(quartic, squaredAxisLanes(u[2], d[2]), bilinearLanes(patch.weights + 16, x, y));
	return quartic;
}

FaceLanes faceLanes(const LeafRun& run, std::uint32_t face, const LegLanes& legs) {
	const float* floats = reinterpret_cast<const float*>(run.words + run.offsets[face]);
	switch (faceKind(run.header, face)) {
		case FaceKind::Plane: return planeLanes(floats, legs);
		case FaceKind::Quadric: return quadricLanes(floats, legs);
		case FaceKind::Ring: return quarticFaceLanes(ringQuarticLanes(floats, legs));
		case FaceKind::Patch: return quarticFaceLanes(patchQuarticLanes(readPatch(run.words + run.offsets[face]), legs));
	}

	return FaceLanes {
		.along = quadraticLanes(Lanes(0.0f), Lanes(0.0f), Lanes(1.0f)),
		.roots = unknownRoots(~0u),
	};
}

unsigned belowLanes(const QuarticLanes& along, Lanes s) {
	return lessLanes(quarticLaneValue(along, s), Lanes(0.0f));
}

void addedFace(LaneFaces& faces, const FaceLanes& face, std::uint32_t index, bool needed) {
	const RootLanes& roots = face.roots;
	const unsigned below   = belowLanes(face.along, Lanes(0.5f));
	const unsigned none    = ~(roots.one | roots.two | roots.unknown);
	faces.below[index]     = below;
	faces.rooted |= roots.one | roots.two;
	faces.unknown |= roots.unknown;
	faces.blocked |= needed ? none & ~below : 0u;
}

LaneFaces laneFaces(const LeafLanes& asked, CrossedFaces& crossed) {
	LaneFaces faces = {
		.below   = {},
		.rooted  = 0,
		.unknown = 0,
		.blocked = 0,
	};

	for (std::uint32_t face = 0; face < asked.run.header.count; ++face) {
		const FaceLanes lanesOfFace = faceLanes(asked.run, face, asked.legs);
		crossed.along[face]         = lanesOfFace.along;
		crossed.roots[face]         = lanesOfFace.roots;
		addedFace(faces, lanesOfFace, face, ((asked.run.header.needed >> face) & 1) != 0);
	}
	return faces;
}

std::uint32_t laneFaceBits(const unsigned* below, std::uint32_t count, int lane) {
	std::uint32_t bits = 0;
	for (std::uint32_t face = 0; face < count; ++face) {
		bits |= ((below[face] >> lane) & 1) << face;
	}
	return bits;
}

LaneVerdicts noVerdicts() {
	return LaneVerdicts {
		.missed = 0,
		.landed = 0,
		.at     = {},
		.face   = {},
	};
}

void settledLane(LaneVerdicts& verdicts, int lane, const LaneLanding& landing) {
	const unsigned bit = 1u << lane;
	if (!landing.settled) {
		return;
	}
	if (landing.found) {
		verdicts.landed |= bit;
		verdicts.at[std::size_t(lane)]   = landing.at;
		verdicts.face[std::size_t(lane)] = landing.face;
		return;
	}
	verdicts.missed |= bit;
}

LaneLanding missedLanding() {
	return LaneLanding {
		.settled = true,
		.found   = false,
		.at      = 0,
		.face    = -1,
	};
}

LaneLanding uncrossedLanding(const LeafRun& run, std::uint32_t signs, bool inside) {
	return LaneLanding {
		.settled = true,
		.found   = runInside(run, signs) != inside,
		.at      = 0,
		.face    = -1,
	};
}

void uncrossedSingleLanes(const LaneAnswer& answer, unsigned lanes, LaneVerdicts& verdicts) {
	const unsigned found = lanes & (answer.below[0] ^ answer.asked.inside);
	for (unsigned bits = found; bits != 0; bits &= bits - 1) {
		const std::size_t lane = std::size_t(std::countr_zero(bits));
		verdicts.at[lane]      = 0;
		verdicts.face[lane]    = -1;
	}
	verdicts.landed |= found;
	verdicts.missed |= lanes & ~found;
}

void uncrossedLanes(const LaneAnswer& answer, unsigned lanes, LaneVerdicts& verdicts) {
	if (answer.asked.run.header.count == 1) {
		uncrossedSingleLanes(answer, lanes, verdicts);
		return;
	}
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane    = std::countr_zero(bits);
		const bool inside = ((answer.asked.inside >> lane) & 1) != 0;
		settledLane(verdicts, lane, uncrossedLanding(answer.asked.run, laneFaceBits(answer.below, answer.asked.run.header.count, lane), inside));
	}
}

void addedRoot(Crossings& crossings, float at, std::uint32_t face) {
	crossings.items[std::size_t(crossings.count)] = Crossing {
		.at   = at,
		.face = face,
	};

	crossings.count += 1;
}

struct LaneAsk {
	const CrossedFaces& faces;
	const std::array<WindowRoots, packetWidth>* solved;
	std::uint32_t together;
	const RunHeader& header;
	int lane;
	bool inside;
};

Quartic laneQuartic(const QuarticLanes& along, int lane) {
	return Quartic {
		.fourth   = along.fourth[lane],
		.cube     = along.cube[lane],
		.square   = along.square[lane],
		.linear   = along.linear[lane],
		.constant = along.constant[lane],
	};
}

unsigned sameLanes(Lanes a, Lanes b) {
	return unsigned(_mm256_movemask_ps(std::bit_cast<__m256>(a == b)));
}

Lanes laneCopysign(Lanes magnitude, Lanes sign) {
	const __m256 bit = _mm256_set1_ps(-0.0f);
	return std::bit_cast<Lanes>(_mm256_or_ps(_mm256_andnot_ps(bit, std::bit_cast<__m256>(magnitude)), _mm256_and_ps(bit, std::bit_cast<__m256>(sign))));
}

Lanes cubicLaneValue(const QuarticLanes& slope, Lanes s) {
	return ((slope.cube * s + slope.square) * s + slope.linear) * s + slope.constant;
}

Lanes cubicLaneSlope(const QuarticLanes& slope, Lanes s) {
	return (Lanes(3.0f) * slope.cube * s + Lanes(2.0f) * slope.square) * s + slope.linear;
}

Lanes quarticLaneSlope(const QuarticLanes& q, Lanes s) {
	return ((Lanes(4.0f) * q.fourth * s + Lanes(3.0f) * q.cube) * s + Lanes(2.0f) * q.square) * s + q.linear;
}

struct BracketLanes {
	Lanes lower;
	Lanes upper;
	Lanes atLower;
};

using LaneMask = Lanes::mask_type;

LaneMask maskOf(unsigned bits) {
	const __m256i lane = _mm256_setr_epi32(1, 2, 4, 8, 16, 32, 64, 128);
	return std::bit_cast<LaneMask>(_mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(_mm256_set1_epi32(int(bits)), lane), lane)));
}

bool anyLane(LaneMask mask) {
	return _mm256_movemask_ps(std::bit_cast<__m256>(mask)) != 0;
}

Lanes settledLanes(const QuarticLanes& q, bool cubic, unsigned lanes, BracketLanes bracket) {
	const int steps      = cubic ? 12 : 40;
	const LaneMask below = bracket.atLower < Lanes(0.0f);
	Lanes s              = Lanes(0.5f) * (bracket.lower + bracket.upper);
	Lanes settled        = s;
	LaneMask open        = maskOf(lanes);
	for (int step = 0; step < steps && anyLane(open); ++step) {
		const Lanes here       = cubic ? cubicLaneValue(q, s) : quarticLaneValue(q, s);
		const LaneMask zero    = open && (here == Lanes(0.0f));
		settled                = std::simd::select(zero, s, settled);
		open                   = open && !zero;
		const LaneMask same    = (here < Lanes(0.0f)) == below;
		bracket.lower          = std::simd::select(same, s, bracket.lower);
		bracket.upper          = std::simd::select(same, bracket.upper, s);
		const Lanes derivative = cubic ? cubicLaneSlope(q, s) : quarticLaneSlope(q, s);
		const Lanes middle     = Lanes(0.5f) * (bracket.lower + bracket.upper);
		const Lanes newton     = std::simd::select(derivative != Lanes(0.0f), s - laneQuotient(here, derivative), middle);
		const Lanes next       = std::simd::select((bracket.lower < newton) && (newton < bracket.upper), newton, middle);
		const Lanes size       = laneAbs(s);
		const LaneMask near    = open && (laneAbs(next - s) <= Lanes(1e-6f) * std::simd::select(Lanes(1.0f) < size, size, Lanes(1.0f)));
		settled                = std::simd::select(near, next, settled);
		open                   = open && !near;
		s                      = next;
	}
	return std::simd::select(open, s, settled);
}

struct ListLanes {
	Lanes at[5];
	unsigned count[5];
};

void appendedLanes(ListLanes& list, unsigned lanes, Lanes at, int first) {
	for (int k = 3; k >= 0; --k) {
		const unsigned exactly = lanes & list.count[k] & ~list.count[k + 1];
		list.at[first + k]     = pickedLanes(exactly, at, list.at[first + k]);
		list.count[k + 1] |= exactly;
	}
}

ListLanes emptyList(Lanes fill) {
	return ListLanes {
		.at    = { fill, fill, fill, fill, fill },
		.count = { ~0u, 0, 0, 0, 0 },
	};
}

void turnedLanes(const QuarticLanes& q, unsigned lanes, std::array<WindowRoots, packetWidth>& out) {
	const Lanes lower = Lanes(-rootSlack);
	const Lanes upper = Lanes(1.0f + rootSlack);

	const QuarticLanes slope = {
		.fourth   = Lanes(0.0f),
		.cube     = Lanes(4.0f) * q.fourth,
		.square   = Lanes(3.0f) * q.cube,
		.linear   = Lanes(2.0f) * q.square,
		.constant = q.linear,
	};

	const Lanes a            = Lanes(3.0f) * slope.cube;
	const Lanes b            = Lanes(2.0f) * slope.square;
	const Lanes c            = slope.linear;
	const Lanes discriminant = b * b - Lanes(4.0f) * a * c;
	const Lanes shared       = Lanes(-0.5f) * (b + laneCopysign(laneSqrt(discriminant), b));
	const Lanes first        = laneQuotient(shared, a);
	const Lanes second       = pickedLanes(~sameLanes(shared, Lanes(0.0f)), laneQuotient(c, shared), first);
	const Lanes least        = pickedLanes(lessLanes(second, first), second, first);
	const Lanes most         = pickedLanes(lessLanes(first, second), second, first);
	const unsigned turning   = lessLanes(Lanes(0.0f), discriminant);
	ListLanes inner          = emptyList(upper);
	appendedLanes(inner, turning & lessLanes(lower, least) & lessLanes(least, upper), least, 0);
	appendedLanes(inner, turning & lessLanes(lower, most) & lessLanes(most, upper), most, 0);
	const Lanes cubicBreaks[4] = { lower, inner.at[0], inner.at[1], upper };
	ListLanes turns            = emptyList(upper);
	for (int k = 0; k < 3; ++k) {
		const Lanes from     = cubicBreaks[k];
		const Lanes to       = cubicBreaks[k + 1];
		const Lanes here     = cubicLaneValue(slope, from);
		const Lanes there    = cubicLaneValue(slope, to);
		const unsigned piece = lanes & inner.count[k];
		const unsigned zero  = piece & sameLanes(here, Lanes(0.0f));
		const unsigned cross = piece & ~zero & (lessLanes(here, Lanes(0.0f)) ^ lessLanes(there, Lanes(0.0f)));
		appendedLanes(turns, zero, from, 1);
		if (cross != 0) {
			const BracketLanes bracket = {
				.lower   = from,
				.upper   = to,
				.atLower = here,
			};
			const Lanes root = settledLanes(slope, true, cross, bracket);
			appendedLanes(turns, cross, root, 1);
		}
	}
	turns.at[0]     = lower;
	ListLanes roots = emptyList(Lanes(0.0f));
	for (int k = 0; k < 4; ++k) {
		const unsigned piece = lanes & turns.count[k];
		if (piece == 0) {
			break;
		}
		const Lanes from     = turns.at[k];
		const Lanes to       = turns.at[k + 1];
		const Lanes here     = quarticLaneValue(q, from);
		const Lanes there    = quarticLaneValue(q, to);
		const unsigned cross = piece & ~sameLanes(here, Lanes(0.0f)) & (lessLanes(here, Lanes(0.0f)) ^ lessLanes(there, Lanes(0.0f)));
		if (cross != 0) {
			const BracketLanes bracket = {
				.lower   = from,
				.upper   = to,
				.atLower = here,
			};
			const Lanes root = settledLanes(q, false, cross, bracket);
			appendedLanes(roots, cross & atMostLanes(lower, root) & atMostLanes(root, upper), root, 0);
		}
	}
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane     = std::countr_zero(bits);
		WindowRoots& found = out[std::size_t(lane)];
		found.count        = 0;
		for (int k = 1; k <= 4; ++k) {
			found.count += int((roots.count[k] >> lane) & 1);
		}
		for (int k = 0; k < 4; ++k) {
			found.at[k] = k < found.count ? float(roots.at[k][lane]) : 0.0f;
		}
	}
}

bool tinyLane(float coefficient) {
	return double(std::abs(coefficient)) <= 1e-12;
}

void solvedLanes(const QuarticLanes& q, unsigned lanes, std::array<WindowRoots, packetWidth>& out) {
	unsigned scalar = 0;
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane    = std::countr_zero(bits);
		const float cube  = 4.0f * float(q.fourth[lane]);
		const bool narrow = tinyLane(cube) || tinyLane(3.0f * cube);
		scalar |= narrow ? 1u << lane : 0u;
	}
	for (unsigned bits = scalar; bits != 0; bits &= bits - 1) {
		const int lane         = std::countr_zero(bits);
		out[std::size_t(lane)] = quarticWindowRoots(laneQuartic(q, lane));
	}
	if ((lanes & ~scalar) != 0) {
		turnedLanes(q, lanes & ~scalar, out);
	}
}

void solvedLaneFace(const LaneAsk& ask, std::uint32_t face, Crossings& crossings) {
	const Quartic quartic   = laneQuartic(ask.faces.along[face], ask.lane);
	const WindowRoots roots = ((ask.together >> face) & 1) != 0 ? ask.solved[face][std::size_t(ask.lane)] : quarticWindowRoots(quartic);
	const bool needed       = !ask.inside && ((ask.header.needed >> face) & 1) != 0;
	crossings.blocked       = crossings.blocked || (needed && roots.count == 0 && !(quarticValue(quartic, 0.5f) < 0.0f));
	for (int k = 0; k < roots.count; ++k) {
		addedRoot(crossings, roots.at[k], face);
	}
}

void laneCrossings(const LaneAsk& ask, Crossings& crossings) {
	const int lane    = ask.lane;
	crossings.count   = 0;
	crossings.blocked = false;
	for (std::uint32_t face = 0; face < ask.header.count; ++face) {
		const RootLanes& roots = ask.faces.roots[face];
		if (((roots.one >> lane) & 1) != 0) {
			addedRoot(crossings, roots.first[lane], face);
		}
		if (((roots.two >> lane) & 1) != 0) {
			addedRoot(crossings, roots.first[lane], face);
			addedRoot(crossings, roots.second[lane], face);
		}
		if (((roots.unknown >> lane) & 1) != 0) {
			solvedLaneFace(ask, face, crossings);
		}
	}
	sortCrossings(crossings);
}

struct CrossedLane {
	const Crossings& crossings;
	std::uint32_t bits;
	bool inside;
};

LaneLanding crossedLanding(const LeafRun& run, const CrossedLane& lane) {
	if (runInside(run, lane.bits) == !lane.inside) {
		return uncrossedLanding(run, lane.bits, lane.inside);
	}

	const LeafVisit visit = {
		.run = run,
		.leg = Leg {
			.base = { 0, 0, 0 },
			.rate = { 0, 0, 0 },
		},
		.inside = lane.inside,
	};

	const Landing landing = lane.inside ? leftCrossings(visit, lane.crossings, lane.bits) : walkedCrossings(visit, lane.crossings, lane.bits);
	return LaneLanding {
		.settled = landing.passings.count == 0,
		.found   = landing.found,
		.at      = landing.at,
		.face    = landing.face,
	};
}

struct CrossedAsk {
	const LeafLanes& asked;
	const CrossedFaces& faces;
};

struct GapLanes {
	unsigned widest;
	unsigned odd;
	Lanes middle;
};

Lanes laneMiddle(Lanes lower, Lanes upper) {
	return lower + laneQuotient(upper - lower, Lanes(2.0f));
}

GapLanes oneCrossingGaps(Lanes first) {
	const unsigned later = lessLanes(first - Lanes(0.0f), Lanes(1.0f) - first);
	return GapLanes {
		.widest = later,
		.odd    = later,
		.middle = pickedLanes(later, laneMiddle(first, Lanes(1.0f)), laneMiddle(Lanes(0.0f), first)),
	};
}

GapLanes twoCrossingGaps(Lanes first, Lanes second) {
	const Lanes before    = first - Lanes(0.0f);
	const Lanes between   = second - first;
	const unsigned middle = lessLanes(before, between);
	const Lanes widest    = pickedLanes(middle, between, before);
	const unsigned after  = lessLanes(widest, Lanes(1.0f) - second);
	const Lanes kept      = pickedLanes(middle, laneMiddle(first, second), laneMiddle(Lanes(0.0f), first));
	return GapLanes {
		.widest = after,
		.odd    = middle & ~after,
		.middle = pickedLanes(after, laneMiddle(second, Lanes(1.0f)), kept),
	};
}

void landedLanes(LaneVerdicts& verdicts, unsigned lanes, unsigned started, Lanes at) {
	const Lanes clamped = std::simd::select(at < Lanes(0.0f), Lanes(0.0f), at);
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane                   = std::countr_zero(bits);
		const bool crossed               = ((started >> lane) & 1) == 0;
		verdicts.at[std::size_t(lane)]   = crossed ? float(clamped[lane]) : 0.0f;
		verdicts.face[std::size_t(lane)] = crossed ? 0 : -1;
	}
	verdicts.landed |= lanes;
}

unsigned singleFaceLanes(const CrossedAsk& crossedAsk, unsigned lanes, LaneVerdicts& verdicts) {
	const RootLanes& roots = crossedAsk.faces.roots[0];
	const unsigned solved  = lanes & ~crossedAsk.asked.inside & ~roots.unknown;
	if (crossedAsk.asked.run.header.count != 1 || solved == 0) {
		return 0;
	}
	const GapLanes one     = oneCrossingGaps(roots.first);
	const GapLanes two     = twoCrossingGaps(roots.first, roots.second);
	const Lanes middle     = pickedLanes(roots.two, two.middle, one.middle);
	const unsigned odd     = (roots.two & two.odd) | (~roots.two & one.odd);
	const unsigned started = belowLanes(crossedAsk.faces.along[0], middle) ^ odd;
	landedLanes(verdicts, solved, started, roots.first);
	return solved;
}

[[gnu::noinline]] void solvedFaceLanes(const CrossedFaces& faces, std::uint32_t together, unsigned lanes, std::array<std::array<WindowRoots, packetWidth>, 8>& solved) {
	for (std::uint32_t bits = together; bits != 0; bits &= bits - 1) {
		const int face = std::countr_zero(bits);
		solvedLanes(faces.along[face], lanes & faces.roots[face].unknown, solved[std::size_t(face)]);
	}
}

void crossedLanes(const CrossedAsk& crossedAsk, unsigned lanes, LaneVerdicts& verdicts) {
	if (lanes == 0) {
		return;
	}
	const LeafLanes& asked    = crossedAsk.asked;
	const CrossedFaces& faces = crossedAsk.faces;
	const std::uint32_t count = asked.run.header.count;
	lanes &= ~singleFaceLanes(crossedAsk, lanes, verdicts);
	std::array<Crossings, packetWidth> crossings;
	std::array<std::array<WindowRoots, packetWidth>, 8> solved;
	std::uint32_t together = 0;
	for (std::uint32_t face = 0; face < count; ++face) {
		together |= std::popcount(lanes & faces.roots[face].unknown) >= 2 ? 1u << face : 0u;
	}
	if (together != 0) {
		solvedFaceLanes(faces, together, lanes, solved);
	}
	std::array<int, packetWidth> gaps      = {};
	std::array<float, packetWidth> middles = {};
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const std::size_t lane = std::size_t(std::countr_zero(bits));

		const LaneAsk ask = {
			.faces    = faces,
			.solved   = solved.data(),
			.together = together,
			.header   = asked.run.header,
			.lane     = int(lane),
			.inside   = ((asked.inside >> lane) & 1) != 0,
		};

		laneCrossings(ask, crossings[lane]);
		gaps[lane]    = widestGap(crossings[lane]);
		middles[lane] = gapMiddle(crossings[lane], gaps[lane]);
	}
	const Lanes middle = std::simd::unchecked_load<Lanes>(middles.data(), packetWidth);
	unsigned below[8]  = {};
	for (std::uint32_t face = 0; face < count; ++face) {
		below[face] = belowLanes(faces.along[face], middle);
	}
	for (unsigned bits = lanes; bits != 0; bits &= bits - 1) {
		const int lane            = std::countr_zero(bits);
		const std::size_t k       = std::size_t(lane);
		const std::uint32_t signs = unwound(crossings[k], laneFaceBits(below, count, lane), gaps[k]);
		const CrossedLane crossed = {
			.crossings = crossings[k],
			.bits      = signs,
			.inside    = ((asked.inside >> lane) & 1) != 0,
		};

		settledLane(verdicts, lane, crossings[k].blocked ? missedLanding() : crossedLanding(asked.run, crossed));
	}
}

LaneVerdicts laneVerdicts(const LeafLanes& asked) {
	LaneVerdicts verdicts = noVerdicts();
	CrossedFaces crossed;
	const LaneFaces faces  = laneFaces(asked, crossed);
	const unsigned blocked = asked.lanes & ~asked.inside & faces.blocked;
	const unsigned known   = asked.lanes & ~blocked & ~faces.unknown;

	const LaneAnswer answer = {
		.asked = asked,
		.faces = faces,
		.below = faces.below,
	};

	verdicts.missed = blocked;
	uncrossedLanes(answer, known & ~faces.rooted, verdicts);
	const CrossedAsk crossedAsk = {
		.asked = asked,
		.faces = crossed,
	};

	crossedLanes(crossedAsk, asked.lanes & ~blocked & (faces.rooted | faces.unknown), verdicts);
	return verdicts;
}

}
