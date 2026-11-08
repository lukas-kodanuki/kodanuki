#include "extras/feature_correct_atmosphere/atmosphere.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kodanuki {

constexpr double planetRadius     = 6360.0;
constexpr double airTop           = 6420.0;
constexpr double eyeHeight        = 0.001;
constexpr double airScaleHeight   = 8.0;
constexpr double hazeScaleHeight  = 1.2;
constexpr double hazeLean         = 0.76;
constexpr double hazeAlbedo       = 0.9;
constexpr std::size_t airHeights  = 48;
constexpr std::size_t airBearings = 32;
constexpr std::size_t airColours  = tablePlaces;
constexpr int viewSteps           = 40;
constexpr int sunSteps            = 12;
constexpr int landingRings        = 32;
constexpr int landingSpokes       = 64;

struct AirDepth {
	double air;
	double haze;
};

struct AirPlace {
	double row;
	double column;
	double height;
	double cosine;
};

struct ViewMarch {
	math::Vector view;
	math::Vector sun;
	double haze;
};

double tableWavelength(std::size_t colour) {
	return shortestWavelength + (longestWavelength - shortestWavelength) * double(colour) / double(airColours - 1);
}

double airScattering(double nanometres) {
	const double ratio = 440.0 / nanometres;
	return 33.1e-3 * ratio * ratio * ratio * ratio;
}

double hazeScattering(double nanometres, double haze) {
	return 0.004 * haze * haze * std::pow(550.0 / nanometres, 1.3);
}

double airPhase(double cosine) {
	return 3.0 / (16.0 * math::pi) * (1.0 + cosine * cosine);
}

double hazePhase(double cosine) {
	const double squared     = hazeLean * hazeLean;
	const double denominator = std::pow(1.0 + squared - 2.0 * hazeLean * cosine, 1.5);
	return 3.0 / (8.0 * math::pi) * (1.0 - squared) * (1.0 + cosine * cosine) / ((2.0 + squared) * denominator);
}

double shellLeaving(const math::Vector& from, const math::Vector& direction, double radius) {
	const double along  = math::dot(from, direction);
	const double inside = math::dot(from, from) - radius * radius;
	return -along + std::sqrt(std::max(along * along - inside, 0.0));
}

bool groundAhead(const math::Vector& from, const math::Vector& direction) {
	const double along  = math::dot(from, direction);
	const double inside = math::dot(from, from) - planetRadius * planetRadius;
	const double under  = along * along - inside;
	return under >= 0.0 && -along - std::sqrt(under) > 0.0;
}

AirDepth airDensityAt(const math::Vector& at) {
	const double height = std::max(math::length(at) - planetRadius, 0.0);
	return AirDepth {
		.air  = std::exp(-height / airScaleHeight),
		.haze = std::exp(-height / hazeScaleHeight),
	};
}

AirDepth sunwardDepth(const math::Vector& from, const math::Vector& sun) {
	const double reach = shellLeaving(from, sun, airTop);

	AirDepth depth = {
		.air  = 0,
		.haze = 0,
	};

	for (int step = 0; step < sunSteps; ++step) {
		const double near    = double(step) / sunSteps;
		const double far     = double(step + 1) / sunSteps;
		const double middle  = (double(step) + 0.5) / sunSteps;
		const AirDepth there = airDensityAt(from + sun * (reach * middle * middle));
		const double span    = reach * (far * far - near * near);
		depth.air += there.air * span;
		depth.haze += there.haze * span;
	}
	return depth;
}

void scatteredAt(const ViewMarch& march, const AirDepth& there, const AirDepth& crossed, double span, double* air, double* dust) {
	for (std::size_t colour = 0; colour < airColours; ++colour) {
		const double nanometres = tableWavelength(colour);
		const double scattered  = hazeScattering(nanometres, march.haze);
		const double thinned    = std::exp(-(airScattering(nanometres) * crossed.air + scattered / hazeAlbedo * crossed.haze));
		air[colour] += thinned * there.air * span * airScattering(nanometres);
		dust[colour] += thinned * there.haze * span * scattered;
	}
}

void marchedView(const ViewMarch& march, double* air, double* dust) {
	const math::Vector eye = { 0.0, 0.0, planetRadius + eyeHeight };
	const double reach     = shellLeaving(eye, march.view, airTop);

	AirDepth seen = {
		.air  = 0,
		.haze = 0,
	};

	for (int step = 0; step < viewSteps; ++step) {
		const double near     = double(step) / viewSteps;
		const double far      = double(step + 1) / viewSteps;
		const double middle   = (double(step) + 0.5) / viewSteps;
		const double span     = reach * (far * far - near * near);
		const math::Vector at = eye + march.view * (reach * middle * middle);
		const AirDepth there  = airDensityAt(at);
		seen.air += there.air * span * 0.5;
		seen.haze += there.haze * span * 0.5;
		if (!groundAhead(at, march.sun)) {
			const AirDepth lit = sunwardDepth(at, march.sun);

			const AirDepth crossed = {
				.air  = seen.air + lit.air,
				.haze = seen.haze + lit.haze,
			};

			scatteredAt(march, there, crossed, span, air, dust);
		}
		seen.air += there.air * span * 0.5;
		seen.haze += there.haze * span * 0.5;
	}
}

math::Vector localView(double height, double bearing) {
	const double level = std::cos(height);
	return { level * std::cos(bearing), level * std::sin(bearing), std::sin(height) };
}

double rowHeight(std::size_t row) {
	const double along = double(row) / double(airHeights - 1);
	return 0.5 * math::pi * along * along;
}

double columnBearing(std::size_t column) {
	return math::pi * double(column) / double(airBearings - 1);
}

AirPlace airPlaceOf(const Atmosphere& atmosphere, const math::Vector& direction) {
	const double rise    = std::clamp(math::dot(direction, atmosphere.up), -1.0, 1.0);
	const double height  = std::asin(rise);
	const double bearing = std::abs(std::atan2(math::dot(direction, atmosphere.aside), math::dot(direction, atmosphere.level)));
	return AirPlace {
		.row    = std::sqrt(std::max(height, 0.0) / (0.5 * math::pi)) * double(airHeights - 1),
		.column = bearing / math::pi * double(airBearings - 1),
		.height = height,
		.cosine = math::dot(direction, atmosphere.sun),
	};
}

double share(double place, std::size_t low, int side) {
	const double up = std::clamp(place - double(low), 0.0, 1.0);
	return side != 0 ? up : 1.0 - up;
}

double airTableAt(const std::vector<double>& table, const AirPlace& place, double colour) {
	const std::size_t row    = std::min(std::size_t(place.row), airHeights - 2);
	const std::size_t column = std::min(std::size_t(place.column), airBearings - 2);
	const std::size_t low    = std::min(std::size_t(colour), airColours - 2);
	double held              = 0.0;
	for (int dr = 0; dr < 2; ++dr) {
		for (int dc = 0; dc < 2; ++dc) {
			for (int dl = 0; dl < 2; ++dl) {
				const double weight = share(place.row, row, dr) * share(place.column, column, dc) * share(colour, low, dl);
				held += weight * table[((row + std::size_t(dr)) * airBearings + column + std::size_t(dc)) * airColours + low + std::size_t(dl)];
			}
		}
	}
	return held;
}

double skyTableAt(const Atmosphere& atmosphere, const AirPlace& place, double colour) {
	return airPhase(place.cosine) * airTableAt(atmosphere.air, place, colour) + hazePhase(place.cosine) * airTableAt(atmosphere.haze, place, colour);
}

SpectrumTable flatTable(double value) {
	SpectrumTable table = {
		.values = {},
	};

	for (double& one : table.values) {
		one = value;
	}
	return table;
}

Atmosphere noAtmosphere(const Lighting& lighting) {
	return Atmosphere {
		.present  = false,
		.sunLight = std::uint32_t(lighting.lights.size()),
		.up       = { 0, 0, 1 },
		.sun      = { 0, 0, 1 },
		.level    = { 1, 0, 0 },
		.aside    = { 0, 1, 0 },
		.sunlight = { 0, 0, 0 },
		.ground   = { 0, 0, 0 },
		.air      = {},
		.haze     = {},
		.passed   = flatTable(1),
		.landing  = flatTable(0),
	};
}

void levelFrame(Atmosphere& atmosphere) {
	const math::Vector level = atmosphere.sun - atmosphere.up * math::dot(atmosphere.sun, atmosphere.up);
	atmosphere.level         = math::length(level) < 1e-9 ? math::basisAround(atmosphere.up).tangent : math::normalized(level);
	atmosphere.aside         = math::cross(atmosphere.up, atmosphere.level);
}

math::Vector localOf(const Atmosphere& atmosphere, const math::Vector& direction) {
	return { math::dot(direction, atmosphere.level), math::dot(direction, atmosphere.aside), math::dot(direction, atmosphere.up) };
}

void marchedTables(Atmosphere& atmosphere, double haze) {
	atmosphere.air.assign(airHeights * airBearings * airColours, 0.0);
	atmosphere.haze.assign(airHeights * airBearings * airColours, 0.0);
	for (std::size_t row = 0; row < airHeights; ++row) {
		for (std::size_t column = 0; column < airBearings; ++column) {
			const std::size_t first = (row * airBearings + column) * airColours;

			const ViewMarch march = {
				.view = localView(rowHeight(row), columnBearing(column)),
				.sun  = localOf(atmosphere, atmosphere.sun),
				.haze = haze,
			};

			marchedView(march, &atmosphere.air[first], &atmosphere.haze[first]);
		}
	}
}

SpectrumTable passedTable(const Atmosphere& atmosphere, double haze) {
	const math::Vector eye  = { 0.0, 0.0, planetRadius + eyeHeight };
	const math::Vector sun  = localOf(atmosphere, atmosphere.sun);
	const AirDepth straight = sunwardDepth(eye, sun);
	SpectrumTable passed    = flatTable(0);
	for (std::size_t colour = 0; colour < airColours && !groundAhead(eye, sun); ++colour) {
		const double nanometres = tableWavelength(colour);
		passed.values[colour]   = std::exp(-(airScattering(nanometres) * straight.air + hazeScattering(nanometres, haze) / hazeAlbedo * straight.haze));
	}
	return passed;
}

SpectrumTable landingTable(const Atmosphere& atmosphere) {
	SpectrumTable landing = flatTable(0);
	for (int ring = 0; ring < landingRings; ++ring) {
		const double rise  = (double(ring) + 0.5) / landingRings;
		const double level = std::sqrt(1.0 - rise * rise);
		for (int spoke = 0; spoke < landingSpokes; ++spoke) {
			const double turn            = 2.0 * math::pi * (double(spoke) + 0.5) / landingSpokes;
			const math::Vector direction = atmosphere.level * (level * std::cos(turn)) + atmosphere.aside * (level * std::sin(turn)) + atmosphere.up * rise;
			const AirPlace place         = airPlaceOf(atmosphere, direction);
			for (std::size_t colour = 0; colour < airColours; ++colour) {
				landing.values[colour] += skyTableAt(atmosphere, place, double(colour)) * rise * (2.0 * math::pi / (landingRings * landingSpokes));
			}
		}
	}
	const double overhead = std::max(math::dot(atmosphere.sun, atmosphere.up), 0.0);
	for (std::size_t colour = 0; colour < airColours; ++colour) {
		landing.values[colour] += atmosphere.passed.values[colour] * overhead;
	}
	return landing;
}

Atmosphere atmosphereOf(const Lighting& lighting) {
	Atmosphere atmosphere = noAtmosphere(lighting);
	if (lighting.sky.kind != SkyKind::Physical || firstSun(lighting) >= lighting.lights.size()) {
		return atmosphere;
	}
	const Light& sun    = lighting.lights[firstSun(lighting)];
	atmosphere.present  = true;
	atmosphere.sunLight = firstSun(lighting);
	atmosphere.sun      = math::normalized(sun.direction);
	atmosphere.sunlight = sun.power;
	atmosphere.ground   = lighting.sky.ground;
	levelFrame(atmosphere);
	marchedTables(atmosphere, double(lighting.sky.haze));
	atmosphere.passed  = passedTable(atmosphere, double(lighting.sky.haze));
	atmosphere.landing = landingTable(atmosphere);
	return atmosphere;
}

Spectrum4 airSeen(const Atmosphere& atmosphere, const math::Vector& direction, const Wavelengths& wavelengths) {
	const Spectrum4 sunlight = spectrumOf(atmosphere.sunlight, wavelengths);
	const AirPlace place     = airPlaceOf(atmosphere, direction);
	if (place.height < 0.0) {
		return spectrumOf(atmosphere.ground, wavelengths) * spectrumTableAt(atmosphere.landing, wavelengths) * sunlight * (1 / math::pi);
	}
	Spectrum4 shown = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		shown.values[lane] = skyTableAt(atmosphere, place, tablePlace(wavelengths.nanometres[lane]));
	}
	return shown * sunlight;
}

Spectrum4 sunPassed(const Atmosphere& atmosphere, std::uint32_t light, const Wavelengths& wavelengths) {
	if (!atmosphere.present || light != atmosphere.sunLight) {
		return flatSpectrum(1);
	}
	return spectrumTableAt(atmosphere.passed, wavelengths);
}

}
