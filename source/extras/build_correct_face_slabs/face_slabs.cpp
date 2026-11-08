#include "extras/build_correct_face_slabs/face_slabs.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdfloat>
#include <vector>

namespace kodanuki {

constexpr double widening   = 1.01;
constexpr double floorShare = 1e-7;
constexpr double middle     = 0.5;
constexpr double halfWidth  = 0.5 + slabReach;

using Line     = std::array<double*, 3>;
using LineStep = void (*)(const Line&);

float halved(double value) {
	return float(std::float16_t(value));
}

float halvedUp(double value) {
	const std::float16_t held = std::float16_t(value);
	if (double(held) >= value) {
		return float(held);
	}
	return float(std::nextafter(held, std::numeric_limits<std::float16_t>::infinity()));
}

Slab neverSlab() {
	return Slab {
		.slope  = { 0, 0, 0 },
		.offset = 0,
		.stray  = std::numeric_limits<float>::infinity(),
	};
}

std::array<int, 3> slabGridIndex(int axis, int step, int first, int second) {
	switch (axis) {
		case 0: return { step, first, second };
		case 1: return { first, step, second };
		default: return { first, second, step };
	}
}

double& slabTermAt(Triquadratic& polynomial, const std::array<int, 3>& at) {
	return polynomial.terms[std::size_t(at[0])][std::size_t(at[1])][std::size_t(at[2])];
}

Line slabGridLine(Triquadratic& polynomial, int axis, int first, int second) {
	return { &slabTermAt(polynomial, slabGridIndex(axis, 0, first, second)), &slabTermAt(polynomial, slabGridIndex(axis, 1, first, second)), &slabTermAt(polynomial, slabGridIndex(axis, 2, first, second)) };
}

void widenedLine(const Line& line) {
	const double start = -slabReach;
	const double span  = 1 + 2 * slabReach;
	const double a0    = *line[0];
	const double a1    = *line[1];
	const double a2    = *line[2];
	*line[0]           = a0 + a1 * start + a2 * start * start;
	*line[1]           = (a1 + 2 * a2 * start) * span;
	*line[2]           = a2 * span * span;
}

Triquadratic alongEveryLine(const Triquadratic& polynomial, LineStep step) {
	Triquadratic result = polynomial;
	for (int axis = 0; axis < 3; ++axis) {
		for (int first = 0; first < 3; ++first) {
			for (int second = 0; second < 3; ++second) {
				step(slabGridLine(result, axis, first, second));
			}
		}
	}
	return result;
}

double triquadraticSize(const Triquadratic& polynomial) {
	double size = 0;
	for (const auto& plane : polynomial.terms) {
		for (const auto& row : plane) {
			for (const double value : row) {
				size += std::abs(value);
			}
		}
	}
	return size;
}

double unitScale(double size) {
	return std::ldexp(1.0, -std::ilogb(size) - 1);
}

bool usableSize(double size) {
	return size > 0 && std::isfinite(size);
}

Triquadratic scaledTriquadratic(const Triquadratic& polynomial, double factor) {
	Triquadratic scaled = polynomial;
	for (auto& plane : scaled.terms) {
		for (auto& row : plane) {
			for (double& value : row) {
				value *= factor;
			}
		}
	}
	return scaled;
}

double termPower(int power) {
	return power == 0 ? 1.0 : (power == 1 ? middle : middle * middle);
}

double termSlope(const Triquadratic& polynomial, const std::array<int, 3>& power, int axis) {
	const int along = power[std::size_t(axis)];
	if (along == 0) {
		return 0;
	}
	double term = polynomial.terms[std::size_t(power[0])][std::size_t(power[1])][std::size_t(power[2])] * along;
	for (int other = 0; other < 3; ++other) {
		term *= termPower(other == axis ? along - 1 : power[std::size_t(other)]);
	}
	return term;
}

double middleSlope(const Triquadratic& polynomial, int axis) {
	double slope = 0;
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			for (int k = 0; k < 3; ++k) {
				slope += termSlope(polynomial, { i, j, k }, axis);
			}
		}
	}
	return slope;
}

bool finiteSlopes(const Slab& slab) {
	return std::isfinite(slab.slope[0]) && std::isfinite(slab.slope[1]) && std::isfinite(slab.slope[2]);
}

Slab heldSlab(const Slab& sloped, const math::Interval& range) {
	const double offset = 0.5 * (range.lower + range.upper);
	Slab slab           = sloped;
	slab.offset         = halved(offset);
	const double lost   = std::abs(offset - double(slab.offset));
	slab.stray          = halvedUp(0.5 * (range.upper - range.lower) * widening + floorShare + lost * widening);
	return std::isfinite(slab.offset) && finiteSlopes(slab) ? slab : neverSlab();
}

Slab triquadraticSlab(const Triquadratic& face) {
	const double size = triquadraticSize(face);
	if (!usableSize(size)) {
		return neverSlab();
	}
	Triquadratic rest = scaledTriquadratic(face, unitScale(size));
	Slab sloped       = neverSlab();
	for (int axis = 0; axis < 3; ++axis) {
		sloped.slope[axis] = halved(middleSlope(rest, axis));
	}
	rest.terms[1][0][0] -= double(sloped.slope[0]);
	rest.terms[0][1][0] -= double(sloped.slope[1]);
	rest.terms[0][0][1] -= double(sloped.slope[2]);
	return heldSlab(sloped, triquadraticNet(alongEveryLine(rest, widenedLine)));
}

Slab patchSlab(const Patch& patch) {
	return triquadraticSlab(patchTriquadratic(patch));
}

Slab quadricSlab(const float* face) {
	Triquadratic polynomial = {
		.terms = {},
	};

	polynomial.terms[2][0][0] = double(face[0]);
	polynomial.terms[0][2][0] = double(face[1]);
	polynomial.terms[0][0][2] = double(face[2]);
	polynomial.terms[1][1][0] = double(face[3]);
	polynomial.terms[1][0][1] = double(face[4]);
	polynomial.terms[0][1][1] = double(face[5]);
	polynomial.terms[1][0][0] = double(face[6]);
	polynomial.terms[0][1][0] = double(face[7]);
	polynomial.terms[0][0][1] = double(face[8]);
	polynomial.terms[0][0][0] = double(face[9]);
	return triquadraticSlab(polynomial);
}

math::Interval pointInterval(double value) {
	return math::Interval {
		.lower = value,
		.upper = value,
	};
}

math::IntervalHyperDual ringFrame(const float* face, const math::Interval& span, int k) {
	math::IntervalHyperDual v = math::constantAs<math::IntervalHyperDual>(double(face[k]));
	for (int axis = 0; axis < 3; ++axis) {
		v = v + math::constantAs<math::IntervalHyperDual>(double(face[3 + 3 * axis + k])) * math::coordinateOver(span, axis);
	}
	return v;
}

math::IntervalHyperDual ringOver(const float* face, const math::Interval& span) {
	const math::IntervalHyperDual x = ringFrame(face, span, 0);
	const math::IntervalHyperDual y = ringFrame(face, span, 1);
	const math::IntervalHyperDual z = ringFrame(face, span, 2);
	const double major              = double(face[12]);
	const double minor              = double(face[13]);
	const math::IntervalHyperDual q = x * x + y * y + z * z + math::constantAs<math::IntervalHyperDual>(major * major - minor * minor);
	const math::IntervalHyperDual t = q * q - math::constantAs<math::IntervalHyperDual>(4 * major * major) * (x * x + y * y);
	return minor < 0 ? -t : t;
}

double ringSize(const float* face) {
	double reach = 0;
	for (int k = 0; k < 3; ++k) {
		double coordinate = std::abs(double(face[k]));
		for (int axis = 0; axis < 3; ++axis) {
			coordinate += std::abs(double(face[3 + 3 * axis + k]));
		}
		reach = std::max(reach, coordinate);
	}
	const double major  = double(face[12]);
	const double minor  = double(face[13]);
	const double square = 12 * reach * reach + major * major + minor * minor;
	return square * square + 32 * major * major * reach * reach;
}

math::Interval curvedRest(const math::IntervalHyperDual& over) {
	const math::Interval square = {
		.lower = 0,
		.upper = halfWidth * halfWidth,
	};

	const math::Interval cross = {
		.lower = -halfWidth * halfWidth,
		.upper = halfWidth * halfWidth,
	};

	const math::Interval half = pointInterval(0.5);
	math::Interval rest       = pointInterval(0);
	for (int k = 0; k < 6; ++k) {
		const bool diagonal = k == 0 || k == 3 || k == 5;
		rest                = rest + over.hessian[k] * (diagonal ? half * square : cross);
	}
	return rest;
}

Slab ringSlab(const float* face) {
	const double size = ringSize(face);
	if (!usableSize(size)) {
		return neverSlab();
	}
	const double scale = unitScale(size);

	const math::Interval cell = {
		.lower = -slabReach,
		.upper = 1 + slabReach,
	};

	const math::IntervalHyperDual centre = ringOver(face, pointInterval(middle));
	const math::IntervalHyperDual over   = ringOver(face, cell);
	Slab sloped                          = neverSlab();
	math::Interval range                 = pointInterval(scale * centre.value.lower);
	for (int axis = 0; axis < 3; ++axis) {
		const double slope = scale * centre.gradient[axis].lower;
		sloped.slope[axis] = halved(slope);
		const double off   = std::abs(slope - double(sloped.slope[axis])) * halfWidth;

		const math::Interval tilt = {
			.lower = -double(sloped.slope[axis]) * middle - off,
			.upper = -double(sloped.slope[axis]) * middle + off,
		};

		range = range + tilt;
	}
	return heldSlab(sloped, range + pointInterval(scale) * curvedRest(over));
}

std::uint32_t halvesWord(float low, float high) {
	const std::uint16_t a = std::bit_cast<std::uint16_t>(std::float16_t(low));
	const std::uint16_t b = std::bit_cast<std::uint16_t>(std::float16_t(high));
	return std::uint32_t(a) | (std::uint32_t(b) << 16);
}

std::array<std::uint32_t, 3> slabWords(const Slab& slab) {
	return { halvesWord(slab.slope[0], slab.slope[1]), halvesWord(slab.slope[2], slab.offset), halvesWord(slab.stray, 0) };
}

float wordHalf(std::uint32_t word, int half) {
	return float(std::bit_cast<std::float16_t>(std::uint16_t(word >> (16 * half))));
}

Slab readSlab(const std::uint32_t* words) {
	return Slab {
		.slope  = { wordHalf(words[0], 0), wordHalf(words[0], 1), wordHalf(words[1], 0) },
		.offset = wordHalf(words[1], 1),
		.stray  = wordHalf(words[2], 0),
	};
}

constexpr float slabWindow = 2e-4f;
constexpr float floatError = 1e-5f;
constexpr float heldReach  = float(slabReach / 2);

float legScale(const float* base, const float* rate) {
	float scale = 1;
	for (int axis = 0; axis < 3; ++axis) {
		scale = std::max(scale, std::abs(base[axis]) + std::abs(rate[axis]) * (1 + slabWindow));
	}
	return scale;
}

bool withinReach(const float* u) {
	bool within = true;
	for (int axis = 0; axis < 3; ++axis) {
		within = within && u[axis] >= -heldReach && u[axis] <= 1 + heldReach;
	}
	return within;
}

SlabLeg slabLegOf(const float* base, const float* rate) {
	const float scale  = legScale(base, rate);
	const float square = scale * scale;

	SlabLeg leg = {
		.entry  = {},
		.leave  = {},
		.margin = floatError * square * square,
		.held   = false,
	};

	for (int axis = 0; axis < 3; ++axis) {
		leg.entry[axis] = base[axis] - rate[axis] * slabWindow;
		leg.leave[axis] = base[axis] + rate[axis] * (1 + slabWindow);
	}
	leg.held = withinReach(leg.entry) && withinReach(leg.leave);
	return leg;
}

SlabLeg unheldSlabLeg() {
	return SlabLeg {
		.entry  = { 0, 0, 0 },
		.leave  = { 0, 0, 0 },
		.margin = 0,
		.held   = false,
	};
}

float planeAt(const Slab& slab, const float* u) {
	return ((slab.slope[0] * u[0] + slab.slope[1] * u[1]) + slab.slope[2] * u[2]) + slab.offset;
}

int slabSide(const SlabLeg& leg, const std::uint32_t* slab) {
	if (!leg.held) {
		return 0;
	}
	const Slab read    = readSlab(slab);
	const float entry  = planeAt(read, leg.entry);
	const float leave  = planeAt(read, leg.leave);
	const float margin = read.stray + leg.margin;
	if (entry > margin && leave > margin) {
		return 1;
	}
	return entry < -margin && leave < -margin ? -1 : 0;
}

Slab faceSlab(FaceKind kind, const std::uint32_t* words) {
	const float* floats = reinterpret_cast<const float*>(words);
	switch (kind) {
		case FaceKind::Plane: return neverSlab();
		case FaceKind::Quadric: return quadricSlab(floats);
		case FaceKind::Patch: return patchSlab(readPatch(words));
		case FaceKind::Ring: return ringSlab(floats);
	}

	return neverSlab();
}

bool hasCurvedFace(const RunHeader& header) {
	const std::uint32_t every = (1u << header.count) - 1;
	return ((header.patches | ~header.planes) & every) != 0;
}

std::vector<std::uint32_t> slabbedRun(const std::uint32_t* run) {
	const RunHeader header                   = runHeader(run[0]);
	const std::vector<std::uint32_t> offsets = faceOffsets(run);
	if (header.slabbed || !hasCurvedFace(header)) {
		return std::vector<std::uint32_t>(run, run + offsets.back());
	}
	RunHeader marked = header;
	marked.slabbed   = true;

	std::vector<std::uint32_t> words = { runHeaderWord(marked) };
	words.insert(words.end(), run + 1, run + offsets[0]);
	for (std::uint32_t face = 0; face < header.count; ++face) {
		const FaceKind kind = faceKind(header, face);
		words.insert(words.end(), run + offsets[face], run + offsets[face + 1]);
		if (kind != FaceKind::Plane) {
			const std::array<std::uint32_t, 3> slab = slabWords(faceSlab(kind, run + offsets[face]));
			words.insert(words.end(), slab.begin(), slab.end());
		}
	}
	return words;
}

Subtree faceSlabs(const Subtree& subtree) {
	if (!faceSlabsBuilt) {
		return subtree;
	}
	Subtree slabbed = subtree;
	slabbed.runs    = {};
	for (SurfaceLeaf& surface : slabbed.surfaces) {
		const std::vector<std::uint32_t> run = slabbedRun(subtree.runs.data() + surface.run);
		surface.run                          = std::uint32_t(slabbed.runs.size());
		slabbed.runs.insert(slabbed.runs.end(), run.begin(), run.end());
	}
	return slabbed;
}

}
