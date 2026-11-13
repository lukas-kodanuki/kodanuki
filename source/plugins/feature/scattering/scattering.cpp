#include "plugins/feature/scattering/scattering.h"

#include <cmath>

namespace kodanuki {

bool scatters(const Material& material) {
	return material.kind == MaterialKind::Glass && material.scatter > 0;
}

Scattering scatteringOf(const Looks& looks) {
	Scattering scattering = {
		.coefficients = {},
		.leans        = {},
		.phases       = {},
	};

	std::uint32_t next = std::uint32_t(looks.materials.size());
	for (const Material& material : looks.materials) {
		scattering.coefficients.push_back(scatters(material) ? double(material.scatter) : 0);
		scattering.leans.push_back(double(material.lean));
		scattering.phases.push_back(scatters(material) ? next : noMaterial);
		next += scatters(material) ? 1 : 0;
	}
	return scattering;
}

bool sceneScatters(const Looks& looks) {
	for (const Material& material : looks.materials) {
		if (scatters(material)) {
			return true;
		}
	}
	return false;
}

Materials withPhases(const Scattering& scattering, const Materials& materials) {
	Materials featured = materials;
	for (std::size_t material = 0; material < scattering.phases.size(); ++material) {
		if (scattering.phases[material] != noMaterial) {
			featured.finishes.push_back(phaseFinish(scattering.leans[material]));
		}
	}
	return featured;
}

Scattered scatteredAlong(const Scattering& scattering, const ScatteredStretch& stretch) {
	const double coefficient = scattering.coefficients[stretch.material];
	if (!(coefficient > 0)) {
		return Scattered {
			.kept     = 1,
			.collided = false,
			.at       = stretch.distance,
		};
	}
	if (!stretch.walking) {
		return Scattered {
			.kept     = std::exp(-coefficient * stretch.distance),
			.collided = false,
			.at       = stretch.distance,
		};
	}
	const double drawn = -std::log(1 - stretch.number) / coefficient;
	return Scattered {
		.kept     = 1,
		.collided = drawn < stretch.distance,
		.at       = std::min(drawn, stretch.distance),
	};
}

}
