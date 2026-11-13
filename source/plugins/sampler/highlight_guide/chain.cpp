#include "plugins/sampler/highlight_guide/chain.h"

#include "sdf/numbers.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kodanuki {

constexpr int chainProjections    = 2;
constexpr int chainHalvings       = 6;
constexpr int chainIterations     = 16;
constexpr double chainReach       = 0.1;
constexpr double chainSettled     = 1e-10;
constexpr double chainAccepted    = 1e-6;
constexpr double chainGrounded    = 1e-6;
constexpr double chainLeastPivot  = 1e-12;
constexpr double chainLeastStrain = 1e-30;

struct Matrix3 {
	std::array<std::array<double, 3>, 3> at;
};

using Square2 = std::array<std::array<double, 2>, 2>;

struct ChainVertex {
	math::Vector point;
	double value;
	double strength;
	math::Vector normal;
	std::array<math::Vector, 2> tangents;
	Matrix3 hessian;
};

struct Bend {
	std::array<double, 2> residual;
	Square2 slope;
	std::array<math::Vector, 2> start;
	std::array<math::Vector, 2> lamp;
	bool sided;
};

struct LampPlace {
	bool far;
	math::Vector place;
};

Matrix3 acrossOf(const math::Vector& unit, double scale) {
	Matrix3 made = {
		.at = {},
	};

	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			const double same    = row == column ? 1 : 0;
			made.at[row][column] = (same - math::component(unit, row) * math::component(unit, column)) * scale;
		}
	}
	return made;
}

Matrix3 identityMatrix3() {
	const math::Vector none = { 0, 0, 0 };
	return acrossOf(none, 1);
}

Matrix3 zeroMatrix() {
	const math::Vector none = { 0, 0, 0 };
	return acrossOf(none, 0);
}

Matrix3 outerOf(const math::Vector& v) {
	Matrix3 made = zeroMatrix();
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			made.at[row][column] = math::component(v, row) * math::component(v, column);
		}
	}
	return made;
}

Matrix3 productOf(const Matrix3& a, const Matrix3& b) {
	Matrix3 made = zeroMatrix();
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			for (int inner = 0; inner < 3; ++inner) {
				made.at[row][column] += a.at[row][inner] * b.at[inner][column];
			}
		}
	}
	return made;
}

Matrix3 summedMatrix(const Matrix3& a, double left, const Matrix3& b) {
	Matrix3 made = zeroMatrix();
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			made.at[row][column] = left * a.at[row][column] + b.at[row][column];
		}
	}
	return made;
}

Matrix3 scaledMatrix(const Matrix3& a, double factor) {
	return summedMatrix(a, factor, zeroMatrix());
}

math::Vector rowTimes(const math::Vector& v, const Matrix3& m) {
	return {
		v.x * m.at[0][0] + v.y * m.at[1][0] + v.z * m.at[2][0],
		v.x * m.at[0][1] + v.y * m.at[1][1] + v.z * m.at[2][1],
		v.x * m.at[0][2] + v.y * m.at[1][2] + v.z * m.at[2][2],
	};
}

Matrix3 hessianMatrix(const math::HyperDual& read) {
	const double* h = read.hessian;

	const Matrix3 made = {
		.at = { { { h[0], h[1], h[2] }, { h[1], h[3], h[4] }, { h[2], h[4], h[5] } } },
	};

	return made;
}

math::HyperDual leafRead(const Program& leaf, const math::Vector& point) {
	const math::HyperDual zero = math::constantAs<math::HyperDual>(0);

	const Inputs<math::HyperDual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::HyperDual>(point.x, 0), math::coordinateAs<math::HyperDual>(point.y, 1), math::coordinateAs<math::HyperDual>(point.z, 2) },
		.n         = { zero, zero, zero },
	};

	thread_local std::vector<math::HyperDual> registers;
	return evaluate(leaf, inputs, registers);
}

math::Vector gradientOf(const math::HyperDual& read) {
	return { read.gradient[0], read.gradient[1], read.gradient[2] };
}

math::Dual leafSlope(const Program& leaf, const math::Vector& point) {
	const math::Dual zero = math::constantAs<math::Dual>(0);

	const Inputs<math::Dual> inputs = {
		.arguments = nullptr,
		.p         = { math::coordinateAs<math::Dual>(point.x, 0), math::coordinateAs<math::Dual>(point.y, 1), math::coordinateAs<math::Dual>(point.z, 2) },
		.n         = { zero, zero, zero },
	};

	thread_local std::vector<math::Dual> registers;
	return evaluate(leaf, inputs, registers);
}

ChainVertex chainVertexAt(const Program& leaf, const math::Vector& point) {
	const math::HyperDual read  = leafRead(leaf, point);
	const math::Vector gradient = gradientOf(read);
	const double strength       = math::length(gradient);
	const math::Vector normal   = gradient * (1 / strength);
	const math::Basis basis     = math::basisAround(normal);
	return ChainVertex {
		.point    = point,
		.value    = read.value,
		.strength = strength,
		.normal   = normal,
		.tangents = { basis.tangent, basis.bitangent },
		.hessian  = hessianMatrix(read),
	};
}

ChainVertex settledVertex(const Program& leaf, const math::Vector& start) {
	math::Vector point = start;
	for (int projection = 0; projection < chainProjections; ++projection) {
		const math::Dual read       = leafSlope(leaf, point);
		const math::Vector gradient = { read.gradient[0], read.gradient[1], read.gradient[2] };
		const double steepness      = math::dot(gradient, gradient);
		if (!(steepness > 0)) {
			break;
		}
		point = point - gradient * (read.value / steepness);
	}
	return chainVertexAt(leaf, point);
}

bool farLamp(const Lamp& lamp) {
	return lamp.kind == LightKind::Directional || lamp.kind == LightKind::Sun || lamp.kind == LightKind::Moon;
}

math::Vector lampMiddle(const Lamp& lamp) {
	switch (lamp.kind) {
		case LightKind::Quad: return lamp.at + (lamp.along + lamp.across) * 0.5;
		case LightKind::Directional:
		case LightKind::Moon:
		case LightKind::Sun: return math::normalized(lamp.direction);
		case LightKind::Point:
		case LightKind::Sphere: return lamp.at;
	}

	return lamp.at;
}

LampPlace lampPlaceOf(const Lamp& lamp) {
	return LampPlace {
		.far   = farLamp(lamp),
		.place = lampMiddle(lamp),
	};
}

Matrix3 lampSpreadOf(const Lamp& lamp, const math::Vector& towards) {
	const double angle = 2 * std::acos(std::clamp(lamp.cosine, -1.0, 1.0));
	switch (lamp.kind) {
		case LightKind::Sphere: return acrossOf(towards, 0.25 * lamp.radius * lamp.radius);
		case LightKind::Quad: return summedMatrix(outerOf(lamp.along), 1.0 / 12, scaledMatrix(outerOf(lamp.across), 1.0 / 12));
		case LightKind::Moon:
		case LightKind::Sun: return acrossOf(towards, angle * angle / 16);
		case LightKind::Point:
		case LightKind::Directional: return zeroMatrix();
	}

	return zeroMatrix();
}

Square2 slopeBlock(const ChainVertex& here, const Matrix3& matrix) {
	Square2 block = {};
	for (int r = 0; r < 2; ++r) {
		const math::Vector left = rowTimes(here.tangents[r], matrix);
		for (int c = 0; c < 2; ++c) {
			block[r][c] = math::dot(left, here.tangents[c]);
		}
	}
	return block;
}

std::array<math::Vector, 2> tangentRows(const ChainVertex& here, const Matrix3& matrix) {
	return { rowTimes(here.tangents[0], matrix), rowTimes(here.tangents[1], matrix) };
}

struct ChainLeg {
	math::Vector unit;
	Matrix3 turn;
};

ChainLeg legTo(const math::Vector& to) {
	const double distance   = math::length(to);
	const math::Vector unit = to * (1 / distance);
	return ChainLeg {
		.unit = unit,
		.turn = acrossOf(unit, 1 / distance),
	};
}

ChainLeg lampLeg(const LampPlace& lamp, const ChainVertex& here) {
	if (lamp.far) {
		return ChainLeg {
			.unit = lamp.place,
			.turn = zeroMatrix(),
		};
	}
	return legTo(lamp.place - here.point);
}

Bend assembledBend(const BounceChain& chain, const LampPlace& lamp, const ChainVertex& here) {
	const ChainLeg before       = legTo(chain.from - here.point);
	const ChainLeg after        = lampLeg(lamp, here);
	const Matrix3 lampTurn      = lamp.far ? identityMatrix3() : after.turn;
	const bool sided            = math::dot(before.unit, here.normal) * math::dot(after.unit, here.normal) > 0;
	const math::Vector raw      = before.unit + after.unit;
	const double side           = math::dot(raw, here.normal) < 0 ? -1 : 1;
	const math::Vector half     = raw * side;
	const double halfLength     = math::length(half);
	const math::Vector unitHalf = half * (1 / halfLength);
	const Matrix3 turnHalf      = acrossOf(unitHalf, 1 / halfLength);
	const Matrix3 turnNormal    = productOf(acrossOf(here.normal, 1 / here.strength), here.hessian);
	const math::Vector off      = unitHalf - here.normal;
	const Matrix3 byBefore      = scaledMatrix(productOf(turnHalf, before.turn), side);
	const Matrix3 byAfter       = scaledMatrix(productOf(turnHalf, after.turn), side);
	const Matrix3 byHere        = summedMatrix(summedMatrix(byBefore, 1, byAfter), -1, scaledMatrix(turnNormal, -1));
	return Bend {
		.residual = { math::dot(here.tangents[0], off), math::dot(here.tangents[1], off) },
		.slope    = slopeBlock(here, byHere),
		.start    = tangentRows(here, byBefore),
		.lamp     = tangentRows(here, scaledMatrix(productOf(turnHalf, lampTurn), side)),
		.sided    = sided,
	};
}

double mismatchOf(const Bend& bend) {
	return std::sqrt(bend.residual[0] * bend.residual[0] + bend.residual[1] * bend.residual[1]);
}

struct Solved2 {
	bool found;
	std::array<double, 2> answer;
};

Solved2 unsolvedPair() {
	return Solved2 {
		.found  = false,
		.answer = { 0, 0 },
	};
}

double largestOf(const Square2& m) {
	return std::max({ std::abs(m[0][0]), std::abs(m[0][1]), std::abs(m[1][0]), std::abs(m[1][1]) });
}

Solved2 solvedPair(const Square2& m, const std::array<double, 2>& right) {
	const double largest               = largestOf(m);
	const bool swapped                 = std::abs(m[1][0]) > std::abs(m[0][0]);
	const std::array<double, 2> top    = swapped ? m[1] : m[0];
	const std::array<double, 2> bottom = swapped ? m[0] : m[1];
	const double first                 = swapped ? right[1] : right[0];
	const double second                = swapped ? right[0] : right[1];
	if (!(std::abs(top[0]) > chainLeastPivot * largest)) {
		return unsolvedPair();
	}
	const double factor = bottom[0] / top[0];
	const double pivot  = bottom[1] - factor * top[1];
	if (!(std::abs(pivot) > chainLeastPivot * largest)) {
		return unsolvedPair();
	}
	const double y = (second - factor * first) / pivot;
	return Solved2 {
		.found  = true,
		.answer = { (first - top[1] * y) / top[0], y },
	};
}

struct Inverted2 {
	bool found;
	Square2 inverse;
};

Inverted2 invertedPair(const Square2& m) {
	const std::array<double, 2> first  = { 1, 0 };
	const std::array<double, 2> second = { 0, 1 };
	const Solved2 a                    = solvedPair(m, first);
	const Solved2 b                    = solvedPair(m, second);
	return Inverted2 {
		.found   = a.found && b.found,
		.inverse = { { { a.answer[0], b.answer[0] }, { a.answer[1], b.answer[1] } } },
	};
}

std::array<double, 2> cappedStep(const BounceChain& chain, const ChainVertex& at, const std::array<double, 2>& step) {
	const double room  = chainReach * math::length(chain.from - at.point);
	const double moved = std::hypot(step[0], step[1]);
	const double scale = moved > room ? room / moved : 1;
	return { step[0] * scale, step[1] * scale };
}

struct ChainState {
	ChainVertex at;
	Bend bend;
	double error;
};

struct ChainStep {
	bool taken;
	ChainState state;
};

ChainState chainStateAt(const BounceChain& chain, const LampPlace& lamp, const ChainVertex& at) {
	const Bend bend = assembledBend(chain, lamp, at);
	return ChainState {
		.at    = at,
		.bend  = bend,
		.error = mismatchOf(bend),
	};
}

ChainStep chainStepOf(bool taken, const ChainState& state) {
	return ChainStep {
		.taken = taken,
		.state = state,
	};
}

ChainStep halvedStep(const BounceChain& chain, const ChainState& now, const std::array<double, 2>& start) {
	const LampPlace lamp       = lampPlaceOf(chain.lamp);
	std::array<double, 2> step = start;
	ChainState next            = now;
	for (int halving = 0; halving <= chainHalvings; ++halving) {
		const math::Vector moved = now.at.point + now.at.tangents[0] * step[0] + now.at.tangents[1] * step[1];
		next                     = chainStateAt(chain, lamp, settledVertex(chain.leaf, moved));
		if (next.error <= now.error) {
			return chainStepOf(true, next);
		}
		step = { step[0] * 0.5, step[1] * 0.5 };
	}
	return chainStepOf(false, next);
}

ChainStep newtonStep(const BounceChain& chain, const ChainState& now) {
	const std::array<double, 2> right = { -now.bend.residual[0], -now.bend.residual[1] };
	const Solved2 solved              = solvedPair(now.bend.slope, right);
	if (!solved.found) {
		return chainStepOf(false, now);
	}
	return halvedStep(chain, now, cappedStep(chain, now.at, solved.answer));
}

ChainState walkedState(const BounceChain& chain, const math::Vector& seed) {
	ChainState state = chainStateAt(chain, lampPlaceOf(chain.lamp), settledVertex(chain.leaf, seed));
	for (int iteration = 0; iteration < chainIterations && state.error > chainSettled; ++iteration) {
		const ChainStep step = newtonStep(chain, state);
		if (!step.taken) {
			break;
		}
		state = step.state;
	}
	return state;
}

bool finiteVector(const math::Vector& v) {
	return std::isfinite(v.x + v.y + v.z);
}

bool chainSettledAt(const BounceChain& chain, const ChainState& state) {
	const bool outsideLamp = chain.lamp.kind != LightKind::Sphere || math::length(state.at.point - chain.lamp.at) > chain.lamp.radius;
	return state.error < chainAccepted && state.bend.sided && std::abs(state.at.value) < chainGrounded && finiteVector(state.at.point) && outsideLamp;
}

Square2 chainNoise(const BounceChain& chain, const ChainState& state) {
	const LampPlace lamp       = lampPlaceOf(chain.lamp);
	const math::Vector towards = lamp.far ? lamp.place : math::normalized(lamp.place - state.at.point);
	const Matrix3 spread       = lampSpreadOf(chain.lamp, towards);
	const double facet         = 0.5 * chain.width * chain.width;
	Square2 noise              = { { { facet, 0 }, { 0, facet } } };
	for (int row = 0; row < 2; ++row) {
		const math::Vector spreadRow = rowTimes(state.bend.lamp[row], spread);
		for (int column = 0; column < 2; ++column) {
			noise[row][column] += math::dot(spreadRow, state.bend.lamp[column]);
		}
	}
	return noise;
}

Square2 sandwiched(const Square2& outer, const Square2& inner) {
	Square2 made = {};
	for (int r = 0; r < 2; ++r) {
		for (int c = 0; c < 2; ++c) {
			for (int p = 0; p < 2; ++p) {
				for (int q = 0; q < 2; ++q) {
					made[r][c] += outer[r][p] * inner[p][q] * outer[c][q];
				}
			}
		}
	}
	return made;
}

math::Vector pushedMotion(const ChainState& state, const Square2& inverse, const math::Vector& push) {
	std::array<double, 2> change = { 0, 0 };
	for (int r = 0; r < 2; ++r) {
		for (int p = 0; p < 2; ++p) {
			change[r] -= inverse[r][p] * math::dot(state.bend.start[p], push);
		}
	}
	return state.at.tangents[0] * change[0] + state.at.tangents[1] * change[1];
}

SettledChain unsettledChain() {
	const math::Vector none = { 0, 0, 0 };
	return SettledChain {
		.found  = false,
		.point  = none,
		.axis   = none,
		.across = none,
		.up     = none,
		.spread = { 0, 0, 0 },
		.motion = { none, none },
	};
}

SettledChain settledChain(const BounceChain& chain, const ChainState& state, const Square2& inverse) {
	const Square2 moved         = sandwiched(inverse, chainNoise(chain, state));
	const math::Vector toFirst  = state.at.point - chain.from;
	const double distance       = math::length(toFirst);
	const math::Vector axis     = toFirst * (1 / distance);
	const math::Basis basis     = math::basisAround(axis);
	const math::Vector plane[2] = { basis.tangent, basis.bitangent };
	Square2 seen                = {};
	for (int r = 0; r < 2; ++r) {
		for (int c = 0; c < 2; ++c) {
			seen[r][c] = math::dot(plane[r], state.at.tangents[c]) / distance;
		}
	}
	const Square2 spread = sandwiched(seen, moved);
	return SettledChain {
		.found  = true,
		.point  = state.at.point,
		.axis   = axis,
		.across = basis.tangent,
		.up     = basis.bitangent,
		.spread = { spread[0][0], spread[0][1], spread[1][1] },
		.motion = { pushedMotion(state, inverse, chain.sideways[0]), pushedMotion(state, inverse, chain.sideways[1]) },
	};
}

bool finiteChain(const SettledChain& settled) {
	return finiteVector(settled.axis) && finiteVector(settled.motion[0]) && finiteVector(settled.motion[1]) && std::isfinite(settled.spread[0] + settled.spread[1] + settled.spread[2]);
}

SettledChain walkedChain(const BounceChain& chain, const math::Vector& seed) {
	const ChainState state = walkedState(chain, seed);
	if (!chainSettledAt(chain, state)) {
		return unsettledChain();
	}
	const Inverted2 inverted = invertedPair(state.bend.slope);
	if (!inverted.found) {
		return unsettledChain();
	}
	SettledChain settled = settledChain(chain, state, inverted.inverse);
	settled.found        = finiteChain(settled);
	return settled;
}

}
