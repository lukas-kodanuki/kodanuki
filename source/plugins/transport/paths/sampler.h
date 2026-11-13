#pragma once

#include "plugins/sampler/glowing_surfaces/glowing_surfaces.h"
#include "plugins/sampler/highlight_guide/guide.h"
#include "plugins/sampler/sky_map/sky_map.h"
#include "plugins/sampler/toward_mirrors/toward_mirrors.h"
#include "plugins/transport/paths/light/light.h"
#include "plugins/transport/paths/material/material.h"
#include "plugins/transport/paths/path.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A way of drawing one kind of number or direction.
 *
 * Each member serves one role: sequence, direction, selection, emission
 * or aim. Each draws and reports the density it drew with, so the mean
 * never depends on which members are on.
 */
enum class SamplerMember : std::uint8_t {
	/**
	 * Sequence: every coordinate is a plain hashed number.
	 */
	Independent,
	/**
	 * Sequence: the first 28 coordinates of an eye path come off an
	 * Owen-scrambled Sobol sequence (plugins/sampler/sobol), the rest are plain.
	 */
	Sobol,
	/**
	 * Direction: the material's own draw.
	 */
	Material,
	/**
	 * Direction: a mixture of the guide's lumps and the material where a
	 * guide steers the surface, the material alone elsewhere
	 * (plugins/sampler/highlight_guide).
	 */
	HighlightGuide,
	/**
	 * Selection: every light shares one shadow ray, and one is kept in
	 * proportion to its offer.
	 */
	ByContribution,
	/**
	 * Selection: at the first vertex each of the first four lamps that
	 * offers anything gets a ray of its own; the rest share one
	 * (plugins/sampler/each_lamp_first).
	 */
	EachLampFirst,
	/**
	 * Emission: by the cosine over a lamp's face, evenly over the sphere
	 * from a point light.
	 */
	Cosine,
	/**
	 * Emission: evenly inside the cones of the balls around what may
	 * relay a light path (plugins/sampler/toward_mirrors).
	 */
	TowardMirrors,
	/**
	 * Aim: the lamps.
	 */
	Lamps,
	/**
	 * Aim: the sky by its chart (plugins/sampler/sky_map). Without it, or with
	 * the sky uncharted, the sky is only met.
	 */
	SkyMap,
	/**
	 * Aim: the glowing surfaces by their cells (plugins/sampler/glowing_surfaces).
	 * Without it, or with no cells, the glows are only met.
	 */
	GlowingSurfaces,
};

/**
 * Whether a light gets a shadow ray of its own or shares one.
 */
enum class Selection : std::uint8_t {
	/**
	 * It is offered with the others and may be kept.
	 */
	Shared,
	/**
	 * It is followed alone, unscaled.
	 */
	Own,
};

/**
 * One emission member and its share of the draws.
 */
struct EmissionShare {
	/**
	 * The member.
	 */
	SamplerMember member;
	/**
	 * Its share, the shares summing to one.
	 */
	double share;
};

/**
 * Every Sampler member that is on, and the states the preparations
 * filled, which the member plugins keep.
 */
struct Sampling {
	/**
	 * The sequence member.
	 */
	SamplerMember sequence;
	/**
	 * The direction member.
	 */
	SamplerMember direction;
	/**
	 * The selection member.
	 */
	SamplerMember selection;
	/**
	 * The emission members and their shares.
	 */
	std::vector<EmissionShare> emission;
	/**
	 * The aim members. Lamps is always first.
	 */
	std::vector<SamplerMember> aims;
	/**
	 * HighlightGuide's survey and pixel guides, empty where no
	 * preparation took them: the plugin's state.
	 */
	const HighlightGuides* guides;
	/**
	 * SkyMap's chart, uncharted where the sky is only met: the plugin's
	 * state.
	 */
	const SkyChart* sky;
	/**
	 * GlowingSurfaces' cells, none where nothing glows or no preparation
	 * gathered them: the plugin's state.
	 */
	const GlowCells* glows;
	/**
	 * TowardMirrors' balls, none where nothing may relay: the plugin's
	 * state.
	 */
	const MirrorBalls* mirrors;
};

/**
 * A number asked of the sequence.
 */
struct NumberQuery {
	/**
	 * What names the path's numbers.
	 */
	const PathNumbers& numbers;
	/**
	 * The coordinate.
	 */
	Coordinate coordinate;
};

/**
 * A direction asked of the direction member at an eye walk's vertex.
 */
struct DirectionQuery {
	/**
	 * The finishes.
	 */
	const Materials& materials;
	/**
	 * The path, standing on the vertex.
	 */
	const Path& path;
	/**
	 * The guide the path keeps.
	 */
	const GuideState& guide;
	/**
	 * The material's numbers.
	 */
	LobeNumbers numbers;
};

/**
 * A pair whose direction density is asked.
 */
struct DirectionDensityQuery {
	/**
	 * The path, standing on the vertex.
	 */
	const Path& path;
	/**
	 * The guide the path keeps.
	 */
	const GuideState& guide;
	/**
	 * The unit direction whose density is asked.
	 */
	math::Vector incoming;
	/**
	 * The material's own density of the direction: bsdfDensity() from
	 * the path's outgoing direction toward incoming, which the caller
	 * often has from respondBsdf() already.
	 */
	double own;
};

/**
 * A path that has just stood on a surface, whose guide is asked.
 */
struct ReachQuery {
	/**
	 * The path, standing on its new vertex.
	 */
	const Path& path;
	/**
	 * The lights.
	 */
	const Lights& lights;
};

/**
 * A light the selection member is asked about.
 */
struct SelectionQuery {
	/**
	 * The light's number.
	 */
	std::uint32_t light;
	/**
	 * The path's depth.
	 */
	int depth;
	/**
	 * How many lights this vertex already gave a ray of their own that
	 * offered anything.
	 */
	std::uint32_t owned;
	/**
	 * The lights.
	 */
	const Lights& lights;
};

/**
 * A direction asked of the emission members.
 */
struct EmissionQuery {
	/**
	 * The lights.
	 */
	const Lights& lights;
	/**
	 * The point on the lamp.
	 */
	const LampPoint& point;
	/**
	 * The number that picks the member, and TowardMirrors' cone.
	 */
	double pick;
	/**
	 * The two numbers.
	 */
	math::UnitPair pair;
};

/**
 * A direction emission drew.
 */
struct EmissionDraw {
	/**
	 * Whether a direction was drawn in front of the face.
	 */
	bool found;
	/**
	 * The unit direction.
	 */
	math::Vector direction;
	/**
	 * The mixture's solid-angle density, every member asked.
	 */
	double density;
};

/**
 * A direction whose emission density is asked.
 */
struct EmissionDensityQuery {
	/**
	 * The lights.
	 */
	const Lights& lights;
	/**
	 * The point on the lamp.
	 */
	const LampPoint& point;
	/**
	 * The unit direction.
	 */
	math::Vector direction;
};

/**
 * An aim asked of the aim members at an eye walk's vertex.
 */
struct AimAsk {
	/**
	 * The light, the point, the normal, the light's two numbers and the
	 * wavelengths.
	 */
	const AimQuery& query;
	/**
	 * What names the path's numbers, for the members that draw more:
	 * AimPart and the jitters.
	 */
	const PathNumbers& numbers;
	/**
	 * The event the numbers are drawn at.
	 */
	int event;
};

/**
 * Return a number of the sequence: the sequence member's one dispatch.
 *
 * @param sampling The members.
 * @param query The numbers and the coordinate.
 */
double numberAt(const Sampling& sampling, const NumberQuery& query);

/**
 * Return the numbers of a coordinate and of the draw after it at the
 * same event, as two numberAt() calls answer them; a pair the sequence
 * holds together is worked out at once.
 *
 * @param sampling The members.
 * @param first The numbers and the pair's first coordinate.
 */
math::UnitPair pairAt(const Sampling& sampling, const NumberQuery& first);

/**
 * Draw the direction an eye walk goes on in: the direction member's one
 * dispatch.
 *
 * The density it reports is the member's.
 *
 * @param sampling The members.
 * @param query The path and the numbers.
 */
Scatter drawDirection(const Sampling& sampling, const DirectionQuery& query);

/**
 * Return the guide a walk starts with: the pixel's where HighlightGuide
 * is on, steering nothing until the walk stands on a surface.
 *
 * @param sampling The members.
 * @param pixel The pixel, by rows from the top left.
 */
GuideState startedGuide(const Sampling& sampling, std::uint32_t pixel);

/**
 * Mark a guide in memory no walk has started in as holding no pixel's
 * lumps unpacked (GuideState.unpacked), which guideRestarted() keeps
 * from one walk to the next and so never writes.
 *
 * @param guide The guide of a walk not yet started, written in place
 *        (rule 46).
 */
void guideForgotten(GuideState& guide);

/**
 * Start a walk's guide again in place, as startedGuide() starts one.
 *
 * It keeps which pixel's lumps the guide holds unpacked, so the memory
 * it is first called on must have been through guideForgotten() or
 * startedGuide().
 *
 * @param sampling The members.
 * @param pixel The pixel, by rows from the top left.
 * @param guide The walk's guide, rewritten in place (rule 46).
 */
void guideRestarted(const Sampling& sampling, std::uint32_t pixel, GuideState& guide);

/**
 * Set the lumps that steer a path's draws on the surface it has just
 * stood on, worked out once for every draw and density there.
 *
 * On the first surface they are the pixel's guide. Past it, where
 * HighlightGuide is on with deep guides, a deep guide is built from
 * the survey where the path travels in the open and its ray was drawn
 * with a density of at least the tunings' least. Elsewhere nothing
 * steers.
 *
 * @param sampling The members.
 * @param query The path on its new vertex and the lights.
 * @param guide The path's guide, its lumps written in place (rule 46).
 */
void guideReached(const Sampling& sampling, const ReachQuery& query, GuideState& guide);

/**
 * Return the density drawDirection() draws a direction with.
 *
 * @param sampling The members.
 * @param query The path and the direction.
 */
double directionDensity(const Sampling& sampling, const DirectionDensityQuery& query);

/**
 * Return whether a light gets a ray of its own: the selection member's
 * one dispatch.
 *
 * A light's aim density is the same either way, so no weight changes.
 *
 * @param sampling The members.
 * @param query The light and the depth.
 */
Selection selectLight(const Sampling& sampling, const SelectionQuery& query);

/**
 * Draw the direction a light path leaves a lamp in: the emission
 * members' one dispatch.
 *
 * @param sampling The members.
 * @param query The lamp point and the numbers.
 */
EmissionDraw drawEmission(const Sampling& sampling, const EmissionQuery& query);

/**
 * Return the mixture density drawEmission() draws a direction with.
 *
 * @param sampling The members.
 * @param query The lamp point and the direction.
 */
double emissionDensity(const Sampling& sampling, const EmissionDensityQuery& query);

/**
 * Return how many lights the aim members number: the lamps, the sky,
 * and the glows as one light past the sky where GlowingSurfaces has
 * cells.
 *
 * @param sampling The members.
 * @param lights The lights.
 */
std::uint32_t aimableCount(const Sampling& sampling, const Lights& lights);

/**
 * Draw a direction toward a light: the aim members' one dispatch.
 *
 * It goes by the light's number: the lamps through Lamps, the sky
 * through SkyMap, the glows through GlowingSurfaces. A light no member
 * aims at gives nothing.
 *
 * @param sampling The members.
 * @param lights The lights.
 * @param ask The light, the point and the path's numbers.
 */
LightAim aimAt(const Sampling& sampling, const Lights& lights, const AimAsk& ask);

/**
 * Return the density aimAt() draws a direction toward a lamp or the sky
 * with, zero for a light no member aims at.
 *
 * @param sampling The members.
 * @param lights The lights.
 * @param query The light, the point and the direction.
 */
double aimDensityAt(const Sampling& sampling, const Lights& lights, const AimDensityQuery& query);

/**
 * Return the density aimAt() draws a direction toward the glows with,
 * for a ray that met a glowing surface: zero where GlowingSurfaces is
 * off or has no cell there.
 *
 * @param sampling The members.
 * @param seen The ray and where it met the surface.
 */
double glowAimDensity(const Sampling& sampling, const GlowSeen& seen);

/**
 * Return what a glance at a glow cell counts of the glow at a place,
 * per unit of glow: one over the cell's guess where the cell holds the
 * place, else zero.
 *
 * @param sampling The members.
 * @param cell The cell aimed at.
 * @param place Where the glance met a surface.
 */
double glanceCounted(const Sampling& sampling, std::uint32_t cell, const math::Vector& place);

/**
 * Return the members `transport/paths` holds itself: Independent,
 * Material, ByContribution, Cosine at a share of one, and Lamps, with
 * every member plugin's state empty
 * (states that live as long as the program).
 */
Sampling coreSampling();

/**
 * Return the members a frame draws with by default: Sobol,
 * HighlightGuide, EachLampFirst, TowardMirrors at a share of one, and
 * Lamps, SkyMap and GlowingSurfaces, with every member plugin's state empty
 * until the preparations fill it.
 */
Sampling defaultSampling();

/**
 * Return the chosen members with each member plugin's state among the
 * kept states, read through that plugin's accessor, which opens it
 * empty where no preparation filled it yet.
 *
 * @param chosen The chosen members.
 * @param states The kept states.
 */
Sampling preparedSampling(const Sampling& chosen, States& states);

}
