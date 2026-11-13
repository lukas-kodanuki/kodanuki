#pragma once

#include "math/colour.h"
#include "math/spectrum.h"
#include "math/vector.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A lamp as the sky layers see it: whether it is a moon, where its disc
 * lies, and the tint a painted sky gives it.
 */
struct LayeredLamp {
	/**
	 * Whether the lamp is a moon.
	 */
	bool moon;
	/**
	 * The unit direction toward the middle of a sun's or a moon's disc.
	 */
	math::Vector direction;
	/**
	 * The sine of the disc's half angle: its radius on the unit sphere.
	 */
	double radius;
	/**
	 * Whether a painted sky tints the lamp: the first sun and every moon.
	 */
	bool tinted;
	/**
	 * The tint per wavelength, read only where the lamp is tinted.
	 */
	SpectrumTable tint;
};

/**
 * Everything the sky shows over its base that is not the air or the
 * clouds: a painted sky's colours, the glow about the first sun, the
 * stars, and the moons' phases.
 */
struct SkyLayers {
	/**
	 * The unit direction up.
	 */
	math::Vector up;
	/**
	 * The unit direction toward the first sun, zero where there is none.
	 */
	math::Vector sun;
	/**
	 * Whether the sky is painted.
	 */
	bool painted;
	/**
	 * The painted runs of colour, read only where the sky is painted.
	 */
	PaintedSky runs;
	/**
	 * The glow's colour times its strength, black for none.
	 */
	Colour glow;
	/**
	 * How far from the sun the glow falls by a factor of e, in radians.
	 */
	double spread;
	/**
	 * The stars.
	 */
	Stars stars;
	/**
	 * Every lamp, matching Lighting::lights one for one.
	 */
	std::vector<LayeredLamp> lamps;
};

/**
 * Work out a scene's sky layers.
 *
 * A painted sky tints its first sun and every moon, which fade as they
 * sink below the horizon, and reddens the sun toward the colour of the
 * dusk horizon as it sets.
 *
 * @param lighting The scene's lighting: its lights and sky.
 */
SkyLayers skyLayersOf(const Lighting& lighting);

/**
 * Return whether the sky glows about a sun: a glow is given and the
 * scene has a sun.
 *
 * @param layers The layers.
 */
bool skyGlows(const SkyLayers& layers);

/**
 * Return whether the scene has a moon.
 *
 * @param layers The layers.
 */
bool skyHasMoon(const SkyLayers& layers);

/**
 * Return the glow about the first sun along a direction, per lane.
 *
 * It falls as exp(-angle / spread) from the sun, fades out as the sun
 * sinks from the horizon to 8 degrees below it, and fades in over the
 * horizon, 0.02 either side.
 *
 * @param layers The layers.
 * @param direction The unit direction.
 * @param wavelengths The wavelengths.
 */
Spectrum4 glowSeen(const SkyLayers& layers, const math::Vector& direction, const Wavelengths& wavelengths);

/**
 * Return the colours a painted sky shows along a direction, per lane.
 *
 * Each run is read at the sine of the direction's elevation, straight
 * between its stops. Day eases in as the first sun rises from 2 to 12
 * degrees and night as it sinks from -4 to -14; dusk is the rest, shown
 * toward the sun and shared out to day and night away from it. Without
 * a sun the sky shows the day.
 *
 * @param layers The layers.
 * @param direction The unit direction.
 * @param wavelengths The wavelengths.
 */
Spectrum4 paintedSeen(const SkyLayers& layers, const math::Vector& direction, const Wavelengths& wavelengths);

/**
 * Return the stars along a direction, per lane.
 *
 * The sphere is cut into the six faces of a cube and each face into
 * square cells, about count in all, with one star in each, placed, made
 * bright (strength times a number to the sixth power) and coloured from
 * blue-white to orange by a hash of the cell. Nothing shows below the
 * horizon, and the rest shows as far as the first sun has set, from 2
 * to 12 degrees below the horizon; without a sun the stars show in
 * full.
 *
 * @param layers The layers.
 * @param direction The unit direction.
 * @param wavelengths The wavelengths.
 */
Spectrum4 starsSeen(const SkyLayers& layers, const math::Vector& direction, const Wavelengths& wavelengths);

/**
 * Return the share of a moon's disc that is lit where a direction meets
 * it: one for any other lamp, and for a moon in a scene without a sun.
 *
 * The point on the disc is taken on the moon's sphere facing the scene,
 * and it is lit by a smooth step of the cosine between its normal and
 * the first sun, 0.05 wide either side of the terminator.
 *
 * @param layers The layers.
 * @param light The lamp, as an index into Lighting::lights.
 * @param direction The unit direction toward the point on the disc.
 */
double moonLit(const SkyLayers& layers, std::uint32_t light, const math::Vector& direction);

/**
 * Return the tint a painted sky gives a lamp, per lane: flat one for a
 * lamp it does not tint.
 *
 * @param layers The layers.
 * @param light The lamp, as an index into Lighting::lights.
 * @param wavelengths The wavelengths.
 */
Spectrum4 settingTint(const SkyLayers& layers, std::uint32_t light, const Wavelengths& wavelengths);

}
