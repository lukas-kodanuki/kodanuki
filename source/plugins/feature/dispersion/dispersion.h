#pragma once

#include "math/spectrum.h"
#include "plugins/transport/paths/material/material.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * Each material's index as Cauchy's formula gives it, and which glasses
 * wear a film, whose colours part the lanes too.
 */
struct Dispersion {
	/**
	 * Each material's A, its refraction, matching Looks::materials.
	 */
	std::vector<double> settled;
	/**
	 * Each material's B in square micrometres, zero where it bends every
	 * wavelength alike.
	 */
	std::vector<double> spread;
	/**
	 * Whether each material is a glass that wears a film.
	 */
	std::vector<bool> filmed;
};

/**
 * A surface's sides to fill per lane by Cauchy's formula.
 */
struct DispersedQuery {
	/**
	 * The material met.
	 */
	std::uint32_t material;
	/**
	 * The material whose body the ray travels in, or noMaterial.
	 */
	std::uint32_t medium;
	/**
	 * The sides with every lane at the core's index.
	 */
	Sides sides;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * Return each material's Cauchy terms.
 *
 * @param looks The scene's looks: its materials and coats.
 */
Dispersion dispersionOf(const Looks& looks);

/**
 * Return whether any glass of a scene disperses, so that the feature is
 * on.
 *
 * @param looks The scene's looks: its materials and coats.
 */
bool sceneDisperses(const Looks& looks);

/**
 * Return whether any glass of a scene wears a film, so that the thin
 * film feature is on.
 *
 * @param looks The scene's looks: its materials and coats.
 */
bool sceneFilmsGlass(const Looks& looks);

/**
 * Return a surface's sides parted where the material met is a glass
 * that wears a film: the film's reflectance differs from lane to lane,
 * so a passage keeps the hero alone.
 *
 * @param dispersion Which glasses wear a film.
 * @param query The material met and the sides.
 */
Sides filmedSides(const Dispersion& dispersion, const DispersedQuery& query);

/**
 * Return a surface's sides with each lane's index by Cauchy's formula,
 * A + B over the wavelength in micrometres squared, on the side the ray
 * travels in (its body's, or 1 in the open) and the side across (1 when
 * leaving a body, the material's when entering it). The sides are
 * parted where the body that bends the ray disperses: the one the ray
 * leaves, or the one it enters.
 *
 * @param dispersion The terms.
 * @param query The materials, the sides and the wavelengths.
 */
Sides dispersedSides(const Dispersion& dispersion, const DispersedQuery& query);

}
