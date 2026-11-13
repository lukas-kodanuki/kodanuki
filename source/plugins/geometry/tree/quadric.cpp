#include "plugins/geometry/tree/quadric.h"
#include "sdf/numbers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace kodanuki {

constexpr int quadricRow[6]       = { 0, 1, 2, 0, 0, 1 };
constexpr int quadricColumn[6]    = { 0, 1, 2, 1, 2, 2 };
constexpr int hessianSlot[6]      = { 0, 3, 5, 1, 2, 4 };
constexpr int refinements         = 2;
constexpr double roundingRelative = 1e-12;

struct Remainder {
	const Program& program;
	const Quadric& quadric;
	const Box& box;
	double enough;
	double hopeless;
};

double boxCentre(const Box& box, int axis) {
	return (box.axes[axis].lower + box.axes[axis].upper) / 2;
}

double boxHalf(const Box& box, int axis) {
	return (box.axes[axis].upper - box.axes[axis].lower) / 2;
}

math::HyperDual hyperDualAtCentre(const Program& program, const Box& box) {
	const math::HyperDual zero = math::constantAs<math::HyperDual>(0);

	const Inputs<math::HyperDual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::HyperDual>(boxCentre(box, 0), 0), math::coordinateAs<math::HyperDual>(boxCentre(box, 1), 1), math::coordinateAs<math::HyperDual>(boxCentre(box, 2), 2) },
		.n         = { zero, zero, zero },
	};

	thread_local std::vector<math::HyperDual> registers;
	return evaluate(program, inputs, registers);
}

math::IntervalHyperDual intervalHyperDualOver(const Program& program, const Box& box) {
	const math::IntervalHyperDual zero = math::constantAs<math::IntervalHyperDual>(0);

	const Inputs<math::IntervalHyperDual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateOver(box.axes[0], 0), math::coordinateOver(box.axes[1], 1), math::coordinateOver(box.axes[2], 2) },
		.n         = { zero, zero, zero },
	};

	thread_local std::vector<math::IntervalHyperDual> registers;
	return evaluate(program, inputs, registers);
}

Quadric taylorQuadric(const Program& program, const Box& box) {
	const math::HyperDual f = hyperDualAtCentre(program, box);
	const double* g         = f.gradient;
	const double* h         = f.hessian;
	const double x          = boxHalf(box, 0);
	const double y          = boxHalf(box, 1);
	const double z          = boxHalf(box, 2);
	return Quadric {
		.coefficients = { x * x * h[0] / 2, y * y * h[3] / 2, z * z * h[5] / 2, x * y * h[1], x * z * h[2], y * z * h[4], x * g[0], y * g[1], z * g[2], f.value },
	};
}

double quadricValue(const Quadric& quadric, const double* u) {
	const double* q     = quadric.coefficients;
	const double square = q[0] * u[0] * u[0] + q[1] * u[1] * u[1] + q[2] * u[2] * u[2];
	const double cross  = q[3] * u[0] * u[1] + q[4] * u[0] * u[2] + q[5] * u[1] * u[2];
	return square + cross + q[6] * u[0] + q[7] * u[1] + q[8] * u[2] + q[9];
}

double quadricSlope(const Quadric& quadric, const double* u, int axis) {
	const double* q        = quadric.coefficients;
	const double square[3] = { q[0], q[1], q[2] };
	const double cross[3]  = { q[3] * u[1] + q[4] * u[2], q[3] * u[0] + q[5] * u[2], q[4] * u[0] + q[5] * u[1] };
	return 2 * square[axis] * u[axis] + cross[axis] + q[6 + axis];
}

double quadricCurvature(const Quadric& quadric, const Box& box, int k) {
	const int i       = quadricRow[k];
	const int j       = quadricColumn[k];
	const double unit = i == j ? 2 * quadric.coefficients[k] : quadric.coefficients[k];
	return unit / (boxHalf(box, i) * boxHalf(box, j));
}

double deviation(const math::Interval& range, double value) {
	return std::max(std::abs(range.upper - value), std::abs(range.lower - value));
}

double unitCoordinate(const Box& box, const Box& piece, int axis) {
	return (boxCentre(piece, axis) - boxCentre(box, axis)) / boxHalf(box, axis);
}

math::Dual dualAtCentre(const Program& program, const Box& box) {
	const math::Dual zero = math::constantAs<math::Dual>(0);

	const Inputs<math::Dual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::Dual>(boxCentre(box, 0), 0), math::coordinateAs<math::Dual>(boxCentre(box, 1), 1), math::coordinateAs<math::Dual>(boxCentre(box, 2), 2) },
		.n         = { zero, zero, zero },
	};

	thread_local std::vector<math::Dual> registers;
	return evaluate(program, inputs, registers);
}

double centreError(const Remainder& remainder, const Box& piece) {
	const math::Dual f = dualAtCentre(remainder.program, piece);
	const double u[3]  = { unitCoordinate(remainder.box, piece, 0), unitCoordinate(remainder.box, piece, 1), unitCoordinate(remainder.box, piece, 2) };
	double error       = std::abs(f.value - quadricValue(remainder.quadric, u));
	for (int i = 0; i < 3; ++i) {
		const double slope = quadricSlope(remainder.quadric, u, i) / boxHalf(remainder.box, i);
		error += boxHalf(piece, i) * std::abs(f.gradient[i] - slope);
	}
	return error;
}

double curvatureError(const Remainder& remainder, const Box& piece) {
	const math::IntervalHyperDual f = intervalHyperDualOver(remainder.program, piece);
	double error                    = 0;
	for (int k = 0; k < 6; ++k) {
		const double weight = boxHalf(piece, quadricRow[k]) * boxHalf(piece, quadricColumn[k]);
		const double half   = quadricRow[k] == quadricColumn[k] ? 0.5 : 1.0;
		error += half * weight * deviation(f.hessian[hessianSlot[k]], quadricCurvature(remainder.quadric, remainder.box, k));
	}
	return error;
}

double pieceBound(const Remainder& remainder, const Box& piece) {
	const double bound = centreError(remainder, piece) + curvatureError(remainder, piece);
	return std::isnan(bound) ? std::numeric_limits<double>::infinity() : bound;
}

double refinedBound(const Remainder& remainder, const Box& piece, int depth) {
	const double own = pieceBound(remainder, piece);
	if (own <= remainder.enough || depth == 0) {
		return own;
	}
	double worst = 0;
	for (int index = 0; index < 8; ++index) {
		worst = std::max(worst, refinedBound(remainder, octantBox(piece, index), depth - 1));
		if (worst > remainder.hopeless) {
			return own;
		}
	}
	return std::min(own, worst);
}

double roundingSlack(const Quadric& quadric) {
	double size = 1;
	for (const double coefficient : quadric.coefficients) {
		size += std::abs(coefficient);
	}
	return roundingRelative * size;
}

double boundWithin(const Remainder& remainder) {
	return refinedBound(remainder, remainder.box, refinements) + roundingSlack(remainder.quadric);
}

double quadricBound(const Program& program, const Quadric& quadric, const Box& box) {
	const Remainder remainder = {
		.program  = program,
		.quadric  = quadric,
		.box      = box,
		.enough   = 0,
		.hopeless = std::numeric_limits<double>::infinity(),
	};

	return boundWithin(remainder);
}

double provenBound(const Proof& proof) {
	const double limit = proof.limit - roundingSlack(proof.quadric);

	const Remainder remainder = {
		.program  = proof.program,
		.quadric  = proof.quadric,
		.box      = proof.box,
		.enough   = limit,
		.hopeless = limit,
	};

	return boundWithin(remainder);
}

Quadric cornerQuadric(const Quadric& quadric) {
	const double* q = quadric.coefficients;
	return Quadric {
		.coefficients = {
			4 * q[0],
			4 * q[1],
			4 * q[2],
			4 * q[3],
			4 * q[4],
			4 * q[5],
			2 * q[6] - 4 * q[0] - 2 * q[3] - 2 * q[4],
			2 * q[7] - 4 * q[1] - 2 * q[3] - 2 * q[5],
			2 * q[8] - 4 * q[2] - 2 * q[4] - 2 * q[5],
			q[9] + (q[0] + q[1] + q[2]) + (q[3] + q[4] + q[5]) - (q[6] + q[7] + q[8]),
		},
	};
}

}
