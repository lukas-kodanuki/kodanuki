#include "plugins/feature/absorption/absorption.h"

#include <cmath>

namespace kodanuki {

Absorption absorptionOf(const Looks& looks) {
	Absorption absorption = {
		.coefficients = {},
	};

	for (const Material& material : looks.materials) {
		absorption.coefficients.push_back(material.absorb);
	}
	return absorption;
}

bool sceneAbsorbs(const Looks& looks) {
	for (const Material& material : looks.materials) {
		const bool absorbing = material.absorb.red > 0 || material.absorb.green > 0 || material.absorb.blue > 0;
		if (material.kind == MaterialKind::Glass && absorbing) {
			return true;
		}
	}
	return false;
}

Spectrum4 keptThrough(const Absorption& absorption, const AbsorbedStretch& stretch) {
	const Spectrum4 depth = spectrumOf(absorption.coefficients[stretch.material], stretch.wavelengths);
	Spectrum4 kept        = flatSpectrum(0);
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		kept.values[lane] = std::exp(-depth.values[lane] * stretch.distance);
	}
	return kept;
}

}
