#include "math/spectrum.h"

#include <algorithm>

namespace kodanuki {

constexpr double visibleRange   = longestWavelength - shortestWavelength;
constexpr double blueGreenLower = 445.0;
constexpr double blueGreenUpper = 530.0;
constexpr double greenRedLower  = 575.0;
constexpr double greenRedUpper  = 605.0;

Spectrum4 operator+(const Spectrum4& a, const Spectrum4& b) {
	return { { a.values[0] + b.values[0], a.values[1] + b.values[1], a.values[2] + b.values[2], a.values[3] + b.values[3] } };
}

Spectrum4 operator-(const Spectrum4& a, const Spectrum4& b) {
	return { { a.values[0] - b.values[0], a.values[1] - b.values[1], a.values[2] - b.values[2], a.values[3] - b.values[3] } };
}

Spectrum4 operator*(const Spectrum4& a, const Spectrum4& b) {
	return { { a.values[0] * b.values[0], a.values[1] * b.values[1], a.values[2] * b.values[2], a.values[3] * b.values[3] } };
}

Spectrum4 operator*(const Spectrum4& a, double s) {
	return { { a.values[0] * s, a.values[1] * s, a.values[2] * s, a.values[3] * s } };
}

Spectrum4 operator/(const Spectrum4& a, double s) {
	return a * (1 / s);
}

Spectrum4 flatSpectrum(double value) {
	return { { value, value, value, value } };
}

double laneMean(const Spectrum4& spectrum) {
	return 0.25 * (spectrum.values[0] + spectrum.values[1] + spectrum.values[2] + spectrum.values[3]);
}

double laneMaximum(const Spectrum4& spectrum) {
	return std::max(std::max(spectrum.values[0], spectrum.values[1]), std::max(spectrum.values[2], spectrum.values[3]));
}

Spectrum4 collapsedToHero(const Spectrum4& throughput) {
	return { { throughput.values[0] * spectrumLanes, 0, 0, 0 } };
}

double smoothRise(double nanometres, double lower, double upper) {
	const double blend = std::clamp((nanometres - lower) / (upper - lower), 0.0, 1.0);
	return blend * blend * (3 - 2 * blend);
}

double basisRed(double nanometres) {
	return smoothRise(nanometres, greenRedLower, greenRedUpper);
}

double basisBlue(double nanometres) {
	return 1 - smoothRise(nanometres, blueGreenLower, blueGreenUpper);
}

double basisGreen(double nanometres) {
	return (1 - basisBlue(nanometres)) * (1 - basisRed(nanometres));
}

Wavelengths wavelengthsAt(const double* nanometres, const double* density) {
	Wavelengths drawn = {
		.nanometres = { float(nanometres[0]), float(nanometres[1]), float(nanometres[2]), float(nanometres[3]) },
		.density    = { float(density[0]), float(density[1]), float(density[2]), float(density[3]) },
		.red        = flatSpectrum(0),
		.green      = flatSpectrum(0),
		.blue       = flatSpectrum(0),
	};

	for (int lane = 0; lane < spectrumLanes; ++lane) {
		drawn.red.values[lane]   = basisRed(nanometres[lane]);
		drawn.green.values[lane] = basisGreen(nanometres[lane]);
		drawn.blue.values[lane]  = basisBlue(nanometres[lane]);
	}
	return drawn;
}

double tablePlace(double nanometres) {
	const double along = std::clamp((nanometres - shortestWavelength) / visibleRange, 0.0, 1.0);
	return along * (tablePlaces - 1);
}

Spectrum4 spectrumTableAt(const SpectrumTable& table, const Wavelengths& wavelengths) {
	Spectrum4 read = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		const double place = tablePlace(wavelengths.nanometres[lane]);
		const int lower    = std::min(int(place), tablePlaces - 2);
		const double share = place - lower;
		read.values[lane]  = table.values[lower] + (table.values[lower + 1] - table.values[lower]) * share;
	}
	return read;
}

Wavelengths spreadWavelengths(double u) {
	const double stride = visibleRange / spectrumLanes;
	const double hero   = shortestWavelength + u * visibleRange;
	double nanometres[4];
	double density[4];
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		const double raw = hero + stride * lane;
		nanometres[lane] = raw >= longestWavelength ? raw - visibleRange : raw;
		density[lane]    = 1 / visibleRange;
	}
	return wavelengthsAt(nanometres, density);
}

}
