#include "plugins/feature/clouds/clouds.h"

#include "core/plugin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>

namespace kodanuki {

const bool cloudTableRead = pluginBuilt("feature/clouds");

constexpr double cloudPlanet      = 6.36e6;
constexpr std::size_t cloudRows   = 160;
constexpr std::size_t cloudTurns  = 640;
constexpr int exactSteps          = 72;
constexpr int tableSteps          = 28;
constexpr int exactOctaves        = 7;
constexpr int tableOctaves        = 4;
constexpr int lightSteps          = 5;
constexpr int lightOctaves        = 3;
constexpr int overcastSteps       = 12;
constexpr double noiseUnit        = 1.0 / 4294967296.0;
constexpr double cloudEdgeWidth   = 0.10;
constexpr double cloudFloorEasing = 0.07;

struct CloudMarch {
	int steps;
	int octaves;
};

struct CloudSpan {
	double enter;
	double leave;
};

std::uint32_t latticeHash(std::int64_t x, std::int64_t y, std::int64_t z) {
	std::uint64_t key = std::uint64_t(x) * 0x9e3779b97f4a7c15ULL ^ std::uint64_t(y) * 0xc2b2ae3d27d4eb4fULL ^ std::uint64_t(z) * 0x165667b19e3779f9ULL;
	key ^= key >> 31;
	key *= 0xbf58476d1ce4e5b9ULL;
	key ^= key >> 29;
	return std::uint32_t(key >> 32);
}

double latticeCorner(std::int64_t x, std::int64_t y, std::int64_t z) {
	return double(latticeHash(x, y, z)) * noiseUnit;
}

double quinticEase(double t) {
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double straightMix(double a, double b, double t) {
	return a + (b - a) * t;
}

double valueNoise(const math::Vector& at) {
	const double fx      = std::floor(at.x);
	const double fy      = std::floor(at.y);
	const double fz      = std::floor(at.z);
	const std::int64_t x = std::int64_t(fx);
	const std::int64_t y = std::int64_t(fy);
	const std::int64_t z = std::int64_t(fz);
	const double u       = quinticEase(at.x - fx);
	const double v       = quinticEase(at.y - fy);
	const double w       = quinticEase(at.z - fz);
	const double near    = straightMix(straightMix(latticeCorner(x, y, z), latticeCorner(x + 1, y, z), u), straightMix(latticeCorner(x, y + 1, z), latticeCorner(x + 1, y + 1, z), u), v);
	const double far     = straightMix(straightMix(latticeCorner(x, y, z + 1), latticeCorner(x + 1, y, z + 1), u), straightMix(latticeCorner(x, y + 1, z + 1), latticeCorner(x + 1, y + 1, z + 1), u), v);
	return straightMix(near, far, w);
}

double easedShare(double from, double to, double value) {
	const double share = std::clamp((value - from) / (to - from), 0.0, 1.0);
	return share * share * (3.0 - 2.0 * share);
}

double cloudHeight(const Cloudscape& scape, const math::Vector& at) {
	return math::length(at + scape.up * cloudPlanet) - cloudPlanet;
}

math::Vector finerSpot(const math::Vector& spot) {
	const math::Vector scaled = spot * 2.03;
	return { scaled.x + 17.1, scaled.y - 5.3, scaled.z + 9.7 };
}

double cloudShell(const Cloudscape& scape, const math::Vector& from, const math::Vector& direction, double radius) {
	const math::Vector centred = from + scape.up * cloudPlanet;
	const double along         = math::dot(centred, direction);
	const double inside        = math::dot(centred, centred) - radius * radius;
	const double under         = along * along - inside;
	if (under < 0.0) {
		return -1.0;
	}
	return -along + std::sqrt(under);
}

double cloudPhase(double cosine, double lean) {
	const double squared = lean * lean;
	return (1.0 - squared) / (4.0 * math::pi * std::pow(1.0 + squared - 2.0 * lean * cosine, 1.5));
}

double cloudBeam(double cosine, double depth) {
	double total    = 0.0;
	double weight   = 1.0;
	double lean     = 1.0;
	double thinning = 1.0;
	for (int octave = 0; octave < 4; ++octave) {
		const double phase = 0.7 * cloudPhase(cosine, 0.8 * lean) + 0.3 * cloudPhase(cosine, -0.2 * lean);
		total += weight * phase * std::exp(-depth * thinning);
		weight *= 0.7;
		lean *= 0.5;
		thinning *= 0.3;
	}
	return total;
}

bool cloudsVisible(const Clouds& clouds) {
	return clouds.cover > 0 && clouds.density > 0;
}

double cloudThickness(const Cloudscape& scape, const math::Vector& at, int octaves) {
	const Clouds& clouds = scape.clouds;
	const double through = (cloudHeight(scape, at) - clouds.height) / clouds.thickness;
	if (through <= 0.0 || through >= 1.0) {
		return 0.0;
	}
	const double edge = 1.0 - clouds.cover + 0.3 * through * through;
	const double full = 1.0 - std::ldexp(1.0, -octaves);
	math::Vector spot = at * (1.0 / clouds.span);
	double total      = 0.0;
	double weight     = 0.5;
	double weights    = 0.0;
	for (int octave = 0; octave < octaves; ++octave) {
		total += weight * valueNoise(spot);
		weights += weight;
		weight *= 0.5;
		spot = finerSpot(spot);
		if (octave + 1 == octaves) {
			break;
		}
		if ((total + (full - weights)) * (1.0 + 0x1p-40) / full <= edge) {
			return 0.0;
		}
		if (((total - 1e-12) / full - edge) / cloudEdgeWidth >= 1.0) {
			return clouds.density * easedShare(0.0, cloudFloorEasing, through);
		}
	}
	const double filled = std::clamp((total / weights - edge) / cloudEdgeWidth, 0.0, 1.0);
	return clouds.density * filled * easedShare(0.0, cloudFloorEasing, through);
}

double depthTowardLight(const Cloudscape& scape, const math::Vector& at) {
	const double reach = 0.8 * scape.clouds.thickness;
	double depth       = 0.0;
	double walked      = 0.0;
	for (int step = 1; step <= lightSteps; ++step) {
		const double length      = reach * double(step) / (0.5 * lightSteps * (lightSteps + 1));
		const math::Vector there = at + scape.toward * (walked + 0.5 * length);
		depth += cloudThickness(scape, there, lightOctaves) * length;
		walked += length;
	}
	return depth;
}

double directionJitter(const math::Vector& direction) {
	std::uint64_t bits[3] = { 0, 0, 0 };
	std::memcpy(&bits[0], &direction.x, sizeof(double));
	std::memcpy(&bits[1], &direction.y, sizeof(double));
	std::memcpy(&bits[2], &direction.z, sizeof(double));
	return double(latticeHash(std::int64_t(bits[0]), std::int64_t(bits[1]), std::int64_t(bits[2]))) * noiseUnit;
}

Veil clearVeil() {
	return Veil {
		.shown   = { 0, 0, 0 },
		.through = 1.0,
	};
}

CloudSpan layerSpan(const Cloudscape& scape, const math::Vector& from, const math::Vector& direction) {
	return CloudSpan {
		.enter = cloudShell(scape, from, direction, cloudPlanet + scape.clouds.height),
		.leave = cloudShell(scape, from, direction, cloudPlanet + scape.clouds.height + scape.clouds.thickness),
	};
}

Colour sentAt(const Cloudscape& scape, const math::Vector& at, double cosine) {
	const double rise = (cloudHeight(scape, at) - scape.clouds.height) / scape.clouds.thickness;
	const double lit  = cloudBeam(cosine, depthTowardLight(scape, at));
	return scape.light * lit + scape.ambient * (0.25 + 0.5 * rise);
}

Veil marchedVeil(const Cloudscape& scape, const math::Vector& direction, const CloudMarch& march) {
	const math::Vector origin = { 0, 0, 0 };
	const CloudSpan span      = layerSpan(scape, origin, direction);
	const double far          = 20.0 * (scape.clouds.height + scape.clouds.thickness);
	if (math::dot(direction, scape.up) <= 0.0 || !cloudsVisible(scape.clouds) || span.enter < 0.0 || span.leave <= span.enter || span.enter > 4.0 * far) {
		return clearVeil();
	}
	const double step   = (span.leave - span.enter) / march.steps;
	const double cosine = math::dot(direction, scape.toward);
	double through      = 1.0;
	Colour shown        = { 0, 0, 0 };
	double walked       = span.enter + step * directionJitter(direction);
	for (int taken = 0; taken < march.steps && through > 0.005; ++taken, walked += step) {
		const math::Vector at = direction * walked;
		const double density  = cloudThickness(scape, at, march.octaves);
		if (density <= 0.0) {
			continue;
		}
		const double kept = std::exp(-density * step);
		shown             = shown + sentAt(scape, at, cosine) * (through * (1.0 - kept));
		through *= kept;
	}
	const double faded = std::exp(-span.enter / far);
	return Veil {
		.shown   = shown * faded,
		.through = 1.0 - (1.0 - through) * faded,
	};
}

math::Vector tableDirection(const Cloudscape& scape, std::size_t row, std::size_t column) {
	const double along = double(row) / double(cloudRows - 1);
	const double rise  = std::sin(0.5 * math::pi * along * along);
	const double level = std::sqrt(std::max(1.0 - rise * rise, 0.0));
	const double turn  = 2.0 * math::pi * double(column) / double(cloudTurns);
	return scape.level * (level * std::cos(turn)) + scape.aside * (level * std::sin(turn)) + scape.up * rise;
}

Veil tableVeil(const Cloudscape& scape, const math::Vector& direction) {
	const double rise = math::dot(direction, scape.up);
	if (rise <= 0.0) {
		return clearVeil();
	}
	const double row       = std::sqrt(std::asin(std::min(rise, 1.0)) / (0.5 * math::pi)) * double(cloudRows - 1);
	const double angle     = std::atan2(math::dot(direction, scape.aside), math::dot(direction, scape.level));
	const double turn      = angle < 0.0 ? angle + 2.0 * math::pi : angle;
	const double column    = turn / (2.0 * math::pi) * double(cloudTurns);
	const std::size_t low  = std::min(std::size_t(row), cloudRows - 2);
	const double upShare   = std::clamp(row - double(low), 0.0, 1.0);
	const std::size_t left = std::size_t(column) % cloudTurns;
	const double onRight   = column - std::floor(column);

	Veil read = {
		.shown   = { 0, 0, 0 },
		.through = 0.0,
	};

	for (int dr = 0; dr < 2; ++dr) {
		for (int dc = 0; dc < 2; ++dc) {
			const double weight     = (dr != 0 ? upShare : 1.0 - upShare) * (dc != 0 ? onRight : 1.0 - onRight);
			const std::size_t index = (low + std::size_t(dr)) * cloudTurns + (dc != 0 ? (left + 1) % cloudTurns : left);
			read.shown              = read.shown + scape.shown[index] * weight;
			read.through += scape.through[index] * weight;
		}
	}
	return read;
}

void laidOutRows(Cloudscape& scape, std::size_t first, std::size_t stride) {
	const CloudMarch march = {
		.steps   = tableSteps,
		.octaves = tableOctaves,
	};

	for (std::size_t row = first; row < cloudRows; row += stride) {
		for (std::size_t column = 0; column < cloudTurns; ++column) {
			const Veil veil                          = marchedVeil(scape, tableDirection(scape, row, column), march);
			scape.shown[row * cloudTurns + column]   = veil.shown;
			scape.through[row * cloudTurns + column] = veil.through;
		}
	}
}

void laidOut(Cloudscape& scape, int threads) {
	const std::size_t workers = std::size_t(std::max(1, threads));
	std::vector<std::thread> crew;
	for (std::size_t worker = 1; worker < workers; ++worker) {
		crew.emplace_back([&scape, worker, workers]() {
			laidOutRows(scape, worker, workers);
		});
	}
	laidOutRows(scape, 0, workers);
	for (std::thread& member : crew) {
		member.join();
	}
}

Cloudscape cloudscapeOf(const CloudLight& light) {
	const math::Vector up   = { 0, 0, 1 };
	const math::Basis basis = math::basisAround(up);

	Cloudscape scape = {
		.present = light.lighting.clouds.present,
		.clouds  = light.lighting.clouds,
		.up      = up,
		.level   = basis.tangent,
		.aside   = basis.bitangent,
		.toward  = math::normalized(light.toward),
		.light   = light.light,
		.ambient = light.ambient,
		.shown   = {},
		.through = {},
		.thinned = {},
	};

	for (const Light& lamp : light.lighting.lights) {
		scape.thinned.push_back(lamp.kind == LightKind::Sun || lamp.kind == LightKind::Moon);
	}
	if (!scape.present) {
		return scape;
	}
	scape.shown.assign(cloudRows * cloudTurns, Colour { 0, 0, 0 });
	scape.through.assign(cloudRows * cloudTurns, 1.0);
	laidOut(scape, light.threads);
	return scape;
}

Veil veiled(const Cloudscape& scape, const math::Vector& direction, bool exact) {
	if (!exact && cloudTableRead) {
		return tableVeil(scape, direction);
	}

	const CloudMarch march = {
		.steps   = exactSteps,
		.octaves = exactOctaves,
	};

	return marchedVeil(scape, direction, march);
}

double overcast(const Cloudscape& scape, const math::Vector& from, const math::Vector& toward) {
	const CloudSpan span = layerSpan(scape, from, toward);
	if (math::dot(toward, scape.up) <= 0.0 || !cloudsVisible(scape.clouds) || span.enter < 0.0 || span.leave <= span.enter) {
		return 1.0;
	}
	const double step = (span.leave - span.enter) / overcastSteps;
	double depth      = 0.0;
	for (int taken = 0; taken < overcastSteps; ++taken) {
		const math::Vector at = from + toward * (span.enter + (taken + 0.5) * step);
		depth += cloudThickness(scape, at, tableOctaves) * step;
	}
	return std::exp(-depth);
}

}
