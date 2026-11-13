#include "math/colour.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kodanuki {

constexpr double tableStep   = 5.0;
constexpr int tableSize      = 65;
constexpr int tableSlices    = tableSize - 1;
constexpr double evenShare   = 0.3;
constexpr int wavelengthCuts = 256;

struct Matching {
	std::array<double, tableSize> x;
	std::array<double, tableSize> y;
	std::array<double, tableSize> z;
	Colour white;
	std::array<double, wavelengthCuts + 1> cuts;
};

struct Tristimulus {
	double x;
	double y;
	double z;
};

Colour operator+(const Colour& a, const Colour& b) {
	return { a.red + b.red, a.green + b.green, a.blue + b.blue };
}

Colour operator*(const Colour& a, const Colour& b) {
	return { a.red * b.red, a.green * b.green, a.blue * b.blue };
}

Colour operator*(const Colour& a, double s) {
	return { a.red * s, a.green * s, a.blue * s };
}

Colour colourFrom(const float* channels) {
	return { double(channels[0]), double(channels[1]), double(channels[2]) };
}

Colour colourFromFloats(const std::vector<float>& from) {
	return { from[0], from[1], from[2] };
}

double luminanceOf(const Colour& colour) {
	return 0.2126 * colour.red + 0.7152 * colour.green + 0.0722 * colour.blue;
}

double channelMean(const Colour& colour) {
	return (colour.red + colour.green + colour.blue) / 3;
}

double channelOf(const Colour& colour, int channel) {
	switch (channel) {
		case 0: return colour.red;
		case 1: return colour.green;
	}

	return colour.blue;
}

Spectrum4 basisOf(const Wavelengths& wavelengths, int channel) {
	switch (channel) {
		case 0: return wavelengths.red;
		case 1: return wavelengths.green;
	}

	return wavelengths.blue;
}

int leastChannel(const Colour& colour) {
	if (colour.red <= colour.green && colour.red <= colour.blue) {
		return 0;
	}
	return colour.green <= colour.red && colour.green <= colour.blue ? 1 : 2;
}

int mostChannel(const Colour& colour, int least) {
	const int first  = least == 0 ? 1 : 0;
	const int second = least == 2 ? 1 : 2;
	return channelOf(colour, first) <= channelOf(colour, second) ? second : first;
}

Spectrum4 spectrumOf(const Colour& colour, const Wavelengths& wavelengths) {
	const int least             = leastChannel(colour);
	const int most              = mostChannel(colour, least);
	const double base           = channelOf(colour, least);
	const double low            = channelOf(colour, 3 - least - most) - base;
	const double high           = channelOf(colour, most) - channelOf(colour, 3 - least - most);
	const Spectrum4& leastBasis = basisOf(wavelengths, least);
	const Spectrum4& mostBasis  = basisOf(wavelengths, most);
	Spectrum4 answer            = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		answer.values[lane] = base + low * (1 - leastBasis.values[lane]) + high * mostBasis.values[lane];
	}
	return answer;
}

double skewedLobe(double nanometres, double peak, double below, double above) {
	const double width  = nanometres < peak ? below : above;
	const double offset = (nanometres - peak) / width;
	return std::exp(-0.5 * offset * offset);
}

double fittedX(double nanometres) {
	return 1.056 * skewedLobe(nanometres, 599.8, 37.9, 31.0) + 0.362 * skewedLobe(nanometres, 442.0, 16.0, 26.7) - 0.065 * skewedLobe(nanometres, 501.1, 20.4, 26.2);
}

double fittedY(double nanometres) {
	return 0.821 * skewedLobe(nanometres, 568.8, 46.9, 40.5) + 0.286 * skewedLobe(nanometres, 530.9, 16.3, 31.1);
}

double fittedZ(double nanometres) {
	return 1.217 * skewedLobe(nanometres, 437.0, 11.8, 36.0) + 0.681 * skewedLobe(nanometres, 459.0, 26.0, 13.8);
}

Colour linearOf(const Tristimulus& xyz) {
	return {
		3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z,
		-0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z,
		0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z,
	};
}

Matching tabledCurves() {
	Matching filled = {
		.x     = {},
		.y     = {},
		.z     = {},
		.white = { 0, 0, 0 },
		.cuts  = {},
	};

	Tristimulus sums = { 0, 0, 0 };
	for (int slot = 0; slot < tableSize; ++slot) {
		const double nanometres = shortestWavelength + tableStep * slot;
		filled.x[slot]          = fittedX(nanometres);
		filled.y[slot]          = fittedY(nanometres);
		filled.z[slot]          = fittedZ(nanometres);
		const bool edge         = slot == 0 || slot == tableSize - 1;
		const double weight     = (edge ? 0.5 : 1.0) * tableStep;
		sums                    = { sums.x + filled.x[slot] * weight, sums.y + filled.y[slot] * weight, sums.z + filled.z[slot] * weight };
	}
	filled.white = linearOf(sums);
	return filled;
}

std::array<double, tableSize> seenResponse(const Matching& curves) {
	std::array<double, tableSize> seen = {};
	for (int slot = 0; slot < tableSize; ++slot) {
		const Tristimulus xyz = { curves.x[slot], curves.y[slot], curves.z[slot] };
		const Colour rgb      = linearOf(xyz);
		seen[slot]            = std::abs(rgb.red / curves.white.red) + std::abs(rgb.green / curves.white.green) + std::abs(rgb.blue / curves.white.blue);
	}
	return seen;
}

std::array<double, tableSlices> sliceShares(const Matching& curves) {
	const std::array<double, tableSize> seen = seenResponse(curves);
	double total                             = 0;
	for (int slice = 0; slice < tableSlices; ++slice) {
		total += seen[slice] + seen[slice + 1];
	}
	std::array<double, tableSlices> shares = {};
	for (int slice = 0; slice < tableSlices; ++slice) {
		shares[slice] = (1 - evenShare) * (seen[slice] + seen[slice + 1]) / total + evenShare / tableSlices;
	}
	return shares;
}

std::array<double, wavelengthCuts + 1> cutsOf(const std::array<double, tableSlices>& shares) {
	std::array<double, tableSlices + 1> before = {};
	for (int slice = 0; slice < tableSlices; ++slice) {
		before[slice + 1] = before[slice] + shares[slice];
	}
	std::array<double, wavelengthCuts + 1> cuts = {};
	int slice                                   = 0;
	for (int index = 0; index <= wavelengthCuts; ++index) {
		const double u = double(index) / wavelengthCuts * before[tableSlices];
		while (slice < tableSlices - 1 && before[slice + 1] <= u) {
			++slice;
		}
		const double along = std::clamp((u - before[slice]) / shares[slice], 0.0, 1.0);
		cuts[index]        = shortestWavelength + tableStep * (slice + along);
	}
	cuts[0]              = shortestWavelength;
	cuts[wavelengthCuts] = longestWavelength;
	return cuts;
}

Matching builtMatching() {
	Matching curves = tabledCurves();
	curves.cuts     = cutsOf(sliceShares(curves));
	return curves;
}

const Matching& matching() {
	static const Matching curves = builtMatching();
	return curves;
}

double tableAt(const std::array<double, tableSize>& curve, double nanometres) {
	const double slot  = (nanometres - shortestWavelength) / tableStep;
	const int below    = std::clamp(int(slot), 0, tableSize - 2);
	const double blend = std::clamp(slot - below, 0.0, 1.0);
	return curve[below] + blend * (curve[below + 1] - curve[below]);
}

Colour colourOf(const Wavelengths& wavelengths, const Spectrum4& spectrum) {
	const Matching& curves = matching();
	Tristimulus xyz        = { 0, 0, 0 };
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		if (wavelengths.density[lane] <= 0) {
			continue;
		}
		const double weight     = spectrum.values[lane] / wavelengths.density[lane];
		const double nanometres = wavelengths.nanometres[lane];
		xyz                     = { xyz.x + tableAt(curves.x, nanometres) * weight, xyz.y + tableAt(curves.y, nanometres) * weight, xyz.z + tableAt(curves.z, nanometres) * weight };
	}
	const Tristimulus mean = { xyz.x / spectrumLanes, xyz.y / spectrumLanes, xyz.z / spectrumLanes };
	const Colour rgb       = linearOf(mean);
	return { rgb.red / curves.white.red, rgb.green / curves.white.green, rgb.blue / curves.white.blue };
}

double wavelengthDensity(double nanometres) {
	const Matching& curves = matching();
	const double* above    = std::upper_bound(curves.cuts.data() + 1, curves.cuts.data() + wavelengthCuts, nanometres);
	const int index        = int(above - curves.cuts.data()) - 1;
	return 1 / (wavelengthCuts * (curves.cuts[index + 1] - curves.cuts[index]));
}

Wavelengths sampledWavelengths(double u) {
	const Matching& curves = matching();
	double nanometres[4];
	double density[4];
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		const double raw    = u + 0.25 * lane;
		const double turned = raw >= 1 ? raw - 1 : raw;
		const double place  = turned * wavelengthCuts;
		const int index     = std::min(int(place), wavelengthCuts - 1);
		const double width  = curves.cuts[index + 1] - curves.cuts[index];
		nanometres[lane]    = std::min(curves.cuts[index] + (place - index) * width, std::nextafter(longestWavelength, shortestWavelength));
		density[lane]       = 1 / (wavelengthCuts * width);
	}
	return wavelengthsAt(nanometres, density);
}

double wavelengthShare(double start, std::uint32_t index) {
	const double turned = double(index) * 0.6180339887498949 + start;
	return turned - std::floor(turned);
}

}
