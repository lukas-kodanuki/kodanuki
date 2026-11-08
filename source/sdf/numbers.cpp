#include "sdf/numbers.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace kodanuki::math {

constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double pi       = 3.14159265358979323846;

constexpr int hessianRow[6]    = { 0, 0, 0, 1, 1, 2 };
constexpr int hessianColumn[6] = { 0, 1, 2, 1, 2, 2 };

Dual constantDual(double value) {
	return Dual {
		.value    = value,
		.gradient = { 0, 0, 0 },
	};
}

HyperDual constantHyperDual(double value) {
	return HyperDual {
		.value    = value,
		.gradient = { 0, 0, 0 },
		.hessian  = { 0, 0, 0, 0, 0, 0 },
	};
}

template <>
double constantAs<double>(double value) {
	return value;
}

template <>
Interval constantAs<Interval>(double value) {
	return Interval {
		.lower = value,
		.upper = value,
	};
}

template <>
Dual constantAs<Dual>(double value) {
	return constantDual(value);
}

template <>
HyperDual constantAs<HyperDual>(double value) {
	return constantHyperDual(value);
}

template <>
Batch constantAs<Batch>(double value) {
	Batch batch = {
		.at = {},
	};

	for (double& lane : batch.at) {
		lane = value;
	}
	return batch;
}

template <>
DualBatch constantAs<DualBatch>(double value) {
	DualBatch batch = {
		.at = {},
	};

	for (Dual& lane : batch.at) {
		lane = constantDual(value);
	}
	return batch;
}

template <>
double coordinateAs<double>(double value, int) {
	return value;
}

template <>
Interval coordinateAs<Interval>(double value, int) {
	return Interval {
		.lower = value,
		.upper = value,
	};
}

template <>
Dual coordinateAs<Dual>(double value, int axis) {
	Dual dual = constantDual(value);

	dual.gradient[axis] = 1;
	return dual;
}

template <>
HyperDual coordinateAs<HyperDual>(double value, int axis) {
	HyperDual dual = constantHyperDual(value);

	dual.gradient[axis] = 1;
	return dual;
}

double sqrt(double a) {
	return std::sqrt(a);
}

double abs(double a) {
	return std::abs(a);
}

double floor(double a) {
	return std::floor(a);
}

double sin(double a) {
	return std::sin(a);
}

double cos(double a) {
	return std::cos(a);
}

double sign(double a) {
	return a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0);
}

double atan2(double y, double x) {
	return std::atan2(y, x);
}

double pow(double a, double b) {
	return std::pow(a, b);
}

double min(double a, double b) {
	return std::min(a, b);
}

double max(double a, double b) {
	return std::max(a, b);
}

double truth(bool b) {
	return b ? 1.0 : 0.0;
}

double compare(Compare kind, double a, double b) {
	switch (kind) {
		case Compare::Less: return truth(a < b);
		case Compare::LessEqual: return truth(a <= b);
		case Compare::Equal: return truth(a == b);
		case Compare::NotEqual: return truth(a != b);
		case Compare::And: return truth(a != 0 && b != 0);
		case Compare::Or: return truth(a != 0 || b != 0);
	}

	return 0;
}

double choose(double condition, double a, double b) {
	return condition != 0 ? a : b;
}

double smoothMinimum(double a, double b, double r) {
	const double blend = std::max(r - std::abs(a - b), 0.0) / r;
	return std::min(a, b) - blend * blend * r * 0.25;
}

double laneValue(double a, int) {
	return a;
}

double laneValue(const Dual& a, int) {
	return a.value;
}

double laneValue(const DualBatch& a, int lane) {
	return a.at[lane].value;
}

double positiveSquare(double a) {
	const double positive = std::max(a, 0.0);
	return positive * positive;
}

double blendSlope(double d, double r) {
	return std::clamp((r - d) / (2 * r), 0.0, 1.0);
}

double bandCurvature(double d, double r) {
	return std::abs(d) < r ? 1 / (2 * r) : 0;
}

Interval hull(const Interval& a, const Interval& b) {
	return Interval {
		.lower = std::min(a.lower, b.lower),
		.upper = std::max(a.upper, b.upper),
	};
}

Interval hullOf(std::initializer_list<double> values) {
	return Interval {
		.lower = std::min(values),
		.upper = std::max(values),
	};
}

Interval operator+(const Interval& a, const Interval& b) {
	return Interval {
		.lower = a.lower + b.lower,
		.upper = a.upper + b.upper,
	};
}

Interval operator-(const Interval& a, const Interval& b) {
	return Interval {
		.lower = a.lower - b.upper,
		.upper = a.upper - b.lower,
	};
}

Interval operator*(const Interval& a, const Interval& b) {
	return hullOf({ a.lower * b.lower, a.lower * b.upper, a.upper * b.lower, a.upper * b.upper });
}

bool containsZero(const Interval& a) {
	return a.lower <= 0 && a.upper >= 0;
}

Interval operator/(const Interval& a, const Interval& b) {
	if (containsZero(b)) {
		return Interval {
			.lower = -infinity,
			.upper = infinity,
		};
	}
	return a * Interval {
		.lower = 1 / b.upper,
		.upper = 1 / b.lower,
	};
}

Interval operator-(const Interval& a) {
	return Interval {
		.lower = -a.upper,
		.upper = -a.lower,
	};
}

Interval sqrt(const Interval& a) {
	return Interval {
		.lower = std::sqrt(std::max(a.lower, 0.0)),
		.upper = std::sqrt(std::max(a.upper, 0.0)),
	};
}

Interval abs(const Interval& a) {
	if (a.lower >= 0) {
		return a;
	}
	if (a.upper <= 0) {
		return -a;
	}
	return Interval {
		.lower = 0,
		.upper = std::max(-a.lower, a.upper),
	};
}

Interval floor(const Interval& a) {
	return Interval {
		.lower = std::floor(a.lower),
		.upper = std::floor(a.upper),
	};
}

bool reaches(const Interval& a, double offset) {
	const double k = std::ceil((a.lower - offset) / (2 * pi));
	return offset + 2 * pi * k <= a.upper;
}

Interval sin(const Interval& a) {
	if (a.upper - a.lower >= 2 * pi) {
		return Interval {
			.lower = -1,
			.upper = 1,
		};
	}
	Interval out = hullOf({ std::sin(a.lower), std::sin(a.upper) });
	out.upper    = reaches(a, pi / 2) ? 1 : out.upper;
	out.lower    = reaches(a, -pi / 2) ? -1 : out.lower;
	return out;
}

Interval cos(const Interval& a) {
	return sin(a + Interval {
					   .lower = pi / 2,
					   .upper = pi / 2,
				   });
}

Interval sign(const Interval& a) {
	return Interval {
		.lower = sign(a.lower),
		.upper = sign(a.upper),
	};
}

Interval atan2(const Interval& y, const Interval& x) {
	const bool nearOrigin = containsZero(x) && containsZero(y);
	const bool acrossCut  = x.lower < 0 && containsZero(y);
	if (nearOrigin || acrossCut) {
		return Interval {
			.lower = -pi,
			.upper = pi,
		};
	}
	return hullOf({ std::atan2(y.lower, x.lower), std::atan2(y.lower, x.upper), std::atan2(y.upper, x.lower), std::atan2(y.upper, x.upper) });
}

bool isWhole(double a) {
	return a == std::floor(a);
}

Interval wholePower(const Interval& a, double n) {
	if (n >= 0 && std::fmod(n, 2.0) == 0) {
		const Interval magnitude = abs(a);
		return Interval {
			.lower = std::pow(magnitude.lower, n),
			.upper = std::pow(magnitude.upper, n),
		};
	}
	if (n > 0) {
		return Interval {
			.lower = std::pow(a.lower, n),
			.upper = std::pow(a.upper, n),
		};
	}
	return constantAs<Interval>(1) / wholePower(a, -n);
}

Interval pow(const Interval& a, const Interval& b) {
	if (b.lower == b.upper && isWhole(b.lower)) {
		return wholePower(a, b.lower);
	}
	if (a.lower <= 0) {
		return Interval {
			.lower = -infinity,
			.upper = infinity,
		};
	}
	return hullOf({ std::pow(a.lower, b.lower), std::pow(a.lower, b.upper), std::pow(a.upper, b.lower), std::pow(a.upper, b.upper) });
}

Interval min(const Interval& a, const Interval& b) {
	return Interval {
		.lower = std::min(a.lower, b.lower),
		.upper = std::min(a.upper, b.upper),
	};
}

Interval max(const Interval& a, const Interval& b) {
	return Interval {
		.lower = std::max(a.lower, b.lower),
		.upper = std::max(a.upper, b.upper),
	};
}

Interval decided(bool yes, bool no) {
	return Interval {
		.lower = yes ? 1.0 : 0.0,
		.upper = no ? 0.0 : 1.0,
	};
}

bool excludesZero(const Interval& a) {
	return a.lower > 0 || a.upper < 0;
}

bool isZero(const Interval& a) {
	return a.lower == 0 && a.upper == 0;
}

Interval compare(Compare kind, const Interval& a, const Interval& b) {
	const bool apart = a.upper < b.lower || b.upper < a.lower;
	const bool same  = a.lower == a.upper && b.lower == b.upper && a.lower == b.lower;
	switch (kind) {
		case Compare::Less: return decided(a.upper < b.lower, a.lower >= b.upper);
		case Compare::LessEqual: return decided(a.upper <= b.lower, a.lower > b.upper);
		case Compare::Equal: return decided(same, apart);
		case Compare::NotEqual: return decided(apart, same);
		case Compare::And: return decided(excludesZero(a) && excludesZero(b), isZero(a) || isZero(b));
		case Compare::Or: return decided(excludesZero(a) || excludesZero(b), isZero(a) && isZero(b));
	}

	return decided(false, false);
}

Interval choose(const Interval& condition, const Interval& a, const Interval& b) {
	if (excludesZero(condition)) {
		return a;
	}
	if (isZero(condition)) {
		return b;
	}
	return hull(a, b);
}

double settledValue(double value, double fallback) {
	return std::isnan(value) ? fallback : value;
}

Interval smoothMinimum(const Interval& a, const Interval& b, const Interval& r) {
	const double lower = smoothMinimum(a.lower, b.lower, r.lower);
	const double upper = smoothMinimum(a.upper, b.upper, r.lower);
	return Interval {
		.lower = settledValue(lower, std::min(a.lower, b.lower) - r.lower / 4),
		.upper = settledValue(upper, std::min(a.upper, b.upper)),
	};
}

Interval positiveSquare(const Interval& a) {
	return Interval {
		.lower = positiveSquare(a.lower),
		.upper = positiveSquare(a.upper),
	};
}

Dual chainDual(const Dual& a, double f, double d1) {
	Dual out = constantDual(f);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = d1 * a.gradient[i];
	}
	return out;
}

struct DualPartials {
	double f;
	double fa;
	double fb;
};

Dual chainDual2(const Dual& a, const Dual& b, const DualPartials& d) {
	Dual out = constantDual(d.f);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = d.fa * a.gradient[i] + d.fb * b.gradient[i];
	}
	return out;
}

Dual operator+(const Dual& a, const Dual& b) {
	const DualPartials partials = {
		.f  = a.value + b.value,
		.fa = 1,
		.fb = 1,
	};

	return chainDual2(a, b, partials);
}

Dual operator-(const Dual& a, const Dual& b) {
	const DualPartials partials = {
		.f  = a.value - b.value,
		.fa = 1,
		.fb = -1,
	};

	return chainDual2(a, b, partials);
}

Dual operator*(const Dual& a, const Dual& b) {
	const DualPartials partials = {
		.f  = a.value * b.value,
		.fa = b.value,
		.fb = a.value,
	};

	return chainDual2(a, b, partials);
}

Dual operator/(const Dual& a, const Dual& b) {
	const DualPartials partials = {
		.f  = a.value / b.value,
		.fa = 1 / b.value,
		.fb = -a.value / (b.value * b.value),
	};

	return chainDual2(a, b, partials);
}

Dual operator-(const Dual& a) {
	return chainDual(a, -a.value, -1);
}

Dual sqrt(const Dual& a) {
	const double f = std::sqrt(a.value);
	return chainDual(a, f, f > 0 ? 0.5 / f : 0);
}

Dual abs(const Dual& a) {
	return a.value < 0 ? -a : a;
}

Dual floor(const Dual& a) {
	return constantDual(std::floor(a.value));
}

Dual sin(const Dual& a) {
	return chainDual(a, std::sin(a.value), std::cos(a.value));
}

Dual cos(const Dual& a) {
	return chainDual(a, std::cos(a.value), -std::sin(a.value));
}

Dual sign(const Dual& a) {
	return constantDual(sign(a.value));
}

Dual atan2(const Dual& y, const Dual& x) {
	const double r = x.value * x.value + y.value * y.value;

	const DualPartials partials = {
		.f  = std::atan2(y.value, x.value),
		.fa = x.value / r,
		.fb = -y.value / r,
	};

	return chainDual2(y, x, partials);
}

double logOrZero(double a) {
	return a > 0 ? std::log(a) : 0;
}

Dual pow(const Dual& a, const Dual& b) {
	const double f  = std::pow(a.value, b.value);
	const double fa = b.value * std::pow(a.value, b.value - 1);

	const DualPartials partials = {
		.f  = f,
		.fa = fa,
		.fb = f * logOrZero(a.value),
	};

	return chainDual2(a, b, partials);
}

Dual min(const Dual& a, const Dual& b) {
	return a.value <= b.value ? a : b;
}

Dual max(const Dual& a, const Dual& b) {
	return a.value >= b.value ? a : b;
}

Dual compare(Compare kind, const Dual& a, const Dual& b) {
	return constantDual(compare(kind, a.value, b.value));
}

Dual choose(const Dual& condition, const Dual& a, const Dual& b) {
	return condition.value != 0 ? a : b;
}

Dual smoothMinimum(const Dual& a, const Dual& b, const Dual& r) {
	const double fa = blendSlope(a.value - b.value, r.value);

	const DualPartials partials = {
		.f  = smoothMinimum(a.value, b.value, r.value),
		.fa = fa,
		.fb = 1 - fa,
	};

	return chainDual2(a, b, partials);
}

Dual positiveSquare(const Dual& a) {
	return chainDual(a, positiveSquare(a.value), 2 * std::max(a.value, 0.0));
}

struct Derivatives {
	double f;
	double d1;
	double d2;
};

HyperDual chainHyper(const HyperDual& a, const Derivatives& d) {
	HyperDual out = constantHyperDual(d.f);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = d.d1 * a.gradient[i];
	}
	for (int k = 0; k < 6; ++k) {
		const double outer = a.gradient[hessianRow[k]] * a.gradient[hessianColumn[k]];
		out.hessian[k]     = d.d1 * a.hessian[k] + d.d2 * outer;
	}
	return out;
}

struct Partials {
	double f;
	double fa;
	double fb;
	double faa;
	double fab;
	double fbb;
};

double hessianEntry(const HyperDual& a, const HyperDual& b, const Partials& d, int k) {
	const int i      = hessianRow[k];
	const int j      = hessianColumn[k];
	const double ga  = a.gradient[i] * a.gradient[j];
	const double gab = a.gradient[i] * b.gradient[j] + b.gradient[i] * a.gradient[j];
	const double gb  = b.gradient[i] * b.gradient[j];
	return d.fa * a.hessian[k] + d.fb * b.hessian[k] + d.faa * ga + d.fab * gab + d.fbb * gb;
}

[[gnu::always_inline]] inline HyperDual chainHyper2(const HyperDual& a, const HyperDual& b, const Partials& d) {
	HyperDual out = constantHyperDual(d.f);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = d.fa * a.gradient[i] + d.fb * b.gradient[i];
	}
	for (int k = 0; k < 6; ++k) {
		out.hessian[k] = hessianEntry(a, b, d, k);
	}
	return out;
}

HyperDual operator+(const HyperDual& a, const HyperDual& b) {
	const Partials partials = {
		.f   = a.value + b.value,
		.fa  = 1,
		.fb  = 1,
		.faa = 0,
		.fab = 0,
		.fbb = 0,
	};

	return chainHyper2(a, b, partials);
}

HyperDual operator-(const HyperDual& a, const HyperDual& b) {
	const Partials partials = {
		.f   = a.value - b.value,
		.fa  = 1,
		.fb  = -1,
		.faa = 0,
		.fab = 0,
		.fbb = 0,
	};

	return chainHyper2(a, b, partials);
}

HyperDual operator*(const HyperDual& a, const HyperDual& b) {
	const Partials partials = {
		.f   = a.value * b.value,
		.fa  = b.value,
		.fb  = a.value,
		.faa = 0,
		.fab = 1,
		.fbb = 0,
	};

	return chainHyper2(a, b, partials);
}

HyperDual operator/(const HyperDual& a, const HyperDual& b) {
	const double v = b.value;

	const Partials d = {
		.f   = a.value / v,
		.fa  = 1 / v,
		.fb  = -a.value / (v * v),
		.faa = 0,
		.fab = -1 / (v * v),
		.fbb = 2 * a.value / (v * v * v),
	};

	return chainHyper2(a, b, d);
}

HyperDual operator-(const HyperDual& a) {
	const Derivatives derivatives = {
		.f  = -a.value,
		.d1 = -1,
		.d2 = 0,
	};

	return chainHyper(a, derivatives);
}

HyperDual sqrt(const HyperDual& a) {
	const double f = std::sqrt(a.value);
	if (f <= 0) {
		return constantHyperDual(f);
	}

	const Derivatives derivatives = {
		.f  = f,
		.d1 = 0.5 / f,
		.d2 = -0.25 / (a.value * f),
	};

	return chainHyper(a, derivatives);
}

HyperDual abs(const HyperDual& a) {
	return a.value < 0 ? -a : a;
}

HyperDual floor(const HyperDual& a) {
	return constantHyperDual(std::floor(a.value));
}

HyperDual sin(const HyperDual& a) {
	const Derivatives derivatives = {
		.f  = std::sin(a.value),
		.d1 = std::cos(a.value),
		.d2 = -std::sin(a.value),
	};

	return chainHyper(a, derivatives);
}

HyperDual cos(const HyperDual& a) {
	const Derivatives derivatives = {
		.f  = std::cos(a.value),
		.d1 = -std::sin(a.value),
		.d2 = -std::cos(a.value),
	};

	return chainHyper(a, derivatives);
}

HyperDual sign(const HyperDual& a) {
	return constantHyperDual(sign(a.value));
}

HyperDual atan2(const HyperDual& y, const HyperDual& x) {
	const double r  = x.value * x.value + y.value * y.value;
	const double xy = x.value * y.value;

	const Partials d = {
		.f   = std::atan2(y.value, x.value),
		.fa  = x.value / r,
		.fb  = -y.value / r,
		.faa = -2 * xy / (r * r),
		.fab = (y.value * y.value - x.value * x.value) / (r * r),
		.fbb = 2 * xy / (r * r),
	};

	return chainHyper2(y, x, d);
}

HyperDual pow(const HyperDual& a, const HyperDual& b) {
	const double v     = a.value;
	const double n     = b.value;
	const double f     = std::pow(v, n);
	const double l     = logOrZero(v);
	const double below = std::pow(v, n - 1);

	const Partials d = {
		.f   = f,
		.fa  = n * below,
		.fb  = f * l,
		.faa = n * (n - 1) * std::pow(v, n - 2),
		.fab = below * (1 + n * l),
		.fbb = f * l * l,
	};

	return chainHyper2(a, b, d);
}

HyperDual min(const HyperDual& a, const HyperDual& b) {
	return a.value <= b.value ? a : b;
}

HyperDual max(const HyperDual& a, const HyperDual& b) {
	return a.value >= b.value ? a : b;
}

HyperDual compare(Compare kind, const HyperDual& a, const HyperDual& b) {
	return constantHyperDual(compare(kind, a.value, b.value));
}

HyperDual choose(const HyperDual& condition, const HyperDual& a, const HyperDual& b) {
	return condition.value != 0 ? a : b;
}

HyperDual smoothMinimum(const HyperDual& a, const HyperDual& b, const HyperDual& r) {
	const double d  = a.value - b.value;
	const double fa = blendSlope(d, r.value);
	const double k  = bandCurvature(d, r.value);

	const Partials partials = {
		.f   = smoothMinimum(a.value, b.value, r.value),
		.fa  = fa,
		.fb  = 1 - fa,
		.faa = -k,
		.fab = k,
		.fbb = -k,
	};

	return chainHyper2(a, b, partials);
}

HyperDual positiveSquare(const HyperDual& a) {
	const Derivatives derivatives = {
		.f  = positiveSquare(a.value),
		.d1 = 2 * std::max(a.value, 0.0),
		.d2 = a.value > 0 ? 2.0 : 0.0,
	};

	return chainHyper(a, derivatives);
}

Interval everything() {
	return Interval {
		.lower = -infinity,
		.upper = infinity,
	};
}

bool isFinite(const Interval& a) {
	return std::isfinite(a.lower) && std::isfinite(a.upper);
}

bool isPointInterval(const Interval& a) {
	return a.lower == a.upper;
}

Interval boundedProduct(const Interval& a, const Interval& b) {
	if (isZero(a) || isZero(b)) {
		return constantAs<Interval>(0);
	}
	if (!isFinite(a) || !isFinite(b)) {
		return everything();
	}
	return a * b;
}

IntervalHyperDual constantIntervalHyperDual(const Interval& value) {
	const Interval zero = constantAs<Interval>(0);
	return IntervalHyperDual {
		.value    = value,
		.gradient = { zero, zero, zero },
		.hessian  = { zero, zero, zero, zero, zero, zero },
	};
}

IntervalHyperDual unboundedIntervalHyperDual(const Interval& value) {
	const Interval all = everything();
	return IntervalHyperDual {
		.value    = value,
		.gradient = { all, all, all },
		.hessian  = { all, all, all, all, all, all },
	};
}

template <>
IntervalHyperDual constantAs<IntervalHyperDual>(double value) {
	return constantIntervalHyperDual(constantAs<Interval>(value));
}

IntervalHyperDual coordinateOver(const Interval& span, int axis) {
	IntervalHyperDual dual = constantIntervalHyperDual(span);

	dual.gradient[axis] = constantAs<Interval>(1);
	return dual;
}

struct IntervalDerivatives {
	Interval f;
	Interval d1;
	Interval d2;
};

IntervalHyperDual chainIntervalHyper(const IntervalHyperDual& a, const IntervalDerivatives& d) {
	IntervalHyperDual out = constantIntervalHyperDual(d.f);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = boundedProduct(d.d1, a.gradient[i]);
	}
	for (int k = 0; k < 6; ++k) {
		const Interval outer = boundedProduct(a.gradient[hessianRow[k]], a.gradient[hessianColumn[k]]);
		out.hessian[k]       = boundedProduct(d.d1, a.hessian[k]) + boundedProduct(d.d2, outer);
	}
	return out;
}

struct IntervalPartials {
	Interval f;
	Interval fa;
	Interval fb;
	Interval faa;
	Interval fab;
	Interval fbb;
};

Interval intervalHessianEntry(const IntervalHyperDual& a, const IntervalHyperDual& b, const IntervalPartials& d, int k) {
	const int i         = hessianRow[k];
	const int j         = hessianColumn[k];
	const Interval ga   = boundedProduct(a.gradient[i], a.gradient[j]);
	const Interval gab  = boundedProduct(a.gradient[i], b.gradient[j]) + boundedProduct(b.gradient[i], a.gradient[j]);
	const Interval gb   = boundedProduct(b.gradient[i], b.gradient[j]);
	const Interval own  = boundedProduct(d.fa, a.hessian[k]) + boundedProduct(d.fb, b.hessian[k]);
	const Interval more = boundedProduct(d.faa, ga) + boundedProduct(d.fab, gab) + boundedProduct(d.fbb, gb);
	return own + more;
}

IntervalHyperDual chainIntervalHyper2(const IntervalHyperDual& a, const IntervalHyperDual& b, const IntervalPartials& d) {
	IntervalHyperDual out = constantIntervalHyperDual(d.f);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = boundedProduct(d.fa, a.gradient[i]) + boundedProduct(d.fb, b.gradient[i]);
	}
	for (int k = 0; k < 6; ++k) {
		out.hessian[k] = intervalHessianEntry(a, b, d, k);
	}
	return out;
}

IntervalHyperDual operator+(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	IntervalHyperDual out = constantIntervalHyperDual(a.value + b.value);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = a.gradient[i] + b.gradient[i];
	}
	for (int k = 0; k < 6; ++k) {
		out.hessian[k] = a.hessian[k] + b.hessian[k];
	}
	return out;
}

IntervalHyperDual operator-(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	IntervalHyperDual out = constantIntervalHyperDual(a.value - b.value);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = a.gradient[i] - b.gradient[i];
	}
	for (int k = 0; k < 6; ++k) {
		out.hessian[k] = a.hessian[k] - b.hessian[k];
	}
	return out;
}

bool isConstantIntervalHyperDual(const IntervalHyperDual& a) {
	return isPointInterval(a.value) && isZero(a.gradient[0]) && isZero(a.gradient[1]) && isZero(a.gradient[2]);
}

bool isFlatIntervalHyperDual(const IntervalHyperDual& a) {
	bool flat = true;
	for (const Interval& slope : a.gradient) {
		flat = flat && isZero(slope);
	}
	for (const Interval& curvature : a.hessian) {
		flat = flat && isZero(curvature);
	}
	return flat;
}

IntervalHyperDual scaledIntervalHyperDual(const IntervalHyperDual& a, const Interval& factor) {
	IntervalHyperDual out = constantIntervalHyperDual(boundedProduct(a.value, factor));

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = boundedProduct(a.gradient[i], factor);
	}
	for (int k = 0; k < 6; ++k) {
		out.hessian[k] = boundedProduct(a.hessian[k], factor);
	}
	return out;
}

Interval productHessian(const IntervalHyperDual& a, const IntervalHyperDual& b, int k) {
	const int i         = hessianRow[k];
	const int j         = hessianColumn[k];
	const Interval own  = boundedProduct(b.value, a.hessian[k]) + boundedProduct(a.value, b.hessian[k]);
	const Interval more = boundedProduct(a.gradient[i], b.gradient[j]) + boundedProduct(b.gradient[i], a.gradient[j]);
	return own + more;
}

IntervalHyperDual generalProduct(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	IntervalHyperDual out = constantIntervalHyperDual(boundedProduct(a.value, b.value));

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = boundedProduct(b.value, a.gradient[i]) + boundedProduct(a.value, b.gradient[i]);
	}
	for (int k = 0; k < 6; ++k) {
		out.hessian[k] = productHessian(a, b, k);
	}
	return out;
}

IntervalHyperDual operator*(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	if (isFlatIntervalHyperDual(b)) {
		return scaledIntervalHyperDual(a, b.value);
	}
	if (isFlatIntervalHyperDual(a)) {
		return scaledIntervalHyperDual(b, a.value);
	}
	return generalProduct(a, b);
}

IntervalHyperDual operator/(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	if (containsZero(b.value)) {
		return unboundedIntervalHyperDual(everything());
	}
	const Interval inverse = constantAs<Interval>(1) / b.value;
	const Interval square  = inverse * inverse;

	const IntervalPartials d = {
		.f   = boundedProduct(a.value, inverse),
		.fa  = inverse,
		.fb  = -boundedProduct(a.value, square),
		.faa = constantAs<Interval>(0),
		.fab = -square,
		.fbb = boundedProduct(constantAs<Interval>(2), boundedProduct(a.value, square * inverse)),
	};

	return chainIntervalHyper2(a, b, d);
}

IntervalHyperDual operator-(const IntervalHyperDual& a) {
	IntervalHyperDual out = constantIntervalHyperDual(-a.value);

	for (int i = 0; i < 3; ++i) {
		out.gradient[i] = -a.gradient[i];
	}
	for (int k = 0; k < 6; ++k) {
		out.hessian[k] = -a.hessian[k];
	}
	return out;
}

IntervalHyperDual sqrt(const IntervalHyperDual& a) {
	const Interval f = sqrt(a.value);
	if (a.value.lower <= 0) {
		return unboundedIntervalHyperDual(f);
	}

	const IntervalDerivatives derivatives = {
		.f  = f,
		.d1 = constantAs<Interval>(0.5) / f,
		.d2 = constantAs<Interval>(-0.25) / (a.value * f),
	};

	return chainIntervalHyper(a, derivatives);
}

IntervalHyperDual abs(const IntervalHyperDual& a) {
	if (a.value.lower >= 0) {
		return a;
	}
	if (a.value.upper <= 0) {
		return -a;
	}
	return unboundedIntervalHyperDual(abs(a.value));
}

IntervalHyperDual steppedIntervalHyperDual(const Interval& value) {
	if (isPointInterval(value)) {
		return constantIntervalHyperDual(value);
	}
	return unboundedIntervalHyperDual(value);
}

IntervalHyperDual floor(const IntervalHyperDual& a) {
	return steppedIntervalHyperDual(floor(a.value));
}

IntervalHyperDual sin(const IntervalHyperDual& a) {
	const IntervalDerivatives derivatives = {
		.f  = sin(a.value),
		.d1 = cos(a.value),
		.d2 = -sin(a.value),
	};

	return chainIntervalHyper(a, derivatives);
}

IntervalHyperDual cos(const IntervalHyperDual& a) {
	const IntervalDerivatives derivatives = {
		.f  = cos(a.value),
		.d1 = -sin(a.value),
		.d2 = -cos(a.value),
	};

	return chainIntervalHyper(a, derivatives);
}

IntervalHyperDual sign(const IntervalHyperDual& a) {
	return steppedIntervalHyperDual(sign(a.value));
}

IntervalHyperDual atan2(const IntervalHyperDual& y, const IntervalHyperDual& x) {
	const Interval f = atan2(y.value, x.value);
	const Interval r = x.value * x.value + y.value * y.value;
	if (f.upper - f.lower >= 2 * pi || containsZero(r)) {
		return unboundedIntervalHyperDual(f);
	}

	const Interval xy     = x.value * y.value;
	const Interval square = r * r;
	const Interval two    = constantAs<Interval>(2);

	const IntervalPartials d = {
		.f   = f,
		.fa  = x.value / r,
		.fb  = -y.value / r,
		.faa = -(two * xy) / square,
		.fab = (y.value * y.value - x.value * x.value) / square,
		.fbb = (two * xy) / square,
	};

	return chainIntervalHyper2(y, x, d);
}

IntervalHyperDual pow(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	const double n      = b.value.lower;
	const bool whole    = isWhole(n) && n >= 0;
	const bool positive = a.value.lower > 0;
	if (!isConstantIntervalHyperDual(b) || !(whole || positive)) {
		return unboundedIntervalHyperDual(pow(a.value, b.value));
	}

	const IntervalDerivatives derivatives = {
		.f  = pow(a.value, b.value),
		.d1 = boundedProduct(constantAs<Interval>(n), pow(a.value, constantAs<Interval>(n - 1))),
		.d2 = boundedProduct(constantAs<Interval>(n * (n - 1)), pow(a.value, constantAs<Interval>(n - 2))),
	};

	return chainIntervalHyper(a, derivatives);
}

IntervalHyperDual min(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	if (a.value.upper <= b.value.lower) {
		return a;
	}
	if (b.value.upper <= a.value.lower) {
		return b;
	}
	return unboundedIntervalHyperDual(min(a.value, b.value));
}

IntervalHyperDual max(const IntervalHyperDual& a, const IntervalHyperDual& b) {
	if (a.value.lower >= b.value.upper) {
		return a;
	}
	if (b.value.lower >= a.value.upper) {
		return b;
	}
	return unboundedIntervalHyperDual(max(a.value, b.value));
}

IntervalHyperDual compare(Compare kind, const IntervalHyperDual& a, const IntervalHyperDual& b) {
	return steppedIntervalHyperDual(compare(kind, a.value, b.value));
}

IntervalHyperDual choose(const IntervalHyperDual& condition, const IntervalHyperDual& a, const IntervalHyperDual& b) {
	if (excludesZero(condition.value)) {
		return a;
	}
	if (isZero(condition.value)) {
		return b;
	}
	return unboundedIntervalHyperDual(hull(a.value, b.value));
}

Interval bandCurvatures(const Interval& d, double r) {
	const double k = 1 / (2 * r);
	if (d.lower >= -r && d.upper <= r) {
		return constantAs<Interval>(k);
	}
	return Interval {
		.lower = 0,
		.upper = k,
	};
}

Interval slopesOver(const Interval& d, double r) {
	return Interval {
		.lower = blendSlope(d.upper, r),
		.upper = blendSlope(d.lower, r),
	};
}

IntervalHyperDual blendedIntervalHyperDual(const IntervalHyperDual& a, const IntervalHyperDual& b, double r) {
	const Interval d  = a.value - b.value;
	const Interval k  = bandCurvatures(d, r);
	const Interval fa = slopesOver(d, r);

	const IntervalPartials partials = {
		.f   = smoothMinimum(a.value, b.value, constantAs<Interval>(r)),
		.fa  = fa,
		.fb  = constantAs<Interval>(1) - fa,
		.faa = -k,
		.fab = k,
		.fbb = -k,
	};

	return chainIntervalHyper2(a, b, partials);
}

IntervalHyperDual smoothMinimum(const IntervalHyperDual& a, const IntervalHyperDual& b, const IntervalHyperDual& r) {
	const double radius = r.value.lower;
	const Interval d    = a.value - b.value;
	if (d.upper <= -radius) {
		return a;
	}
	if (d.lower >= radius) {
		return b;
	}
	if (!isFinite(d)) {
		return unboundedIntervalHyperDual(smoothMinimum(a.value, b.value, r.value));
	}
	return blendedIntervalHyperDual(a, b, radius);
}

Interval positiveSlopes(const Interval& a) {
	return Interval {
		.lower = 2 * std::max(a.lower, 0.0),
		.upper = 2 * std::max(a.upper, 0.0),
	};
}

Interval positiveCurvatures(const Interval& a) {
	return Interval {
		.lower = a.lower > 0 ? 2.0 : 0.0,
		.upper = a.upper > 0 ? 2.0 : 0.0,
	};
}

IntervalHyperDual positiveSquare(const IntervalHyperDual& a) {
	const IntervalDerivatives derivatives = {
		.f  = positiveSquare(a.value),
		.d1 = positiveSlopes(a.value),
		.d2 = positiveCurvatures(a.value),
	};

	return chainIntervalHyper(a, derivatives);
}

Batch batchOf(const Batch& a, double (*f)(double)) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = f(a.at[k]);
	}
	return out;
}

Batch pairedBatch(const Batch& a, const Batch& b, double (*f)(double, double)) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = f(a.at[k], b.at[k]);
	}
	return out;
}

Batch operator+(const Batch& a, const Batch& b) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = a.at[k] + b.at[k];
	}
	return out;
}

Batch operator-(const Batch& a, const Batch& b) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = a.at[k] - b.at[k];
	}
	return out;
}

Batch operator*(const Batch& a, const Batch& b) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = a.at[k] * b.at[k];
	}
	return out;
}

Batch operator/(const Batch& a, const Batch& b) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = a.at[k] / b.at[k];
	}
	return out;
}

Batch operator-(const Batch& a) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = -a.at[k];
	}
	return out;
}

Batch sqrt(const Batch& a) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = std::sqrt(a.at[k]);
	}
	return out;
}

Batch abs(const Batch& a) {
	return batchOf(a, abs);
}

Batch floor(const Batch& a) {
	return batchOf(a, floor);
}

Batch sin(const Batch& a) {
	return batchOf(a, sin);
}

Batch cos(const Batch& a) {
	return batchOf(a, cos);
}

Batch sign(const Batch& a) {
	return batchOf(a, sign);
}

Batch atan2(const Batch& y, const Batch& x) {
	return pairedBatch(y, x, atan2);
}

Batch pow(const Batch& a, const Batch& b) {
	return pairedBatch(a, b, pow);
}

Batch min(const Batch& a, const Batch& b) {
	return pairedBatch(a, b, min);
}

Batch max(const Batch& a, const Batch& b) {
	return pairedBatch(a, b, max);
}

Batch compare(Compare kind, const Batch& a, const Batch& b) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = compare(kind, a.at[k], b.at[k]);
	}
	return out;
}

Batch choose(const Batch& condition, const Batch& a, const Batch& b) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = choose(condition.at[k], a.at[k], b.at[k]);
	}
	return out;
}

Batch smoothMinimum(const Batch& a, const Batch& b, const Batch& r) {
	Batch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = smoothMinimum(a.at[k], b.at[k], r.at[k]);
	}
	return out;
}

Batch positiveSquare(const Batch& a) {
	return batchOf(a, positiveSquare);
}

DualBatch dualsOf(const DualBatch& a, Dual (*f)(const Dual&)) {
	DualBatch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = f(a.at[k]);
	}
	return out;
}

DualBatch pairedDuals(const DualBatch& a, const DualBatch& b, Dual (*f)(const Dual&, const Dual&)) {
	DualBatch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = f(a.at[k], b.at[k]);
	}
	return out;
}

DualBatch operator+(const DualBatch& a, const DualBatch& b) {
	return pairedDuals(a, b, [](const Dual& x, const Dual& y) {
		return x + y;
	});
}

DualBatch operator-(const DualBatch& a, const DualBatch& b) {
	return pairedDuals(a, b, [](const Dual& x, const Dual& y) {
		return x - y;
	});
}

DualBatch operator*(const DualBatch& a, const DualBatch& b) {
	return pairedDuals(a, b, [](const Dual& x, const Dual& y) {
		return x * y;
	});
}

DualBatch operator/(const DualBatch& a, const DualBatch& b) {
	return pairedDuals(a, b, [](const Dual& x, const Dual& y) {
		return x / y;
	});
}

DualBatch operator-(const DualBatch& a) {
	return dualsOf(a, [](const Dual& x) {
		return -x;
	});
}

DualBatch sqrt(const DualBatch& a) {
	return dualsOf(a, sqrt);
}

DualBatch abs(const DualBatch& a) {
	return dualsOf(a, abs);
}

DualBatch floor(const DualBatch& a) {
	return dualsOf(a, floor);
}

DualBatch sin(const DualBatch& a) {
	return dualsOf(a, sin);
}

DualBatch cos(const DualBatch& a) {
	return dualsOf(a, cos);
}

DualBatch sign(const DualBatch& a) {
	return dualsOf(a, sign);
}

DualBatch atan2(const DualBatch& y, const DualBatch& x) {
	return pairedDuals(y, x, atan2);
}

DualBatch pow(const DualBatch& a, const DualBatch& b) {
	return pairedDuals(a, b, pow);
}

DualBatch min(const DualBatch& a, const DualBatch& b) {
	return pairedDuals(a, b, min);
}

DualBatch max(const DualBatch& a, const DualBatch& b) {
	return pairedDuals(a, b, max);
}

DualBatch compare(Compare kind, const DualBatch& a, const DualBatch& b) {
	DualBatch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = compare(kind, a.at[k], b.at[k]);
	}
	return out;
}

DualBatch choose(const DualBatch& condition, const DualBatch& a, const DualBatch& b) {
	DualBatch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = choose(condition.at[k], a.at[k], b.at[k]);
	}
	return out;
}

DualBatch smoothMinimum(const DualBatch& a, const DualBatch& b, const DualBatch& r) {
	DualBatch out = {
		.at = {},
	};

	for (int k = 0; k < batchWidth; ++k) {
		out.at[k] = smoothMinimum(a.at[k], b.at[k], r.at[k]);
	}
	return out;
}

DualBatch positiveSquare(const DualBatch& a) {
	return dualsOf(a, positiveSquare);
}

}
