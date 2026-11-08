#include "check.h"
#include "spectral/colour.h"
#include "spectral/spectrum.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kodanuki {

void checkFlatWhite() {
	double worst = 0;
	for (int k = 0; k < 4096; ++k) {
		const Wavelengths wavelengths = sampledWavelengths((k + 0.5) / 4096);
		const Spectrum4 flat          = flatSpectrum(1);
		const Colour colour           = colourOf(wavelengths, flat);
		const Colour spread           = colourOf(spreadWavelengths((k + 0.5) / 4096), flat);
		worst                         = std::max({ worst, std::abs(colour.red - 1), std::abs(colour.green - 1), std::abs(colour.blue - 1), std::abs(spread.red - 1), std::abs(spread.green - 1), std::abs(spread.blue - 1) });
	}
	std::printf("spectral: a flat unit spectrum strays %.4f from white at worst over one draw\n", worst);
	Colour mean = { 0, 0, 0 };
	for (int k = 0; k < 4096; ++k) {
		const Wavelengths wavelengths = sampledWavelengths((k + 0.5) / 4096);
		mean                          = mean + colourOf(wavelengths, flatSpectrum(1)) * (1.0 / 4096);
	}
	checkNear(Near { mean.red, 1, 2e-3, "colourOf of a flat unit spectrum is white in red" });
	checkNear(Near { mean.green, 1, 2e-3, "colourOf of a flat unit spectrum is white in green" });
	checkNear(Near { mean.blue, 1, 2e-3, "colourOf of a flat unit spectrum is white in blue" });
}

void checkDensityIntegral() {
	const int steps = 32000;
	double total    = 0;
	for (int k = 0; k < steps; ++k) {
		const double nanometres = shortestWavelength + (k + 0.5) * (longestWavelength - shortestWavelength) / steps;
		total += wavelengthDensity(nanometres) * (longestWavelength - shortestWavelength) / steps;
	}
	checkNear(Near { total, 1, 1e-4, "the wavelength density integrates to one" });
	const Wavelengths drawn = sampledWavelengths(0.3);
	checkNear(Near { drawn.density[2], wavelengthDensity(drawn.nanometres[2]), 1e-12, "a drawn lane keeps the density at its wavelength" });
}

void checkUpsampling() {
	const Wavelengths wavelengths = sampledWavelengths(0.17);
	const Colour grey40           = { 0.4, 0.4, 0.4 };
	const Spectrum4 grey          = spectrumOf(grey40, wavelengths);
	checkNear(Near { laneMaximum(grey) - 0.4, 0, 1e-12, "a grey upsamples flat" });
	const Colour orange = { 0.8, 0.3, 0.1 };
	Colour mean         = { 0, 0, 0 };
	for (int k = 0; k < 4096; ++k) {
		const Wavelengths drawn = sampledWavelengths((k + 0.5) / 4096);
		mean                    = mean + colourOf(drawn, spectrumOf(orange, drawn)) * (1.0 / 4096);
	}
	std::printf("spectral: [0.8, 0.3, 0.1] upsampled reads back as [%.4f, %.4f, %.4f]\n", mean.red, mean.green, mean.blue);
	check(std::abs(mean.red - 0.8) < 0.05 && std::abs(mean.green - 0.3) < 0.05 && std::abs(mean.blue - 0.1) < 0.05, "an upsampled colour reads back near itself");
}

void checkSpectral() {
	checkFlatWhite();
	checkDensityIntegral();
	checkUpsampling();
}

}
