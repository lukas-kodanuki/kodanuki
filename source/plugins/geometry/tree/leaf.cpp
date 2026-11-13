#include "plugins/geometry/tree/leaf.h"

#include "plugins/geometry/tree/pass.h"
#include "plugins/geometry/tree/patch.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace kodanuki {

constexpr double tinyCoefficient = 1e-12;

struct Roots {
	float items[4];
	int count;
};

struct RootSpan {
	float lower;
	float upper;
};

struct Quadratic {
	float a;
	float b;
	float c;
};

LeafRun leafRun(const std::uint32_t* words) {
	LeafRun run = {
		.words   = words,
		.header  = runHeader(words[0]),
		.offsets = {},
	};

	std::uint32_t at = firstFaceWord(run.header);
	for (std::uint32_t face = 0; face < run.header.count; ++face) {
		run.offsets[face] = at;
		at += faceWordCount(run.header, face, words + at);
	}
	run.offsets[run.header.count] = at;
	return run;
}

const float* faceFloats(const LeafRun& run, std::uint32_t face) {
	return reinterpret_cast<const float*>(run.words + run.offsets[face]);
}

bool runInside(const LeafRun& run, std::uint32_t bits) {
	if (run.header.count == 1) {
		return bits == 1;
	}
	return ((run.words[1 + bits / 32] >> (bits % 32)) & 1) != 0;
}

Quartic linearQuartic(float linear, float constant) {
	return Quartic {
		.fourth   = 0,
		.cube     = 0,
		.square   = 0,
		.linear   = linear,
		.constant = constant,
	};
}

Quartic planeAlong(const float* f, const Leg& leg) {
	const float* p = leg.base;
	const float* d = leg.rate;
	const float b  = f[0] * d[0] + f[1] * d[1] + f[2] * d[2];
	const float c  = f[0] * p[0] + f[1] * p[1] + f[2] * p[2] + f[3];
	return linearQuartic(b, c);
}

Quartic quadricAlong(const float* f, const Leg& leg) {
	const float* p = leg.base;
	const float* d = leg.rate;
	const float a  = f[0] * d[0] * d[0] + f[1] * d[1] * d[1] + f[2] * d[2] * d[2] + f[3] * d[0] * d[1] + f[4] * d[0] * d[2] + f[5] * d[1] * d[2];
	const float b  = 2.0f * (f[0] * p[0] * d[0] + f[1] * p[1] * d[1] + f[2] * p[2] * d[2]) + f[3] * (p[0] * d[1] + p[1] * d[0]) + f[4] * (p[0] * d[2] + p[2] * d[0]) + f[5] * (p[1] * d[2] + p[2] * d[1]) + f[6] * d[0] + f[7] * d[1] + f[8] * d[2];
	const float c  = f[0] * p[0] * p[0] + f[1] * p[1] * p[1] + f[2] * p[2] * p[2] + f[3] * p[0] * p[1] + f[4] * p[0] * p[2] + f[5] * p[1] * p[2] + f[6] * p[0] + f[7] * p[1] + f[8] * p[2] + f[9];
	return Quartic {
		.fourth   = 0,
		.cube     = 0,
		.square   = a,
		.linear   = b,
		.constant = c,
	};
}

Quadratic squaredAxis(float base, float rate) {
	return Quadratic {
		.a = rate * rate,
		.b = 2.0f * base * rate,
		.c = base * base,
	};
}

Quadratic bilinearAlong(const float* w, const float* first, const float* second) {
	return Quadratic {
		.a = w[3] * first[1] * second[1],
		.b = (w[1] * first[1] + w[2] * second[1]) + w[3] * (first[0] * second[1] + second[0] * first[1]),
		.c = (w[0] + w[1] * first[0] + w[2] * second[0]) + w[3] * first[0] * second[0],
	};
}

void addProduct(Quartic& into, const Quadratic& left, const Quadratic& right) {
	into.fourth += left.a * right.a;
	into.cube += left.a * right.b + left.b * right.a;
	into.square += (left.a * right.c + left.c * right.a) + left.b * right.b;
	into.linear += left.b * right.c + left.c * right.b;
	into.constant += left.c * right.c;
}

Quartic trilinearAlong(const float* c, const float* u, const float* d) {
	const float p0      = c[0] + c[1] * u[0];
	const float p1      = c[1] * d[0];
	const float r0      = c[4] + c[5] * u[0];
	const float r1      = c[5] * d[0];
	const float q0      = c[2] + c[3] * u[0];
	const float q1      = c[3] * d[0];
	const float s0      = c[6] + c[7] * u[0];
	const float s1      = c[7] * d[0];
	const float first0  = p0 + q0 * u[1];
	const float first1  = (p1 + q0 * d[1]) + q1 * u[1];
	const float first2  = q1 * d[1];
	const float second0 = r0 + s0 * u[1];
	const float second1 = (r1 + s0 * d[1]) + s1 * u[1];
	const float second2 = s1 * d[1];
	return Quartic {
		.fourth   = 0,
		.cube     = second2 * d[2],
		.square   = (first2 + second1 * d[2]) + second2 * u[2],
		.linear   = (first1 + second0 * d[2]) + second1 * u[2],
		.constant = first0 + second0 * u[2],
	};
}

Quartic patchAlong(const Patch& patch, const Leg& leg) {
	const float* u  = leg.base;
	const float* d  = leg.rate;
	Quartic quartic = trilinearAlong(patch.weights, u, d);
	if (!patch.bent) {
		return quartic;
	}
	const float x[2] = { u[0], d[0] };
	const float y[2] = { u[1], d[1] };
	const float z[2] = { u[2], d[2] };
	addProduct(quartic, squaredAxis(u[0], d[0]), bilinearAlong(patch.weights + 8, y, z));
	addProduct(quartic, squaredAxis(u[1], d[1]), bilinearAlong(patch.weights + 12, x, z));
	addProduct(quartic, squaredAxis(u[2], d[2]), bilinearAlong(patch.weights + 16, x, y));
	return quartic;
}

Quartic negated(const Quartic& quartic) {
	return Quartic {
		.fourth   = -quartic.fourth,
		.cube     = -quartic.cube,
		.square   = -quartic.square,
		.linear   = -quartic.linear,
		.constant = -quartic.constant,
	};
}

Quartic ringAlong(const float* ring, const Leg& leg) {
	const float* base = leg.base;
	const float* rate = leg.rate;
	float p[3];
	float d[3];
	for (int k = 0; k < 3; ++k) {
		p[k] = ring[k] + ring[3 + k] * base[0] + ring[6 + k] * base[1] + ring[9 + k] * base[2];
		d[k] = ring[3 + k] * rate[0] + ring[6 + k] * rate[1] + ring[9 + k] * rate[2];
	}
	const float major = ring[12];
	const float minor = ring[13];
	const float dd    = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	const float pd    = 2.0f * (p[0] * d[0] + p[1] * d[1] + p[2] * d[2]);
	const float pp    = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
	const float q2    = dd;
	const float q1    = pd;
	const float q0    = pp + major * major - minor * minor;
	const float r2    = dd - d[2] * d[2];
	const float r1    = pd - 2.0f * p[2] * d[2];
	const float r0    = pp - p[2] * p[2];
	const float m     = 4.0f * major * major;

	const Quartic quartic = {
		.fourth   = q2 * q2,
		.cube     = 2.0f * q2 * q1,
		.square   = q1 * q1 + 2.0f * q2 * q0 - m * r2,
		.linear   = 2.0f * q1 * q0 - m * r1,
		.constant = q0 * q0 - m * r0,
	};

	return minor < 0.0f ? negated(quartic) : quartic;
}

Quartic faceAlong(const LeafRun& run, std::uint32_t face, const Leg& leg) {
	switch (faceKind(run.header, face)) {
		case FaceKind::Plane: return planeAlong(faceFloats(run, face), leg);
		case FaceKind::Quadric: return quadricAlong(faceFloats(run, face), leg);
		case FaceKind::Patch: return patchAlong(readPatch(run.words + run.offsets[face]), leg);
		case FaceKind::Ring: return ringAlong(faceFloats(run, face), leg);
	}

	return linearQuartic(0, 1);
}

Restricted noFaces(std::uint32_t count) {
	Restricted restricted;
	restricted.count = count;
	return restricted;
}

float quarticValue(const Quartic& quartic, float s) {
	return (((quartic.fourth * s + quartic.cube) * s + quartic.square) * s + quartic.linear) * s + quartic.constant;
}

float quarticSlope(const Quartic& quartic, float s) {
	return ((4.0f * quartic.fourth * s + 3.0f * quartic.cube) * s + 2.0f * quartic.square) * s + quartic.linear;
}

Roots noRoots() {
	return Roots {
		.items = { 0, 0, 0, 0 },
		.count = 0,
	};
}

void addRoot(Roots& roots, float root, const RootSpan& span) {
	if (root >= span.lower && root <= span.upper && roots.count < 4) {
		roots.items[roots.count] = root;
		roots.count += 1;
	}
}

Roots linearRoots(float b, float c, const RootSpan& span) {
	Roots roots = noRoots();
	if (b != 0.0f) {
		addRoot(roots, -c / b, span);
	}
	return roots;
}

Roots quadricRoots(const Quartic& face, const RootSpan& span) {
	const float a = face.square;
	const float b = face.linear;
	const float c = face.constant;
	if (a == 0.0f) {
		return linearRoots(b, c, span);
	}
	const float discriminant = b * b - 4.0f * a * c;
	if (discriminant < 0.0f) {
		return noRoots();
	}
	const float root   = std::sqrt(discriminant);
	const float q      = -0.5f * (b + (b >= 0.0f ? root : -root));
	const float first  = q / a;
	const float second = q != 0.0f ? c / q : first;
	Roots roots        = noRoots();
	addRoot(roots, std::min(first, second), span);
	addRoot(roots, std::max(first, second), span);
	return roots;
}

void addInside(Roots& roots, float root, const RootSpan& span) {
	if (root > span.lower && root < span.upper) {
		roots.items[roots.count] = root;
		roots.count += 1;
	}
}

Roots openQuadraticRoots(const Quadratic& q, const RootSpan& span) {
	Roots roots = noRoots();
	if (double(std::abs(q.a)) <= tinyCoefficient) {
		if (double(std::abs(q.b)) > tinyCoefficient) {
			addInside(roots, -q.c / q.b, span);
		}
		return roots;
	}
	const float discriminant = q.b * q.b - 4.0f * q.a * q.c;
	if (discriminant <= 0.0f) {
		return roots;
	}
	const float shared = -0.5f * (q.b + std::copysign(std::sqrt(discriminant), q.b));
	const float first  = shared / q.a;
	const float second = shared != 0.0f ? q.c / shared : first;
	addInside(roots, std::min(first, second), span);
	addInside(roots, std::max(first, second), span);
	return roots;
}

float cubicValue(const Quartic& slope, float s) {
	return ((slope.cube * s + slope.square) * s + slope.linear) * s + slope.constant;
}

float cubicSlope(const Quartic& slope, float s) {
	return (3.0f * slope.cube * s + 2.0f * slope.square) * s + slope.linear;
}

float bracketed(float next, const RootSpan& bracket) {
	return next > bracket.lower && next < bracket.upper ? next : 0.5f * (bracket.lower + bracket.upper);
}

float settledCubic(const Quartic& slope, RootSpan bracket, float atLower) {
	float s = 0.5f * (bracket.lower + bracket.upper);
	for (int step = 0; step < 12; ++step) {
		const float here = cubicValue(slope, s);
		if (here == 0.0f) {
			return s;
		}
		bracket.lower          = (here < 0.0f) == (atLower < 0.0f) ? s : bracket.lower;
		bracket.upper          = (here < 0.0f) == (atLower < 0.0f) ? bracket.upper : s;
		const float derivative = cubicSlope(slope, s);
		const float next       = bracketed(derivative != 0.0f ? s - here / derivative : 0.5f * (bracket.lower + bracket.upper), bracket);
		if (std::abs(next - s) <= 1e-6f * std::max(1.0f, std::abs(s))) {
			return next;
		}
		s = next;
	}
	return s;
}

Roots cubicRoots(const Quartic& slope, const RootSpan& span) {
	if (double(std::abs(slope.cube)) <= tinyCoefficient) {
		const Quadratic quadratic = {
			.a = slope.square,
			.b = slope.linear,
			.c = slope.constant,
		};

		return openQuadraticRoots(quadratic, span);
	}

	const Quadratic turns = {
		.a = 3.0f * slope.cube,
		.b = 2.0f * slope.square,
		.c = slope.linear,
	};

	const Roots inner       = openQuadraticRoots(turns, span);
	float breaks[4]         = { span.lower, inner.items[0], inner.items[1], span.upper };
	breaks[inner.count + 1] = span.upper;
	Roots roots             = noRoots();
	for (int k = 0; k <= inner.count; ++k) {
		const RootSpan piece = {
			.lower = breaks[k],
			.upper = breaks[k + 1],
		};

		const float here  = cubicValue(slope, piece.lower);
		const float there = cubicValue(slope, piece.upper);
		if (here == 0.0f) {
			roots.items[roots.count] = piece.lower;
			roots.count += 1;
			continue;
		}
		if ((here < 0.0f) != (there < 0.0f)) {
			roots.items[roots.count] = settledCubic(slope, piece, here);
			roots.count += 1;
		}
	}
	return roots;
}

float settledQuartic(const Quartic& quartic, RootSpan bracket, float atLower) {
	float s = 0.5f * (bracket.lower + bracket.upper);
	for (int step = 0; step < 40; ++step) {
		const float here = quarticValue(quartic, s);
		if (here == 0.0f) {
			return s;
		}
		bracket.lower          = (here < 0.0f) == (atLower < 0.0f) ? s : bracket.lower;
		bracket.upper          = (here < 0.0f) == (atLower < 0.0f) ? bracket.upper : s;
		const float derivative = quarticSlope(quartic, s);
		const float next       = bracketed(derivative != 0.0f ? s - here / derivative : 0.5f * (bracket.lower + bracket.upper), bracket);
		if (std::abs(next - s) <= 1e-6f * std::max(1.0f, std::abs(s))) {
			return next;
		}
		s = next;
	}
	return s;
}

Quartic slopeOf(const Quartic& quartic) {
	return Quartic {
		.fourth   = 0,
		.cube     = 4.0f * quartic.fourth,
		.square   = 3.0f * quartic.cube,
		.linear   = 2.0f * quartic.square,
		.constant = quartic.linear,
	};
}

Quartic shiftedQuartic(const Quartic& q, float l) {
	return Quartic {
		.fourth   = q.fourth,
		.cube     = q.cube + 4.0f * q.fourth * l,
		.square   = q.square + (3.0f * q.cube + 6.0f * q.fourth * l) * l,
		.linear   = q.linear + (2.0f * q.square + (3.0f * q.cube + 4.0f * q.fourth * l) * l) * l,
		.constant = quarticValue(q, l),
	};
}

Quartic stretchedQuartic(const Quartic& q, float w) {
	const float square = w * w;
	return Quartic {
		.fourth   = q.fourth * (square * square),
		.cube     = q.cube * (square * w),
		.square   = q.square * square,
		.linear   = q.linear * w,
		.constant = q.constant,
	};
}

std::array<float, 5> bernsteinOf(const Quartic& d) {
	return {
		d.constant,
		d.constant + 0.25f * d.linear,
		d.constant + 0.5f * d.linear + d.square / 6.0f,
		d.constant + 0.75f * d.linear + 0.5f * d.square + 0.25f * d.cube,
		d.constant + d.linear + d.square + d.cube + d.fourth,
	};
}

float evaluationSlack(const Quartic& q) {
	return signSlack * ((std::abs(q.fourth) + std::abs(q.cube)) + (std::abs(q.square) + std::abs(q.linear)) + std::abs(q.constant));
}

bool keepsOneSign(const Quartic& quartic, const RootSpan& span) {
	const Quartic window                 = stretchedQuartic(shiftedQuartic(quartic, span.lower), span.upper - span.lower);
	const std::array<float, 5> bernstein = bernsteinOf(window);
	const float slack                    = evaluationSlack(quartic);
	bool above                           = true;
	bool below                           = true;
	for (const float coefficient : bernstein) {
		above = above && coefficient > slack;
		below = below && coefficient < -slack;
	}
	return above || below;
}

Roots quarticRoots(const Quartic& quartic, const RootSpan& span) {
	if (keepsOneSign(quartic, span)) {
		return noRoots();
	}
	const Roots turns = cubicRoots(slopeOf(quartic), span);
	float breaks[5]   = { span.lower, 0, 0, 0, 0 };
	int total         = 1;
	for (int k = 0; k < turns.count; ++k) {
		breaks[total] = turns.items[k];
		total += 1;
	}
	breaks[total] = span.upper;
	Roots roots   = noRoots();
	for (int k = 0; k < total; ++k) {
		const RootSpan piece = {
			.lower = breaks[k],
			.upper = breaks[k + 1],
		};

		const float here  = quarticValue(quartic, piece.lower);
		const float there = quarticValue(quartic, piece.upper);
		if (here != 0.0f && (here < 0.0f) != (there < 0.0f)) {
			addRoot(roots, settledQuartic(quartic, piece, here), span);
		}
	}
	return roots;
}

RootSpan legWindow() {
	return RootSpan {
		.lower = -rootSlack,
		.upper = 1.0f + rootSlack,
	};
}

WindowRoots quarticWindowRoots(const Quartic& quartic) {
	const Roots roots = quarticRoots(quartic, legWindow());
	return WindowRoots {
		.at    = { roots.items[0], roots.items[1], roots.items[2], roots.items[3] },
		.count = roots.count,
	};
}

Roots faceRoots(const LeafRun& run, const Restricted& restricted, std::uint32_t face) {
	const Quartic& along = restricted.faces[face];
	switch (faceKind(run.header, face)) {
		case FaceKind::Plane: return linearRoots(along.linear, along.constant, legWindow());
		case FaceKind::Quadric: return quadricRoots(along, legWindow());
		case FaceKind::Patch:
		case FaceKind::Ring: return quarticRoots(along, legWindow());
	}

	return noRoots();
}

Crossings noCrossings() {
	Crossings crossings;
	crossings.count   = 0;
	crossings.blocked = false;
	return crossings;
}

bool isNeeded(const LeafVisit& visit, std::uint32_t face) {
	return !visit.inside && ((visit.run.header.needed >> face) & 1) != 0;
}

bool neverInside(const Restricted& restricted, std::uint32_t face, const Roots& roots) {
	return roots.count == 0 && !(quarticValue(restricted.faces[face], 0.5f) < 0.0f);
}

void addCrossings(Crossings& crossings, const Roots& roots, std::uint32_t face) {
	for (int k = 0; k < roots.count; ++k) {
		crossings.items[std::size_t(crossings.count)] = Crossing {
			.at   = roots.items[k],
			.face = face,
		};

		crossings.count += 1;
	}
}

bool crossesBefore(const Crossing& a, const Crossing& b) {
	return a.at < b.at || (a.at == b.at && a.face < b.face);
}

void sortCrossings(Crossings& crossings) {
	for (int i = 1; i < crossings.count; ++i) {
		const Crossing item = crossings.items[std::size_t(i)];
		int k               = i;
		for (; k > 0 && crossesBefore(item, crossings.items[std::size_t(k - 1)]); --k) {
			crossings.items[std::size_t(k)] = crossings.items[std::size_t(k - 1)];
		}
		crossings.items[std::size_t(k)] = item;
	}
}

struct FaceSolve {
	const LeafVisit& visit;
	SlabLeg leg;
};

bool isPlane(const RunHeader& header, std::uint32_t face) {
	return (((header.planes & ~header.patches) >> face) & 1) != 0;
}

int keptSide(const FaceSolve& asked, std::uint32_t face) {
	const LeafRun& run = asked.visit.run;
	if (!run.header.slabbed || isPlane(run.header, face)) {
		return 0;
	}
	return passFaceSide(asked.leg, run.words + run.offsets[face + 1] - slabWordCount);
}

bool solvedFace(const FaceSolve& asked, SolvedFaces& solved, std::uint32_t face) {
	const LeafVisit& visit        = asked.visit;
	const int side                = keptSide(asked, face);
	solved.restricted.faces[face] = side != 0 ? linearQuartic(0, float(side)) : faceAlong(visit.run, face, visit.leg);
	const Roots roots             = side != 0 ? noRoots() : faceRoots(visit.run, solved.restricted, face);
	if (isNeeded(visit, face) && neverInside(solved.restricted, face, roots)) {
		solved.crossings.blocked = true;
		return false;
	}
	addCrossings(solved.crossings, roots, face);
	return true;
}

bool solvedGroup(const FaceSolve& asked, SolvedFaces& solved, std::uint32_t faces) {
	for (std::uint32_t bits = faces; bits != 0; bits &= bits - 1) {
		if (!solvedFace(asked, solved, std::uint32_t(std::countr_zero(bits)))) {
			return false;
		}
	}
	return true;
}

SolvedFaces solvedFaces(const LeafVisit& visit) {
	const std::uint32_t every  = (1u << visit.run.header.count) - 1;
	const std::uint32_t needed = visit.inside ? 0u : visit.run.header.needed & every;

	const FaceSolve asked = {
		.visit = visit,
		.leg   = passLeg(visit.run.header, visit.leg.base, visit.leg.rate),
	};

	SolvedFaces solved = {
		.restricted = noFaces(visit.run.header.count),
		.crossings  = noCrossings(),
	};

	if (solvedGroup(asked, solved, needed) && solvedGroup(asked, solved, every & ~needed)) {
		sortCrossings(solved.crossings);
	}
	return solved;
}

std::uint32_t signBits(const Restricted& restricted, float s) {
	std::uint32_t bits = 0;
	for (std::uint32_t face = 0; face < restricted.count; ++face) {
		bits |= quarticValue(restricted.faces[face], s) < 0.0f ? 1u << face : 0u;
	}
	return bits;
}

float gapLower(const Crossings& crossings, int gap) {
	return gap == 0 ? 0.0f : crossings.items[std::size_t(gap - 1)].at;
}

float gapWidth(const Crossings& crossings, int gap) {
	const float upper = gap == crossings.count ? 1.0f : crossings.items[std::size_t(gap)].at;
	return upper - gapLower(crossings, gap);
}

int widestGap(const Crossings& crossings) {
	int widest = 0;
	for (int gap = 1; gap <= crossings.count; ++gap) {
		widest = gapWidth(crossings, gap) > gapWidth(crossings, widest) ? gap : widest;
	}
	return widest;
}

std::uint32_t unwound(const Crossings& crossings, std::uint32_t bits, int count) {
	std::uint32_t before = bits;
	for (int i = 0; i < count; ++i) {
		before ^= 1u << crossings.items[std::size_t(i)].face;
	}
	return before;
}

float gapMiddle(const Crossings& crossings, int gap) {
	return gapLower(crossings, gap) + gapWidth(crossings, gap) / 2;
}

struct Tie {
	const LeafRun& run;
	const Crossings& crossings;
	std::uint32_t before;
	bool wanted;
};

int firstTied(const Tie& tie, int landing) {
	const float reach = tie.crossings.items[std::size_t(landing)].at + rootSlack;
	int first         = landing;
	for (int k = landing + 1; k < tie.crossings.count && tie.crossings.items[std::size_t(k)].at <= reach; ++k) {
		const std::uint32_t face = tie.crossings.items[std::size_t(k)].face;
		if (face < tie.crossings.items[std::size_t(first)].face && runInside(tie.run, tie.before ^ (1u << face)) == tie.wanted) {
			first = k;
		}
	}
	return first;
}

Passings noPassings() {
	Passings passings;
	passings.count = 0;
	return passings;
}

Landing landedAt(float at, int face) {
	return Landing {
		.found    = true,
		.at       = std::max(at, 0.0f),
		.passings = noPassings(),
		.face     = face,
	};
}

bool crossedAlone(const Crossings& crossings, int k) {
	const float at    = crossings.items[std::size_t(k)].at;
	const bool before = k > 0 && crossings.items[std::size_t(k - 1)].at >= at - rootSlack;
	const bool after  = k + 1 < crossings.count && crossings.items[std::size_t(k + 1)].at <= at + rootSlack;
	return !before && !after;
}

int aloneFace(const Crossings& crossings, int k) {
	return crossedAlone(crossings, k) ? int(crossings.items[std::size_t(k)].face) : -1;
}

Landing landedOn(const Crossings& crossings, int k) {
	return landedAt(crossings.items[std::size_t(k)].at, aloneFace(crossings, k));
}

Landing noLanding() {
	return Landing {
		.found    = false,
		.at       = 0,
		.passings = noPassings(),
		.face     = -1,
	};
}

std::uint32_t startingBits(const Restricted& restricted, const Crossings& crossings) {
	const int gap = widestGap(crossings);
	return unwound(crossings, signBits(restricted, gapMiddle(crossings, gap)), gap);
}

Landing walkedCrossings(const LeafVisit& visit, const Crossings& crossings, std::uint32_t bits) {
	const bool wanted = !visit.inside;
	for (int i = 0; i < crossings.count; ++i) {
		const std::uint32_t face = crossings.items[std::size_t(i)].face;
		if (runInside(visit.run, bits ^ (1u << face)) != wanted) {
			bits ^= 1u << face;
			continue;
		}

		const Tie tie = {
			.run       = visit.run,
			.crossings = crossings,
			.before    = bits,
			.wanted    = wanted,
		};

		return landedOn(crossings, firstTied(tie, i));
	}
	return noLanding();
}

struct TiedGroup {
	int first;
	int end;
};

TiedGroup tiedGroup(const Crossings& crossings, int first) {
	const float reach = crossings.items[std::size_t(first)].at + rootSlack;
	int end           = first + 1;
	while (end < crossings.count && crossings.items[std::size_t(end)].at <= reach) {
		end += 1;
	}
	return TiedGroup {
		.first = first,
		.end   = end,
	};
}

std::uint32_t crossedGroup(const Crossings& crossings, const TiedGroup& group, std::uint32_t bits) {
	std::uint32_t after = bits;
	for (int k = group.first; k < group.end; ++k) {
		after ^= 1u << crossings.items[std::size_t(k)].face;
	}
	return after;
}

int firstLeaving(const Tie& tie, const TiedGroup& group) {
	std::uint32_t bits = tie.before;
	for (int k = group.first; k < group.end; ++k) {
		bits ^= 1u << tie.crossings.items[std::size_t(k)].face;
		if (runInside(tie.run, bits) == tie.wanted) {
			return k;
		}
	}
	return group.end - 1;
}

Landing passedAt(const Landing& landing, float at) {
	Landing passed                                         = landing;
	passed.passings.at[std::size_t(passed.passings.count)] = std::max(at, 0.0f);
	passed.passings.count += 1;
	return passed;
}

Landing leftAt(const Tie& tie, const TiedGroup& group, const Passings& passings) {
	const int left = firstLeaving(tie, group);
	return Landing {
		.found    = true,
		.at       = std::max(tie.crossings.items[std::size_t(left)].at, 0.0f),
		.passings = passings,
		.face     = aloneFace(tie.crossings, left),
	};
}

Landing leftCrossings(const LeafVisit& visit, const Crossings& crossings, std::uint32_t bits) {
	Landing landing = noLanding();
	for (int i = 0; i < crossings.count;) {
		const TiedGroup group     = tiedGroup(crossings, i);
		const std::uint32_t after = crossedGroup(crossings, group, bits);

		const Tie tie = {
			.run       = visit.run,
			.crossings = crossings,
			.before    = bits,
			.wanted    = false,
		};

		if (!runInside(visit.run, after)) {
			return leftAt(tie, group, landing.passings);
		}
		landing = after != bits ? passedAt(landing, crossings.items[std::size_t(i)].at) : landing;
		bits    = after;
		i       = group.end;
	}
	return landing;
}

bool turnsInside(const LeafVisit& visit, const Crossings& crossings, std::uint32_t bits) {
	std::uint32_t signs = bits;
	for (int i = 0; i < crossings.count; ++i) {
		signs ^= 1u << crossings.items[std::size_t(i)].face;
		if (runInside(visit.run, signs)) {
			return true;
		}
	}
	return false;
}

bool reachesSurface(const LeafVisit& visit) {
	const SolvedFaces solved = solvedFaces(visit);
	if (solved.crossings.blocked) {
		return false;
	}
	const std::uint32_t bits = startingBits(solved.restricted, solved.crossings);
	return runInside(visit.run, bits) || turnsInside(visit, solved.crossings, bits);
}

Landing landLeaf(const LeafVisit& visit) {
	const SolvedFaces solved = solvedFaces(visit);
	if (solved.crossings.blocked) {
		return noLanding();
	}
	const std::uint32_t bits = startingBits(solved.restricted, solved.crossings);
	if (runInside(visit.run, bits) == !visit.inside) {
		return landedAt(0, -1);
	}
	return visit.inside ? leftCrossings(visit, solved.crossings, bits) : walkedCrossings(visit, solved.crossings, bits);
}

}
