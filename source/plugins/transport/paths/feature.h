#pragma once

#include "core/geometry.h"
#include "math/spectrum.h"
#include "math/vector.h"
#include "plugins/feature/absorption/absorption.h"
#include "plugins/feature/atmosphere/atmosphere.h"
#include "plugins/feature/clouds/clouds.h"
#include "plugins/feature/coats/coats.h"
#include "plugins/feature/dispersion/dispersion.h"
#include "plugins/feature/path_rules/path_rules.h"
#include "plugins/feature/scattering/scattering.h"
#include "plugins/feature/sky_layers/sky_layers.h"
#include "plugins/transport/paths/light/light.h"
#include "plugins/transport/paths/material/material.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A piece of scene content beyond the core's materials, lamps and sky.
 *
 * A member adds language fields, scene data and shading. The shading
 * side answers at the moments of this point. With no member on, every
 * moment leaves what it is given unchanged.
 */
enum class FeatureMember : std::uint8_t {
	/**
	 * Beer's law inside glass, on where a glass absorbs.
	 */
	Absorption,
	/**
	 * The air of a physical sky lit by the first sun, and the sun's
	 * light as the air lets it through.
	 */
	Atmosphere,
	/**
	 * A sky of three runs of colour, for day, dusk and night, blended by
	 * how high the first sun stands; the first sun and the moons tinted
	 * as they set.
	 */
	PaintedSky,
	/**
	 * A haze of light about the first sun, over any sky.
	 */
	SkyGlow,
	/**
	 * Points of light on the sky's sphere, showing as the first sun sets.
	 */
	Stars,
	/**
	 * The lit part of a moon's disc, as the first sun lights it.
	 */
	Moon,
	/**
	 * A layer of cloud over a round planet, veiling the sky and thinning
	 * the suns and moons behind it.
	 */
	Clouds,
	/**
	 * Indices that change with the wavelength, by Cauchy's formula.
	 */
	Dispersion,
	/**
	 * A layer on a glass thin enough that light off its two faces
	 * interferes; it parts the lanes. The film itself is the core
	 * material's (Finish::film).
	 */
	ThinFilm,
	/**
	 * Bodies that turn light aside inside, by Henyey-Greenstein.
	 */
	Scattering,
	/**
	 * Changes of a surface's look where a mask weighs it: its albedo,
	 * glow, roughness and shading normal.
	 */
	Coats,
	/**
	 * Rules shapes and lights say about the paths that meet them: shapes
	 * the camera or the lines to the lights pass, caustics refused, and
	 * lights that reach only some groups of shapes.
	 */
	PathRules,
};

/**
 * Every Feature member that is on, and one typed field per member
 * that keeps state.
 */
struct Features {
	/**
	 * The members, in the order their moments run.
	 */
	std::vector<FeatureMember> members;
	/**
	 * The absorption feature's coefficients.
	 */
	Absorption absorption;
	/**
	 * The air of a physical sky, empty without one.
	 */
	Atmosphere atmosphere;
	/**
	 * The painted sky, the glow, the stars and the moons.
	 */
	SkyLayers layers;
	/**
	 * The clouds, lit and laid out, empty without them.
	 */
	Cloudscape clouds;
	/**
	 * Each material's Cauchy terms, and which glasses wear a film.
	 */
	Dispersion dispersion;
	/**
	 * Each scattering body's coefficient and its phase finish.
	 */
	Scattering scattering;
	/**
	 * Every material's roughness ladder.
	 */
	CoatLadders coats;
	/**
	 * Each leaf's rules and groups and each lamp's reach.
	 */
	PathRules rules;
	/**
	 * Whether PathRules is among the members, kept beside them since
	 * every ray asks it.
	 */
	bool ruled;
};

/**
 * How a stretch of a medium is crossed.
 */
enum class MediumUse : std::uint8_t {
	/**
	 * An eye walk's ray: it may collide, and what it keeps is the
	 * absorption alone, since the chance of no collision is drawn.
	 */
	Walking,
	/**
	 * A light walk's ray or a line to a light: it never collides, and
	 * what it keeps is the absorption and the scattering together.
	 */
	Crossing,
};

/**
 * A shaded surface to wear features on.
 */
struct WearQuery {
	/**
	 * The hit.
	 */
	const Hit& hit;
	/**
	 * The shading so far.
	 */
	Shaded shaded;
	/**
	 * The unit heading of the ray.
	 */
	math::Vector heading;
	/**
	 * The scene and the featured finishes.
	 */
	const Shading& shading;
	/**
	 * Whether the ray meets the surface from outside, where a surface
	 * gives off light.
	 */
	bool outside;
	/**
	 * The registers the masks' and reliefs' programs run in (rule 46).
	 */
	std::vector<double>& registers;
};

/**
 * A surface's sides to fill per lane.
 */
struct InterfaceQuery {
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
 * A stretch of a ray inside a glass's body.
 */
struct MediumQuery {
	/**
	 * The material whose body the ray is inside.
	 */
	std::uint32_t material;
	/**
	 * Where the stretch starts.
	 */
	math::Vector origin;
	/**
	 * The unit direction along it.
	 */
	math::Vector heading;
	/**
	 * How long it is.
	 */
	double distance;
	/**
	 * What the ray keeps along it so far, per lane.
	 */
	Spectrum4 kept;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
	/**
	 * Whether the ray may collide.
	 */
	MediumUse use;
	/**
	 * The number a collision's distance is drawn with, read only while
	 * walking.
	 */
	double number;
};

/**
 * What a ray keeps along a medium, and whether it collided.
 */
struct MediumAnswer {
	/**
	 * What it keeps, per lane.
	 */
	Spectrum4 kept;
	/**
	 * Whether it collided before the end of the stretch.
	 */
	bool collided;
	/**
	 * How far along the stretch it collided.
	 */
	double at;
};

/**
 * A ray that left the scene.
 */
struct SkyQuery {
	/**
	 * The unit direction.
	 */
	math::Vector direction;
	/**
	 * The core's sky radiance: the gradient.
	 */
	Spectrum4 radiance;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
	/**
	 * Whether the ray was drawn exactly: a camera ray, or one that has
	 * passed only exact surfaces since. The clouds march such a ray in
	 * full and read any other off their table.
	 */
	bool exact;
};

/**
 * The light a lamp sends a point.
 */
struct LampQuery {
	/**
	 * The lamp's number.
	 */
	std::uint32_t light;
	/**
	 * The point.
	 */
	math::Vector point;
	/**
	 * The unit direction toward the lamp.
	 */
	math::Vector toward;
	/**
	 * What the core says arrives.
	 */
	Spectrum4 radiance;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * What the Feature members are worked out from.
 */
struct FeatureQuery {
	/**
	 * The scene's geometry: its leaves.
	 */
	const Scene& scene;
	/**
	 * The scene's looks: its materials and coats.
	 */
	const Looks& looks;
	/**
	 * The scene's lighting: its lights, sky, clouds and stars.
	 */
	const Lighting& lighting;
	/**
	 * How many threads the cloud table is built on.
	 */
	int threads;
};

/**
 * Return the members a scene's content turns on, with their state
 * worked out: the absorption, the air's tables, the sky layers and the
 * clouds' table.
 *
 * The clouds are lit by the first sun as the air or a painted sky lets
 * it through, or by the first moon, as much of it as is lit, where
 * there is no sun or the sun stands lower than the moon by more than a
 * cosine of 0.05 below the horizon; and by the sky about them: nine
 * directions read through skyAt() with every other member on and the
 * clouds not yet, each as a colour.
 *
 * @param query The scene, its looks and lighting, and the threads.
 */
Features featuresOf(const FeatureQuery& query);

/**
 * Return what the Feature members of a loaded scene file are worked
 * out from.
 *
 * @param file The scene file.
 * @param threads How many threads the cloud table is built on.
 */
FeatureQuery featureQueryOf(const SceneFile& file, int threads);

/**
 * Return whether a ray walking inside a body may collide there: the
 * scattering feature is on and the body scatters. False in the open.
 *
 * @param features The members.
 * @param medium The body's material, or noMaterial.
 */
bool mediumCollides(const Features& features, std::uint32_t medium);

/**
 * Return the finishes with every member's own appended: the load
 * moment's one switch. The scattering feature appends one phase finish
 * per scattering body, then the coats every rung of every ladder;
 * indices the core made stay what they were.
 *
 * @param features The members.
 * @param materials The core's finishes.
 * @param smooth How a glass of roughness 0 answers, for the rungs.
 */
Materials featuredMaterials(const Features& features, const Materials& materials, SmoothGlass smooth);

/**
 * Return the scene material whose body a finish has: the material a
 * coat's rung is drawn from, or the finish itself. Every reader of a
 * body's absorption, dispersion or scattering asks by it.
 *
 * @param features The members.
 * @param material The finish, or noMaterial.
 */
std::uint32_t mediumBody(const Features& features, std::uint32_t material);

/**
 * Return a surface with every member's wear on: the wear moment's one
 * switch.
 *
 * @param features The members.
 * @param query The hit and the shading so far.
 */
Shaded wornAt(const Features& features, const WearQuery& query);

/**
 * Return a surface's sides with every member's indices: the interface
 * moment's one switch.
 *
 * @param features The members.
 * @param query The material and the sides.
 */
Sides sidesAt(const Features& features, const InterfaceQuery& query);

/**
 * Return what a ray keeps along a medium, and whether it collides: the
 * medium moment's one switch.
 *
 * The scattering feature first draws where a walking ray collides, and
 * what a crossing one keeps of the light it turns aside; the absorption
 * then keeps its share up to the collision, or along the whole stretch.
 *
 * @param features The members.
 * @param query The material, the stretch, what it kept so far, its use
 *        and its number.
 */
MediumAnswer mediumAlong(const Features& features, const MediumQuery& query);

/**
 * Return the sky along a direction with every member on it: the sky
 * moment's one switch. Every reader of the sky asks it here: a meet, an
 * aim, the sky map's chart and the clouds' ambient light.
 *
 * The air or a painted sky replaces the core's gradient, the glow and
 * the stars add to it, and the clouds veil the rest last: what lies
 * behind them times what they let through, plus what they send.
 *
 * @param features The members.
 * @param query The direction, the core's radiance and whether the ray
 *        is exact.
 */
Spectrum4 skyAt(const Features& features, const SkyQuery& query);

/**
 * Return the sky along a direction as a colour, for what reads the sky
 * as a whole rather than per path: the gradient's colour where no sky
 * member is on, else skyAt() turned to a colour over 16 spread
 * wavelengths, as a ray that was not drawn exactly sees it.
 *
 * @param features The members.
 * @param sky The core's gradient.
 * @param direction The unit direction.
 */
Colour skyColourAt(const Features& features, const GradientSky& sky, const math::Vector& direction);

/**
 * Return what a lamp sends a point with every member on it: the lamp
 * moment's one switch.
 *
 * The air tints the first sun by what it lets through, a painted sky
 * tints the first sun and the moons as they set, a moon sends only
 * from its lit part, and the clouds thin every sun and moon by what
 * they let reach the point.
 *
 * @param features The members.
 * @param query The lamp, the point, the core's radiance and the
 *        wavelengths.
 */
Spectrum4 lampAt(const Features& features, const LampQuery& query);

/**
 * Return whether a light may end a path: the ending moment's one
 * switch for lamps and the sky. Without the rules feature every light
 * ends every path.
 *
 * @param features The members.
 * @param ending The light, the bits at the vertex next to it and where
 *        that vertex stands.
 */
bool lightEnds(const Features& features, const RuleEnding& ending);

/**
 * Return whether a glowing surface may end a path: the ending moment's
 * switch for glows.
 *
 * @param features The members.
 * @param ending The bits at the vertex next to the glow and the glowing
 *        leaf.
 */
bool glowEnds(const Features& features, const GlowEnding& ending);

/**
 * Return what a path from the eye has met once it stands on a vertex:
 * the step moment's one switch. Without the rules feature the state
 * stays as it is.
 *
 * @param features The members.
 * @param step What it met, the vertex's sharpness and leaf.
 */
RuleState ruledStep(const Features& features, const RuleStep& step);

/**
 * Return what a path from a light has met once it stands on a vertex,
 * in the light's order: the step moment's switch for light walks.
 *
 * @param features The members.
 * @param step What it met, the vertex's sharpness and leaf.
 */
RuleState ruledLightStep(const Features& features, const RuleStep& step);

/**
 * Return whether a ray or a line passes a leaf it hit as if it were not
 * there: the passing moment's one switch. Without the rules feature
 * nothing passes.
 *
 * @param features The members.
 * @param leaf The leaf hit, or noLeaf.
 * @param passage Which way it runs.
 */
bool passes(const Features& features, std::uint32_t leaf, Passage passage);

/**
 * Return whether the rules feature is on.
 *
 * @param features The members.
 */
bool rulesSaid(const Features& features);

/**
 * Return how much of a lamp's power reaches a point as the members
 * see it, for weighing lamps against each other rather than for
 * shading: the clouds' thinning of a sun or a moon toward it times the
 * mean of the tint the air or a painted sky gives the lamp.
 *
 * @param features The members.
 * @param light The lamp, as an index into Lighting::lights.
 * @param point The point.
 */
double lampThinning(const Features& features, std::uint32_t light, const math::Vector& point);

}
