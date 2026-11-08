#include "extras/feature_correct_dispersion/dispersion.h"

namespace kodanuki {

Dispersion dispersionOf(const Looks& looks) {
	Dispersion dispersion = {
		.settled = {},
		.spread  = {},
		.filmed  = {},
	};

	for (const Material& material : looks.materials) {
		dispersion.settled.push_back(double(material.refraction));
		dispersion.spread.push_back(material.kind == MaterialKind::Glass ? double(material.dispersion) : 0);
		dispersion.filmed.push_back(material.kind == MaterialKind::Glass && material.film[0] > 0);
	}
	return dispersion;
}

bool sceneDisperses(const Looks& looks) {
	for (const Material& material : looks.materials) {
		if (material.kind == MaterialKind::Glass && material.dispersion != 0) {
			return true;
		}
	}
	return false;
}

bool sceneFilmsGlass(const Looks& looks) {
	for (const Material& material : looks.materials) {
		if (material.kind == MaterialKind::Glass && material.film[0] > 0) {
			return true;
		}
	}
	return false;
}

Sides filmedSides(const Dispersion& dispersion, const DispersedQuery& query) {
	Sides sides  = query.sides;
	sides.parted = sides.parted || dispersion.filmed[query.material];
	return sides;
}

double cauchyIndex(const Dispersion& dispersion, std::uint32_t material, double nanometres) {
	const double micrometres = nanometres * 1e-3;
	return dispersion.settled[material] + dispersion.spread[material] / (micrometres * micrometres);
}

Spectrum4 laneIndicesOf(const Dispersion& dispersion, std::uint32_t material, const Wavelengths& wavelengths) {
	Spectrum4 indices = flatSpectrum(1);
	if (material == noMaterial) {
		return indices;
	}
	for (int lane = 0; lane < spectrumLanes; ++lane) {
		indices.values[lane] = cauchyIndex(dispersion, material, wavelengths.nanometres[lane]);
	}
	return indices;
}

Sides dispersedSides(const Dispersion& dispersion, const DispersedQuery& query) {
	const bool inside           = query.sides.inside;
	const std::uint32_t bending = inside ? query.medium : query.material;
	if (bending == noMaterial || dispersion.spread[bending] == 0) {
		return query.sides;
	}
	const std::uint32_t across = inside ? noMaterial : query.material;
	return Sides {
		.travelling = inside ? laneIndicesOf(dispersion, query.medium, query.wavelengths) : query.sides.travelling,
		.beyond     = laneIndicesOf(dispersion, across, query.wavelengths),
		.parted     = true,
		.inside     = inside,
	};
}

}
