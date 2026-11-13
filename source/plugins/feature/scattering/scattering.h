#pragma once

#include "math/spectrum.h"
#include "plugins/transport/paths/material/material.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What the scattering feature reads: each body's coefficient and the
 * finish its collisions stand on.
 */
struct Scattering {
	/**
	 * How much of a metre of travel each material's body turns aside,
	 * the same at every wavelength; zero where it scatters nothing.
	 * Matches Looks::materials one for one.
	 */
	std::vector<double> coefficients;
	/**
	 * Each material's lean, Henyey-Greenstein's g.
	 */
	std::vector<double> leans;
	/**
	 * Each scattering material's phase finish, as an index into the
	 * featured finishes, or noMaterial: the scene's finishes come first,
	 * then one phase finish per scattering body in scene order.
	 */
	std::vector<std::uint32_t> phases;
};

/**
 * A stretch of a scattering body.
 */
struct ScatteredStretch {
	/**
	 * The material whose body it is.
	 */
	std::uint32_t material;
	/**
	 * How long it is.
	 */
	double distance;
	/**
	 * Whether the ray may collide.
	 */
	bool walking;
	/**
	 * The number a collision's distance is drawn with.
	 */
	double number;
};

/**
 * Where along a stretch a ray collides, and what it keeps.
 */
struct Scattered {
	/**
	 * What it keeps beyond its absorption: one for a walking ray, whose
	 * chance of no collision is drawn, and e to the minus the coefficient
	 * times the distance for a crossing one.
	 */
	double kept;
	/**
	 * Whether it collides before the end of the stretch.
	 */
	bool collided;
	/**
	 * How far along it collides, the whole stretch where it does not.
	 */
	double at;
};

/**
 * Return each material's scattering and the index its phase finish will
 * have.
 *
 * @param looks The scene's looks: its materials and coats.
 */
Scattering scatteringOf(const Looks& looks);

/**
 * Return whether any body of a scene scatters, so that the feature is
 * on.
 *
 * @param looks The scene's looks: its materials and coats.
 */
bool sceneScatters(const Looks& looks);

/**
 * Return the finishes with one phase finish appended per scattering
 * body, at the indices Scattering::phases names.
 *
 * @param scattering The leans and the phases.
 * @param materials The core's finishes.
 */
Materials withPhases(const Scattering& scattering, const Materials& materials);

/**
 * Return what a ray keeps along a stretch of a body, and whether it
 * collides.
 *
 * A walking ray collides at minus the logarithm of one less its number,
 * over the coefficient, where that falls inside the stretch. A crossing
 * ray never collides.
 *
 * @param scattering The coefficients.
 * @param stretch The body, the stretch, its use and its number.
 */
Scattered scatteredAlong(const Scattering& scattering, const ScatteredStretch& stretch);

}
