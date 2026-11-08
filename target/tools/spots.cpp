#include "spots.h"

#include <cmath>
#include <random>

namespace kodanuki {

double distanceAt(const Scene& scene, const Spot& p) {
	std::vector<double> registers;

	const Probe probe = {
		.p         = p,
		.registers = registers,
	};

	return sceneDistance(scene, probe);
}

Spot along(const Spot& from, const Spot& direction, double t) {
	return from + direction * t;
}

Spot unitToward(const Spot& from, const Spot& to) {
	return math::normalized(to - from);
}

std::vector<Spot> surfaceSpots(const SceneFile& file, int count, double height) {
	std::mt19937 random(5);
	std::uniform_real_distribution<double> across(-4, 4);
	std::uniform_real_distribution<double> rise(0, height);
	std::vector<Spot> spots;
	while (int(spots.size()) < count) {
		const Spot aim       = { across(random), across(random), rise(random) };
		const Spot direction = unitToward(file.shot.camera.eye, aim);
		double t             = 0;
		for (int i = 0; i < 400 && t < 40; ++i) {
			const double d = distanceAt(file.scene, along(file.shot.camera.eye, direction, t));
			t += d;
			if (d < 1e-5) {
				spots.push_back(along(file.shot.camera.eye, direction, t));
				break;
			}
		}
	}
	return spots;
}

math::Interval cellSpan(double coordinate, double size) {
	const double lower = std::floor(coordinate / size) * size;
	return math::Interval {
		.lower = lower,
		.upper = lower + size,
	};
}

Box cellAround(const Spot& p, double size) {
	return Box {
		.axes = { cellSpan(p.x, size), cellSpan(p.y, size), cellSpan(p.z, size) },
	};
}

}
