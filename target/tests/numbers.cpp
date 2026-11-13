#include "sdf/numbers.h"
#include "check.h"
#include "plugins/transport/paths/paths.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"

#include <array>
#include <random>
#include <vector>

namespace kodanuki {

using Point = std::array<double, 3>;

const Program& mixedProgram(const Scene& scene) {
	return scene.programs.at(0);
}

template <typename T>
Inputs<T> inputsAt(const Scene& scene, const std::array<T, 3>& p) {
	return Inputs<T> {
		.arguments = scene.arguments.data(),
		.p         = { p[0], p[1], p[2] },
		.n         = { math::constantAs<T>(0), math::constantAs<T>(0), math::constantAs<T>(0) },
	};
}

double valueAt(const Scene& scene, const Point& p) {
	std::vector<double> registers;
	return evaluate(mixedProgram(scene), inputsAt<double>(scene, p), registers);
}

Point shifted(const Point& p, int axis, double by) {
	Point q = p;
	q[axis] += by;
	return q;
}

math::Dual dualAt(const Scene& scene, const Point& p) {
	std::vector<math::Dual> registers;
	const std::array<math::Dual, 3> d = { math::coordinateAs<math::Dual>(p[0], 0), math::coordinateAs<math::Dual>(p[1], 1), math::coordinateAs<math::Dual>(p[2], 2) };
	return evaluate(mixedProgram(scene), inputsAt<math::Dual>(scene, d), registers);
}

math::HyperDual hyperAt(const Scene& scene, const Point& p) {
	std::vector<math::HyperDual> registers;
	const std::array<math::HyperDual, 3> d = { math::coordinateAs<math::HyperDual>(p[0], 0), math::coordinateAs<math::HyperDual>(p[1], 1), math::coordinateAs<math::HyperDual>(p[2], 2) };
	return evaluate(mixedProgram(scene), inputsAt<math::HyperDual>(scene, d), registers);
}

void checkGradients(const Scene& scene, const Point& p) {
	const double h              = 1e-5;
	const math::Dual dual       = dualAt(scene, p);
	const math::HyperDual hyper = hyperAt(scene, p);
	for (int i = 0; i < 3; ++i) {
		const double difference = (valueAt(scene, shifted(p, i, h)) - valueAt(scene, shifted(p, i, -h))) / (2 * h);

		const Near dualSlope = {
			.got       = dual.gradient[i],
			.want      = difference,
			.tolerance = 1e-6,
			.what      = "dual gradient against finite differences",
		};

		checkNear(dualSlope);

		const Near hyperSlope = {
			.got       = hyper.gradient[i],
			.want      = dual.gradient[i],
			.tolerance = 1e-12,
			.what      = "hyperdual gradient equals dual",
		};

		checkNear(hyperSlope);
	}
	const int rows[6]    = { 0, 0, 0, 1, 1, 2 };
	const int columns[6] = { 0, 1, 2, 1, 2, 2 };
	for (int k = 0; k < 6; ++k) {
		const double up   = dualAt(scene, shifted(p, columns[k], h)).gradient[rows[k]];
		const double down = dualAt(scene, shifted(p, columns[k], -h)).gradient[rows[k]];

		const Near hyperCurvature = {
			.got       = hyper.hessian[k],
			.want      = (up - down) / (2 * h),
			.tolerance = 1e-5,
			.what      = "hyperdual Hessian against differences of duals",
		};

		checkNear(hyperCurvature);
	}
}

math::Interval intervalOver(const Scene& scene, const Point& low, double size) {
	std::vector<math::Interval> registers;
	std::array<math::Interval, 3> box;
	for (int i = 0; i < 3; ++i) {
		box[i] = math::Interval {
			.lower = low[i],
			.upper = low[i] + size,
		};
	}
	return evaluate(mixedProgram(scene), inputsAt<math::Interval>(scene, box), registers);
}

void checkInterval(const Scene& scene, const Point& low, std::mt19937& random) {
	const double size          = 0.3;
	const math::Interval bound = intervalOver(scene, low, size);
	std::uniform_real_distribution<double> unit(0, 1);
	for (int s = 0; s < 50; ++s) {
		const Point p  = { low[0] + size * unit(random), low[1] + size * unit(random), low[2] + size * unit(random) };
		const double v = valueAt(scene, p);
		check(v >= bound.lower - 1e-12 && v <= bound.upper + 1e-12, "interval contains the point values");
	}
}

void checkNumbers() {
	const Scene scene = sceneOrExit(writeScene("numbers", R"(
		function mixed(k) {
			let q = abs(p) - [0.5, 0.4, 0.3];
			let a = length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
			let b = sin(p.x * 3) * cos(p.y * 2) + atan2(p.y, p.x + 3) + pow(p.z * p.z + 1, 1.5) / (2 + p.x);
			let c = select(p.z > 0.1, sqrt(dot(p, p) + 1), mix(p.y, p.x, 0.25));
			return min(a * k, b) + c - floor(p.x + 10);
		}

		mixed(2);
	)"))
							.scene;
	std::mt19937 random(7);
	std::uniform_real_distribution<double> range(-1, 1);
	for (int s = 0; s < 40; ++s) {
		const Point p = { range(random), range(random), range(random) };
		checkGradients(scene, p);
		checkInterval(scene, p, random);
	}
}

}
