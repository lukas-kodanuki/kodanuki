#pragma once

#include "math/colour.h"
#include "math/spectrum.h"
#include "math/vector.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The air over a scene lit by its first sun, worked out per wavelength.
 *
 * Sunlight is scattered once toward the eye by the air, which turns
 * short wavelengths aside far more than long ones, and by the haze,
 * which turns all alike and mostly forward. What each scatters toward
 * the eye before its lean is held in a table by height, bearing from
 * the sun and wavelength; the lean, sharp about the sun, is worked out
 * when asked. Everything is per unit of the sun's light above the air.
 */
struct Atmosphere {
	/**
	 * Whether the scene has a physical sky.
	 */
	bool present;
	/**
	 * The first sun, as an index into Lighting::lights.
	 */
	std::uint32_t sunLight;
	/**
	 * The unit direction up.
	 */
	math::Vector up;
	/**
	 * The unit direction toward the first sun.
	 */
	math::Vector sun;
	/**
	 * The unit direction level with the horizon toward the sun, which
	 * bearings are measured from.
	 */
	math::Vector level;
	/**
	 * The level unit direction a quarter turn from level.
	 */
	math::Vector aside;
	/**
	 * The sun's power above the air, per channel.
	 */
	Colour sunlight;
	/**
	 * The fraction of light the ground below the horizon sends back.
	 */
	Colour ground;
	/**
	 * What the air scatters toward the eye before its lean, by height,
	 * bearing and wavelength, in that order of nesting.
	 */
	std::vector<double> air;
	/**
	 * The same for the haze.
	 */
	std::vector<double> haze;
	/**
	 * The fraction of the sun's light that reaches the ground straight
	 * through the air; zero where the sun has set.
	 */
	SpectrumTable passed;
	/**
	 * What lands on flat ground from the sun and the whole sky.
	 */
	SpectrumTable landing;
};

/**
 * Work out the air of a scene's physical sky, or an empty one.
 *
 * The tables are marched once: 48 heights, 32 bearings and 33
 * wavelengths, 40 steps along each view and 12 toward the sun, over a
 * planet of 6360 km with its air to 6420 km.
 *
 * @param lighting The scene's lighting: its lights and sky.
 */
Atmosphere atmosphereOf(const Lighting& lighting);

/**
 * Return the radiance the air shows along a direction, per lane.
 *
 * Below the horizon it shows the ground, lit by the sun and the sky.
 *
 * @param atmosphere The air.
 * @param direction The unit direction.
 * @param wavelengths The wavelengths.
 */
Spectrum4 airSeen(const Atmosphere& atmosphere, const math::Vector& direction, const Wavelengths& wavelengths);

/**
 * Return the fraction of the first sun's light the air lets through to
 * the ground, per lane; one for any other light.
 *
 * @param atmosphere The air.
 * @param light The light, as an index into Lighting::lights.
 * @param wavelengths The wavelengths.
 */
Spectrum4 sunPassed(const Atmosphere& atmosphere, std::uint32_t light, const Wavelengths& wavelengths);

}
