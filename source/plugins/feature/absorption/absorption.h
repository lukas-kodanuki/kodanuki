#pragma once

#include "math/colour.h"
#include "math/spectrum.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What the absorption feature reads: how much a unit of travel inside
 * each material's body absorbs.
 */
struct Absorption {
	/**
	 * The absorption coefficient of each material per metre and channel,
	 * matching Looks::materials one for one.
	 */
	std::vector<Colour> coefficients;
};

/**
 * A stretch of travel inside one material's body.
 */
struct AbsorbedStretch {
	/**
	 * The material, an index into Absorption::coefficients.
	 */
	std::uint32_t material;
	/**
	 * How long the stretch is.
	 */
	double distance;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * Return each material's absorption, read from its `absorb` field.
 *
 * @param looks The scene's looks: its materials and coats.
 */
Absorption absorptionOf(const Looks& looks);

/**
 * Return whether any glass of a scene absorbs, so that the feature is
 * on.
 *
 * @param looks The scene's looks: its materials and coats.
 */
bool sceneAbsorbs(const Looks& looks);

/**
 * Return what a ray keeps over a stretch inside a body, per lane.
 *
 * It is Beer's law over the coefficient upsampled to the lanes: each
 * lane keeps e to the minus its coefficient times the distance.
 *
 * @param absorption The coefficients.
 * @param stretch The material, the distance and the wavelengths.
 */
Spectrum4 keptThrough(const Absorption& absorption, const AbsorbedStretch& stretch);

}
