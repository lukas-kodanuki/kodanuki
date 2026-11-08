#pragma once

#include "math/random.h"
#include "math/vector.h"
#include "sdf/scene.h"
#include "spectral/colour.h"
#include "spectral/spectrum.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The glow cell of an aim at no glow cell.
 */
constexpr std::uint32_t noCell = 0xFFFFFFFFu;

/**
 * A quad's edges as a frame, worked out once for every view of it.
 */
struct QuadEdges {
	/**
	 * The length of the first edge.
	 */
	double alongLength;
	/**
	 * The length of the second edge.
	 */
	double acrossLength;
	/**
	 * The first edge's unit direction.
	 */
	math::Vector x;
	/**
	 * The second edge's unit direction.
	 */
	math::Vector y;
	/**
	 * The cross product of x and y.
	 */
	math::Vector raw;
};

/**
 * A light of the scene, worked out in double at load.
 *
 * The kind says which fields are read.
 */
struct Lamp {
	/**
	 * Which of the five lights it is.
	 */
	LightKind kind;
	/**
	 * The centre of a point or a sphere, or a quad's corner.
	 */
	math::Vector at;
	/**
	 * The unit direction toward a directional light, a sun or a moon.
	 */
	math::Vector direction;
	/**
	 * A quad's first edge.
	 */
	math::Vector along;
	/**
	 * A quad's second edge.
	 */
	math::Vector across;
	/**
	 * The unit face normal of a quad, along the cross product of its
	 * edges. A quad shines on that side only.
	 */
	math::Vector facing;
	/**
	 * A sphere's radius.
	 */
	double radius;
	/**
	 * The cosine of a sun's or a moon's half angle.
	 */
	double cosine;
	/**
	 * What the scene says the light sends: the intensity of a point
	 * light, the irradiance a directional light, a sun or a full moon lands on a
	 * surface facing it, and for a sphere or a quad its radiance times
	 * pi r squared or times its area.
	 */
	Colour power;
	/**
	 * The radiance off a sphere, a quad or a sun's or a full moon's disc.
	 */
	Colour radiance;
	/**
	 * The area of a sphere or a quad, zero for the others.
	 */
	double area;
	/**
	 * The power the light gives off, from the mean of its channels.
	 */
	double emitted;
	/**
	 * A quad's edges as a frame, quadEdgesOf() along and across.
	 */
	QuadEdges edges;
};

/**
 * The sky: three colours blended along the up axis.
 */
struct GradientSky {
	/**
	 * Which way is up.
	 */
	math::Vector up;
	/**
	 * The radiance looking straight up.
	 */
	Colour zenith;
	/**
	 * The radiance looking level.
	 */
	Colour horizon;
	/**
	 * The radiance looking straight down.
	 */
	Colour ground;
};

/**
 * Every light of a frame, numbered: the lamps in scene order, then the
 * sky last.
 *
 * Aim extras add lights between the lamps and the sky, as the Sampler
 * point says.
 */
struct Lights {
	/**
	 * The lamps.
	 */
	std::vector<Lamp> lamps;
	/**
	 * The sky.
	 */
	GradientSky sky;
};

/**
 * An aim at one light from a point.
 */
struct AimQuery {
	/**
	 * The light's number.
	 */
	std::uint32_t light;
	/**
	 * The point aimed from.
	 */
	math::Vector from;
	/**
	 * The unit normal on the side the point aims into, or zero for a point
	 * on no surface, which aims every way.
	 */
	math::Vector normal;
	/**
	 * The two numbers the point on the light is drawn with.
	 */
	math::UnitPair pair;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * A direction drawn toward a light.
 */
struct LightAim {
	/**
	 * Whether a direction was drawn.
	 */
	bool found;
	/**
	 * The unit direction toward the light.
	 */
	math::Vector toward;
	/**
	 * How far the light lies along it, infinity for a distant light.
	 */
	double distance;
	/**
	 * What arrives from the light over the density the direction was
	 * drawn with, per lane: for a point light its intensity over the
	 * squared distance, for a directional light its irradiance.
	 */
	Spectrum4 radiance;
	/**
	 * For the sky, the radiance along the aim before it is divided by
	 * the density, which the sky's features are worn on (skyAt); for
	 * any other light, radiance again.
	 */
	Spectrum4 raw;
	/**
	 * The solid-angle density the direction was drawn with, zero for a
	 * light with no extent.
	 */
	double density;
	/**
	 * The glow cell aimed at, or noCell for every other light. An aim
	 * at a glow cell sends the cell's guess over the density, and
	 * distance is where the direction leaves the cell's box.
	 */
	std::uint32_t cell;
};

/**
 * A direction toward a light whose aim density is asked.
 */
struct AimDensityQuery {
	/**
	 * The light's number.
	 */
	std::uint32_t light;
	/**
	 * The point aimed from.
	 */
	math::Vector from;
	/**
	 * The unit direction.
	 */
	math::Vector toward;
	/**
	 * How far along it the light was met.
	 */
	double distance;
};

/**
 * A ray asked whether it meets one light.
 */
struct MeetQuery {
	/**
	 * The light's number.
	 */
	std::uint32_t light;
	/**
	 * Where the ray starts.
	 */
	math::Vector origin;
	/**
	 * The unit direction.
	 */
	math::Vector direction;
	/**
	 * How far the ray may go: the hit's distance, or infinity for a ray
	 * that left the scene.
	 */
	double limit;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * Where a ray met a light.
 */
struct LightMeet {
	/**
	 * Whether it met it.
	 */
	bool met;
	/**
	 * How far along the ray, infinity for a distant light.
	 */
	double distance;
	/**
	 * What the light sends back along the ray, per lane.
	 */
	Spectrum4 radiance;
};

/**
 * A point drawn on a lamp, where a path from the light starts.
 */
struct LampPoint {
	/**
	 * The light's number.
	 */
	std::uint32_t light;
	/**
	 * The point.
	 */
	math::Vector point;
	/**
	 * The unit normal of a sphere or a quad, zero for a point light.
	 */
	math::Vector normal;
	/**
	 * The area density it was drawn with: one over the area, or one for
	 * a point light.
	 */
	double density;
};

/**
 * A point to draw on a lamp.
 */
struct LampDraw {
	/**
	 * The light's number.
	 */
	std::uint32_t light;
	/**
	 * The two numbers.
	 */
	math::UnitPair pair;
};

/**
 * Work out a quad's edges as a frame.
 *
 * @param along The first edge.
 * @param across The second edge.
 */
QuadEdges quadEdgesOf(const math::Vector& along, const math::Vector& across);

/**
 * Work out a frame's lights from the scene.
 *
 * @param lighting The scene's lighting: its lights and sky.
 */
Lights lightsOf(const Lighting& lighting);

/**
 * Return how many lights the core numbers: the lamps and the sky.
 *
 * @param lights The lights.
 */
std::uint32_t lightCount(const Lights& lights);

/**
 * Return the sky's number: the last light.
 *
 * @param lights The lights.
 */
std::uint32_t skyLight(const Lights& lights);

/**
 * Return whether a light has no extent, so that no ray meets it and an
 * aim at it draws its one direction: a point or a directional light.
 *
 * @param lights The lights.
 * @param light The light's number.
 */
bool isSingular(const Lights& lights, std::uint32_t light);

/**
 * Return whether a camera ray may meet a light.
 *
 * It meets the sky, and no lamp: a sun's or a moon's disc is seen only off a
 * surface.
 *
 * @param lights The lights.
 * @param light The light's number.
 */
bool seenByCamera(const Lights& lights, std::uint32_t light);

/**
 * Draw a direction toward a lamp from a point.
 *
 * A sphere is drawn over the cone it fills. A square quad is drawn over
 * its solid angle as a spherical rectangle, any other evenly over its
 * area. A sun or a moon is drawn over its cone. A point and a directional light
 * give their one direction at density zero. A lamp wholly below the
 * surface gives nothing, and so does the sky.
 *
 * @param lights The lights.
 * @param query The light, the point, the normal, the numbers and the
 *        wavelengths.
 */
LightAim aimLight(const Lights& lights, const AimQuery& query);

/**
 * Return the solid-angle density aimLight() draws a direction with.
 *
 * It is zero for a light with no extent and for the sky.
 *
 * @param lights The lights.
 * @param query The light, the point and the direction.
 */
double aimDensity(const Lights& lights, const AimDensityQuery& query);

/**
 * Find where a ray meets a light, and what the light sends back along
 * it.
 *
 * A sphere and a quad are met within the limit, a quad from its shining
 * side. A sun or a moon is met by a ray whose limit is infinite and whose
 * direction lies in its cone. The sky is met by every ray with an
 * infinite limit. A point and a directional light are never met.
 *
 * @param lights The lights.
 * @param query The light and the ray.
 */
LightMeet meetLight(const Lights& lights, const MeetQuery& query);

/**
 * Return whether a lamp starts paths from the light: a point light, a
 * sphere or a quad with an extent.
 *
 * @param lights The lights.
 * @param light The light's number.
 */
bool startsPaths(const Lights& lights, std::uint32_t light);

/**
 * Draw a point evenly over a lamp that starts paths.
 *
 * @param lights The lights.
 * @param draw The light and the numbers.
 */
LampPoint pointOnLamp(const Lights& lights, const LampDraw& draw);

/**
 * Return the area density pointOnLamp() draws a point with: one over
 * the area, or one for a point light.
 *
 * @param lights The lights.
 * @param light The light's number.
 */
double lampPointDensity(const Lights& lights, std::uint32_t light);

/**
 * Return what a lamp sends from a point on it, per lane: the radiance
 * of a sphere or a quad, the intensity of a point light.
 *
 * @param lights The lights.
 * @param point The point on the lamp.
 * @param wavelengths The wavelengths.
 */
Spectrum4 lampEmission(const Lights& lights, const LampPoint& point, const Wavelengths& wavelengths);

/**
 * Return the share of light paths each lamp starts, by power.
 *
 * Lamps that start no paths get zero. Without such a lamp every share
 * is zero.
 *
 * @param lights The lights.
 */
std::vector<double> lampShares(const Lights& lights);

/**
 * Return the gradient's colour along a direction: the horizon's blended
 * toward the zenith above it and the ground below it by the square root
 * of the height.
 *
 * @param sky The sky.
 * @param direction The unit direction.
 */
Colour gradientColour(const GradientSky& sky, const math::Vector& direction);

/**
 * Return the sky's radiance along a direction, per lane.
 *
 * Above level, it blends the horizon toward the zenith by the square
 * root of the sine of the elevation. Below level, it blends toward the
 * ground the same way.
 *
 * @param sky The sky.
 * @param direction The unit direction.
 * @param wavelengths The wavelengths.
 */
Spectrum4 skyRadiance(const GradientSky& sky, const math::Vector& direction, const Wavelengths& wavelengths);

}
