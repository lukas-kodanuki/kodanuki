#include "plugins/feature/sky_layers/sky_layers.h"

#include "math/random.h"

#include <algorithm>
#include <cmath>

namespace kodanuki {

constexpr double skyDegree = math::pi / 180.0;

struct CubeFace {
	std::uint64_t index;
	math::Vector out;
	math::Vector right;
	math::Vector up;
};

struct StarCell {
	int column;
	int row;
	int cells;
	std::uint64_t key;
};

struct StarDraws {
	double across;
	double down;
	double brightness;
	double warmth;
};

double smoothShare(double from, double to, double value) {
	const double share = std::clamp((value - from) / (to - from), 0.0, 1.0);
	return share * share * (3.0 - 2.0 * share);
}

double sunElevation(const SkyLayers& layers) {
	return std::asin(std::clamp(math::dot(layers.up, layers.sun), -1.0, 1.0));
}

bool layersHaveSun(const SkyLayers& layers) {
	return math::length(layers.sun) > 0;
}

double dayShare(const SkyLayers& layers) {
	return smoothShare(2.0 * skyDegree, 12.0 * skyDegree, sunElevation(layers));
}

double nightShare(const SkyLayers& layers) {
	return 1.0 - smoothShare(-14.0 * skyDegree, -4.0 * skyDegree, sunElevation(layers));
}

double duskShare(const SkyLayers& layers) {
	return std::max(1.0 - dayShare(layers) - nightShare(layers), 0.0);
}

Colour stopColour(const SkyStop& stop) {
	return stop.colour;
}

Colour alongRun(const std::vector<SkyStop>& stops, double height) {
	if (height <= double(stops.front().height)) {
		return stopColour(stops.front());
	}
	for (std::size_t stop = 1; stop < stops.size(); ++stop) {
		const SkyStop& upper = stops[stop];
		if (height <= double(upper.height)) {
			const SkyStop& lower = stops[stop - 1];
			const double share   = (height - double(lower.height)) / std::max(double(upper.height) - double(lower.height), 1e-12);
			return stopColour(lower) * (1.0 - share) + stopColour(upper) * share;
		}
	}
	return stopColour(stops.back());
}

double towardSun(const SkyLayers& layers, const math::Vector& direction, double height) {
	const math::Vector level   = direction - layers.up * height;
	const math::Vector sunward = layers.sun - layers.up * math::dot(layers.up, layers.sun);
	const double apart         = math::length(level) * math::length(sunward);
	return apart > 1e-9 ? 0.5 + 0.5 * math::dot(level, sunward) / apart : 0.5;
}

Colour paintedColour(const SkyLayers& layers, const math::Vector& direction) {
	const double height = std::clamp(math::dot(layers.up, direction), -1.0, 1.0);
	if (!layersHaveSun(layers)) {
		return alongRun(layers.runs.day, height);
	}
	const double day     = dayShare(layers);
	const double night   = nightShare(layers);
	const double dusk    = std::max(1.0 - day - night, 0.0);
	const double towards = towardSun(layers, direction, height);
	const double away    = dusk * (1.0 - towards) * 0.5;
	return alongRun(layers.runs.day, height) * (day + away) + alongRun(layers.runs.night, height) * (night + away) + alongRun(layers.runs.dusk, height) * (dusk * towards);
}

SpectrumTable tintTable(const Colour& colour, double fade) {
	SpectrumTable table = {
		.values = {},
	};

	for (int place = 0; place < tablePlaces; ++place) {
		const double along  = std::min(double(place) / double(tablePlaces - 1), 1.0 - 1e-9);
		table.values[place] = fade * spectrumOf(colour, spreadWavelengths(along)).values[0];
	}
	return table;
}

Colour reddened(const Lighting& lighting, const SkyLayers& layers) {
	const Colour white   = { 1, 1, 1 };
	const Colour horizon = alongRun(lighting.sky.painted.dusk, 0.0);
	const double most    = std::max({ horizon.red, horizon.green, horizon.blue, 1e-9 });
	const double share   = duskShare(layers);
	return white * (1.0 - share) + horizon * (share / most);
}

LayeredLamp layeredLamp(const Lighting& lighting, const SkyLayers& layers, std::uint32_t light) {
	const Light& lamp            = lighting.lights[light];
	const math::Vector direction = math::normalized(lamp.direction);
	const bool moon              = lamp.kind == LightKind::Moon;
	const bool sun               = light == firstSun(lighting);
	const bool tinted            = lighting.sky.kind == SkyKind::Painted && (moon || sun);
	const double fade            = smoothShare(-0.005, 0.005, math::dot(layers.up, direction));
	const Colour white           = { 1, 1, 1 };
	const Colour colour          = sun && layersHaveSun(layers) ? reddened(lighting, layers) : white;
	return LayeredLamp {
		.moon      = moon,
		.direction = direction,
		.radius    = std::sin(0.5 * double(lamp.angle)),
		.tinted    = tinted,
		.tint      = tintTable(colour, tinted ? fade : 1),
	};
}

SkyLayers skyLayersOf(const Lighting& lighting) {
	const std::uint32_t sun = firstSun(lighting);
	const Colour glow       = lighting.sky.glow * double(lighting.sky.strength);

	SkyLayers layers = {
		.up      = { 0, 0, 1 },
		.sun     = sun < lighting.lights.size() ? math::normalized(lighting.lights[sun].direction) : math::Vector { 0, 0, 0 },
		.painted = lighting.sky.kind == SkyKind::Painted,
		.runs    = lighting.sky.painted,
		.glow    = glow,
		.spread  = double(lighting.sky.spread),
		.stars   = lighting.stars,
		.lamps   = {},
	};

	for (std::uint32_t light = 0; light < lighting.lights.size(); ++light) {
		layers.lamps.push_back(layeredLamp(lighting, layers, light));
	}
	return layers;
}

bool skyGlows(const SkyLayers& layers) {
	const bool lit = layers.glow.red > 0 || layers.glow.green > 0 || layers.glow.blue > 0;
	return lit && layersHaveSun(layers);
}

bool skyHasMoon(const SkyLayers& layers) {
	for (const LayeredLamp& lamp : layers.lamps) {
		if (lamp.moon) {
			return true;
		}
	}
	return false;
}

Spectrum4 glowSeen(const SkyLayers& layers, const math::Vector& direction, const Wavelengths& wavelengths) {
	const double above   = smoothShare(-0.02, 0.02, math::dot(layers.up, direction));
	const double apart   = std::acos(std::clamp(math::dot(direction, layers.sun), -1.0, 1.0));
	const double fade    = smoothShare(-8.0 * skyDegree, 0.0, sunElevation(layers));
	const double falling = std::exp(-apart / std::max(layers.spread, 1e-6)) * fade * above;
	return spectrumOf(layers.glow * falling, wavelengths);
}

Spectrum4 paintedSeen(const SkyLayers& layers, const math::Vector& direction, const Wavelengths& wavelengths) {
	return spectrumOf(paintedColour(layers, direction), wavelengths);
}

CubeFace cubeFaceOf(const math::Vector& direction) {
	const double x = std::abs(direction.x);
	const double y = std::abs(direction.y);
	const double z = std::abs(direction.z);
	if (x >= y && x >= z) {
		const double sign = direction.x >= 0 ? 1.0 : -1.0;
		return CubeFace {
			.index = direction.x >= 0 ? 0u : 1u,
			.out   = { sign, 0, 0 },
			.right = { 0, sign, 0 },
			.up    = { 0, 0, 1 },
		};
	}
	if (y >= z) {
		const double sign = direction.y >= 0 ? 1.0 : -1.0;
		return CubeFace {
			.index = direction.y >= 0 ? 2u : 3u,
			.out   = { 0, sign, 0 },
			.right = { 0, 0, sign },
			.up    = { 1, 0, 0 },
		};
	}
	const double sign = direction.z >= 0 ? 1.0 : -1.0;
	return CubeFace {
		.index = direction.z >= 0 ? 4u : 5u,
		.out   = { 0, 0, sign },
		.right = { sign, 0, 0 },
		.up    = { 0, 1, 0 },
	};
}

std::uint64_t nextStarKey(std::uint64_t key) {
	return math::mixedBits(key + 0x9e3779b97f4a7c15ULL);
}

StarDraws starDrawsOf(std::uint64_t key) {
	const std::uint64_t first  = nextStarKey(key);
	const std::uint64_t second = nextStarKey(first);
	const std::uint64_t third  = nextStarKey(second);
	const std::uint64_t fourth = nextStarKey(third);
	return StarDraws {
		.across     = math::unitOf(first),
		.down       = math::unitOf(second),
		.brightness = math::unitOf(third),
		.warmth     = math::unitOf(fourth),
	};
}

StarCell starCellOf(const Stars& stars, const CubeFace& face, const math::Vector& direction) {
	const double outward  = math::dot(direction, face.out);
	const double u        = math::dot(direction, face.right) / outward;
	const double v        = math::dot(direction, face.up) / outward;
	const int cells       = std::max(1, int(std::ceil(std::sqrt(double(stars.count) / 6.0))));
	const int column      = std::clamp(int((u * 0.5 + 0.5) * cells), 0, cells - 1);
	const int row         = std::clamp(int((v * 0.5 + 0.5) * cells), 0, cells - 1);
	const std::uint64_t k = (face.index * 65536 + std::uint64_t(row)) * 65536 + std::uint64_t(column);
	return StarCell {
		.column = column,
		.row    = row,
		.cells  = cells,
		.key    = k,
	};
}

Colour starlight(const Stars& stars, const math::Vector& direction) {
	const CubeFace face     = cubeFaceOf(direction);
	const StarCell cell     = starCellOf(stars, face, direction);
	const StarDraws draws   = starDrawsOf(cell.key);
	const double across     = (cell.column + 0.2 + 0.6 * draws.across) / cell.cells * 2.0 - 1.0;
	const double down       = (cell.row + 0.2 + 0.6 * draws.down) / cell.cells * 2.0 - 1.0;
	const math::Vector star = math::normalized(face.out + face.right * across + face.up * down);
	if (math::dot(direction, star) < std::cos(0.5 * double(stars.angle))) {
		return Colour { 0, 0, 0 };
	}
	const double brightness = double(stars.strength) * std::pow(draws.brightness, 6.0);
	const Colour cool       = { 0.75, 0.85, 1.0 };
	const Colour warm       = { 1.0, 0.82, 0.62 };
	return (cool * (1.0 - draws.warmth) + warm * draws.warmth) * brightness;
}

double starNight(const SkyLayers& layers) {
	return layersHaveSun(layers) ? 1.0 - smoothShare(-12.0 * skyDegree, -2.0 * skyDegree, sunElevation(layers)) : 1.0;
}

Spectrum4 starsSeen(const SkyLayers& layers, const math::Vector& direction, const Wavelengths& wavelengths) {
	if (!(math::dot(layers.up, direction) > 0)) {
		return flatSpectrum(0);
	}
	return spectrumOf(starlight(layers.stars, direction) * starNight(layers), wavelengths);
}

double moonLit(const SkyLayers& layers, std::uint32_t light, const math::Vector& direction) {
	if (light >= layers.lamps.size() || !layers.lamps[light].moon || !layersHaveSun(layers)) {
		return 1;
	}
	const LayeredLamp& moon   = layers.lamps[light];
	const double along        = math::dot(direction, moon.direction);
	const double aside        = std::max(1.0 - along * along, 0.0);
	const double reach        = along - std::sqrt(std::max(moon.radius * moon.radius - aside, 0.0));
	const math::Vector normal = (direction * reach - moon.direction) / moon.radius;
	const double lit          = std::clamp(math::dot(normal, layers.sun) / 0.1 + 0.5, 0.0, 1.0);
	return lit * lit * (3.0 - 2.0 * lit);
}

Spectrum4 settingTint(const SkyLayers& layers, std::uint32_t light, const Wavelengths& wavelengths) {
	if (light >= layers.lamps.size() || !layers.lamps[light].tinted) {
		return flatSpectrum(1);
	}
	return spectrumTableAt(layers.lamps[light].tint, wavelengths);
}

}
