#include "plugins/geometry/tree/patch.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr int bentCount  = 20;
constexpr int flatCount  = 8;
constexpr int watchCount = 15;
constexpr int mostSteps  = 32767;

using Powers  = std::array<std::array<int, 3>, bentCount>;
using Matrix  = std::array<std::array<double, bentCount>, bentCount>;
using Unit    = std::array<double, 3>;
using Weights = std::array<double, bentCount>;
using Grid    = std::array<std::array<std::array<double, 3>, 3>, 3>;

constexpr Powers powers = { { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 }, { 1, 1, 1 }, { 2, 0, 0 }, { 2, 1, 0 }, { 2, 0, 1 }, { 2, 1, 1 }, { 0, 2, 0 }, { 1, 2, 0 }, { 0, 2, 1 }, { 1, 2, 1 }, { 0, 0, 2 }, { 1, 0, 2 }, { 0, 1, 2 }, { 1, 1, 2 } } };

double raised(double value, int power) {
	return power == 0 ? 1.0 : (power == 1 ? value : value * value);
}

double monomial(const Unit& u, int index) {
	const std::array<int, 3>& power = powers[std::size_t(index)];
	return raised(u[0], power[0]) * raised(u[1], power[1]) * raised(u[2], power[2]);
}

Unit edgeMiddle(int axis, int side) {
	Unit node   = { 0, 0, 0 };
	int counted = 0;
	for (int k = 0; k < 3; ++k) {
		node[std::size_t(k)] = k == axis ? 0.5 : double((side >> counted++) & 1);
	}
	return node;
}

std::array<Unit, bentCount> nodeSites() {
	std::array<Unit, bentCount> nodes = {};
	for (int corner = 0; corner < 8; ++corner) {
		nodes[std::size_t(corner)] = { double(corner & 1), double((corner >> 1) & 1), double((corner >> 2) & 1) };
	}
	for (int axis = 0; axis < 3; ++axis) {
		for (int side = 0; side < 4; ++side) {
			nodes[std::size_t(8 + axis * 4 + side)] = edgeMiddle(axis, side);
		}
	}
	return nodes;
}

std::array<Unit, watchCount> watchSites() {
	std::array<Unit, watchCount> where = { { { 0.5, 0.5, 0.5 }, { 0, 0.5, 0.5 }, { 1, 0.5, 0.5 }, { 0.5, 0, 0.5 }, { 0.5, 1, 0.5 }, { 0.5, 0.5, 0 }, { 0.5, 0.5, 1 } } };
	for (int octant = 0; octant < 8; ++octant) {
		where[std::size_t(7 + octant)] = { (octant & 1) != 0 ? 0.75 : 0.25, (octant & 2) != 0 ? 0.75 : 0.25, (octant & 4) != 0 ? 0.75 : 0.25 };
	}
	return where;
}

Matrix nodeMatrix() {
	const std::array<Unit, bentCount> nodes = nodeSites();
	Matrix matrix                           = {};
	for (std::size_t row = 0; row < bentCount; ++row) {
		for (int column = 0; column < bentCount; ++column) {
			matrix[row][std::size_t(column)] = monomial(nodes[row], column);
		}
	}
	return matrix;
}

Matrix identityMatrix() {
	Matrix identity = {};
	for (std::size_t k = 0; k < bentCount; ++k) {
		identity[k][k] = 1;
	}
	return identity;
}

std::size_t pivotRow(const Matrix& left, std::size_t column) {
	std::size_t best = column;
	for (std::size_t row = column + 1; row < bentCount; ++row) {
		best = std::abs(left[row][column]) > std::abs(left[best][column]) ? row : best;
	}
	return best;
}

void eliminate(Matrix& left, Matrix& right, std::size_t column) {
	for (std::size_t row = 0; row < bentCount; ++row) {
		const double factor = left[row][column];
		if (row == column || factor == 0) {
			continue;
		}
		for (std::size_t k = 0; k < bentCount; ++k) {
			left[row][k] -= factor * left[column][k];
			right[row][k] -= factor * right[column][k];
		}
	}
}

Matrix invertedNodes() {
	Matrix left  = nodeMatrix();
	Matrix right = identityMatrix();
	for (std::size_t column = 0; column < bentCount; ++column) {
		const std::size_t best = pivotRow(left, column);
		std::swap(left[column], left[best]);
		std::swap(right[column], right[best]);
		const double pivot = left[column][column];
		for (std::size_t k = 0; k < bentCount; ++k) {
			left[column][k] /= pivot;
			right[column][k] /= pivot;
		}
		eliminate(left, right, column);
	}
	return right;
}

const Matrix& fitMatrix() {
	static const Matrix matrix = invertedNodes();
	return matrix;
}

const std::array<Unit, bentCount>& nodes() {
	static const std::array<Unit, bentCount> sites = nodeSites();
	return sites;
}

const std::array<Unit, watchCount>& watchers() {
	static const std::array<Unit, watchCount> sites = watchSites();
	return sites;
}

double placedCoordinate(const Box& box, const Unit& u, int axis) {
	const math::Interval& span = box.axes[axis];
	return span.lower + (span.upper - span.lower) * u[std::size_t(axis)];
}

Readings patchReadings(const Box& box) {
	Readings readings = {
		.points = {},
	};

	for (std::size_t k = 0; k < 35; ++k) {
		const Unit& u = k < bentCount ? nodes()[k] : watchers()[k - bentCount];
		for (int axis = 0; axis < 3; ++axis) {
			readings.points[k][axis] = placedCoordinate(box, u, axis);
		}
	}
	return readings;
}

std::array<double, 35> fieldReadings(const PatchQuery& query) {
	const Readings readings = patchReadings(query.box);

	const PointBatch batch = {
		.points    = readings.points,
		.count     = 35,
		.arguments = nullptr,
	};

	std::array<double, 35> values;
	values.fill(std::numeric_limits<double>::infinity());
	thread_local std::vector<double> registers;
	for (const std::shared_ptr<const Program>& program : query.programs) {
		const double* field = evaluatePoints(*program, batch, registers);
		for (std::size_t k = 0; k < 35; ++k) {
			values[k] = std::min(values[k], field[k]);
		}
	}
	return values;
}

std::int16_t countedSteps(double value, float step) {
	if (!(step > 0.0f)) {
		return 0;
	}
	const double steps = std::nearbyint(value / double(step));
	return std::int16_t(std::clamp(steps, double(-mostSteps), double(mostSteps)));
}

Weights solvedWeights(const std::array<double, 35>& values) {
	Weights totals = {};
	for (std::size_t row = 0; row < bentCount; ++row) {
		for (std::size_t column = 0; column < bentCount; ++column) {
			totals[row] += fitMatrix()[row][column] * values[column];
		}
	}
	return totals;
}

Patch roundedPatch(const Weights& totals) {
	double largest = 0;
	for (const double total : totals) {
		largest = std::max(largest, std::abs(total));
	}

	Patch patch = {
		.weights = {},
		.step    = float(largest / mostSteps),
		.bent    = false,
	};

	for (std::size_t k = 0; k < bentCount; ++k) {
		patch.weights[k] = float(countedSteps(totals[k], patch.step)) * patch.step;
		patch.bent       = patch.bent || (k >= flatCount && patch.weights[k] != 0.0f);
	}
	return patch;
}

double patchValueAt(const Patch& patch, const Unit& u) {
	return patchValue(patch, u.data());
}

double strayOf(const Patch& patch, const std::array<double, 35>& values) {
	double stray = 0;
	for (std::size_t k = 0; k < watchCount; ++k) {
		stray = std::max(stray, std::abs(values[bentCount + k] - patchValueAt(patch, watchers()[k])));
	}
	return std::isnan(stray) ? std::numeric_limits<double>::infinity() : stray;
}

Fitted fittedTo(const std::array<double, 35>& values) {
	const Patch patch = roundedPatch(solvedWeights(values));
	return Fitted {
		.patch = patch,
		.stray = strayOf(patch, values),
	};
}

Fitted fitPatch(const PatchQuery& query) {
	return fittedTo(fieldReadings(query));
}

double joinedStray(const SeamFit& fit, const std::array<double, 35>& whole, bool larger) {
	double stray = 0;
	for (std::size_t k = 0; k < watchCount; ++k) {
		const double left  = patchValueAt(fit.left, watchers()[k]);
		const double right = patchValueAt(fit.right, watchers()[k]);
		const double both  = larger ? std::max(left, right) : std::min(left, right);
		stray              = std::max(stray, std::abs(whole[bentCount + k] - both));
	}
	return std::isnan(stray) ? std::numeric_limits<double>::infinity() : stray;
}

SeamFit fitSeam(const SeamQuery& query) {
	const PatchQuery left = {
		.programs = query.left,
		.box      = query.box,
	};

	const PatchQuery right = {
		.programs = query.right,
		.box      = query.box,
	};

	const Fitted one   = fitPatch(left);
	const Fitted other = fitPatch(right);

	SeamFit fit = {
		.left  = one.patch,
		.right = other.patch,
		.stray = 0,
	};

	fit.stray = std::max({ joinedStray(fit, query.whole, query.larger), one.stray, other.stray });
	return fit;
}

double patchValue(const Patch& patch, const double* u) {
	const Unit at = { u[0], u[1], u[2] };
	double total  = 0;
	for (int k = 0; k < bentCount; ++k) {
		total += double(patch.weights[k]) * monomial(at, k);
	}
	return total;
}

double& gridAt(Grid& grid, const std::array<int, 3>& at) {
	return grid[std::size_t(at[0])][std::size_t(at[1])][std::size_t(at[2])];
}

std::array<int, 3> gridIndex(int axis, int step, const std::array<int, 2>& across) {
	switch (axis) {
		case 0: return { step, across[0], across[1] };
		case 1: return { across[0], step, across[1] };
		default: return { across[0], across[1], step };
	}
}

void bernsteinAlong(Grid& grid, int axis) {
	for (int first = 0; first < 3; ++first) {
		for (int second = 0; second < 3; ++second) {
			double& a0      = gridAt(grid, gridIndex(axis, 0, { first, second }));
			double& a1      = gridAt(grid, gridIndex(axis, 1, { first, second }));
			double& a2      = gridAt(grid, gridIndex(axis, 2, { first, second }));
			const double b1 = a0 + 0.5 * a1;
			const double b2 = (a0 + a1) + a2;
			a1              = b1;
			a2              = b2;
		}
	}
}

Triquadratic patchTriquadratic(const Patch& patch) {
	Triquadratic polynomial = {
		.terms = {},
	};

	for (std::size_t k = 0; k < bentCount; ++k) {
		const std::array<int, 3>& power = powers[k];
		gridAt(polynomial.terms, power) = double(patch.weights[k]);
	}
	return polynomial;
}

math::Interval triquadraticNet(const Triquadratic& polynomial) {
	Grid grid = polynomial.terms;
	for (int axis = 0; axis < 3; ++axis) {
		bernsteinAlong(grid, axis);
	}

	math::Interval net = {
		.lower = grid[0][0][0],
		.upper = grid[0][0][0],
	};

	for (const std::array<std::array<double, 3>, 3>& plane : grid) {
		for (const std::array<double, 3>& row : plane) {
			for (const double value : row) {
				net.lower = std::min(net.lower, value);
				net.upper = std::max(net.upper, value);
			}
		}
	}
	return net;
}

math::Interval patchNet(const Patch& patch) {
	return triquadraticNet(patchTriquadratic(patch));
}

std::uint32_t floatWord(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

std::uint32_t stepsWord(const Patch& patch, int first) {
	const std::uint16_t a = std::uint16_t(countedSteps(double(patch.weights[first]), patch.step));
	const std::uint16_t b = std::uint16_t(countedSteps(double(patch.weights[first + 1]), patch.step));
	return std::uint32_t(a) | (std::uint32_t(b) << 16);
}

std::vector<std::uint32_t> patchWords(const Patch& patch) {
	const int count = patch.bent ? bentCount : flatCount;
	std::vector<std::uint32_t> words;
	words.push_back(floatWord(std::copysign(patch.step, patch.bent ? -1.0f : 1.0f)));
	for (int k = 0; k < count; k += 2) {
		words.push_back(stepsWord(patch, k));
	}
	return words;
}

float wordSteps(std::uint32_t word, int half, float step) {
	const std::int16_t steps = std::bit_cast<std::int16_t>(std::uint16_t(word >> (16 * half)));
	return float(steps) * step;
}

Patch readPatch(const std::uint32_t* words) {
	const float signedStep = std::bit_cast<float>(words[0]);

	Patch patch = {
		.weights = {},
		.step    = std::abs(signedStep),
		.bent    = std::signbit(signedStep),
	};

	const int count = patch.bent ? bentCount : flatCount;
	for (int k = 0; k < count; ++k) {
		patch.weights[k] = wordSteps(words[1 + k / 2], k % 2, patch.step);
	}
	return patch;
}

std::uint32_t patchWordCount(const std::uint32_t* words) {
	const bool bent = std::signbit(std::bit_cast<float>(words[0]));
	return 1 + (bent ? bentCount : flatCount) / 2;
}

}
