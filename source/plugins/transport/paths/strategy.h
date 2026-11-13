#pragma once

#include "core/geometry.h"
#include "plugins/transport/paths/camera.h"
#include "plugins/transport/paths/feature.h"
#include "plugins/transport/paths/light/light.h"
#include "plugins/transport/paths/material/material.h"
#include "plugins/transport/paths/path.h"
#include "plugins/transport/paths/sampler.h"
#include "plugins/transport/paths/weight.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The most lines a vertex asks to test: aim's own lines and its shared
 * one, and one connection per kept light surface.
 */
constexpr std::uint32_t mostLines = 16;

/**
 * A way of making a whole path from the lens to a light.
 *
 * Each member makes paths and reports its density for every path. The
 * MIS engine weighs every member that is on against the others, so the
 * mean never depends on which are on. Meet is always on.
 */
enum class StrategyMember : std::uint8_t {
	/**
	 * The eye walk meets a light along a ray it drew, or the glow of the
	 * surface it stands on.
	 */
	Meet,
	/**
	 * The eye walk aims at a light from its vertex.
	 */
	Aim,
	/**
	 * Paths from the lights, traced once per frame before any tile, that
	 * pass at least one sharp surface and are joined to the lens at every
	 * dull surface they reach in the open (plugins/strategy/join). What they bring
	 * a pixel is the frame's prepared light.
	 */
	Join,
	/**
	 * Every vertex of the eye walk is connected to every surface of the
	 * light subpath the walk led with, each connection a line tested like
	 * an aim's (plugins/strategy/connect).
	 */
	Connect,
};

/**
 * Where in the eye walk a strategy is asked to act.
 */
enum class StrategyMoment : std::uint8_t {
	/**
	 * The ray in flight has stopped. Meet meets the lights along it.
	 */
	Arrival,
	/**
	 * The walk stands on a vertex. Meet takes its glow and Aim aims from
	 * it.
	 */
	Vertex,
};

/**
 * Every Strategy member that is on, and the state the light paths keep.
 */
struct Strategies {
	/**
	 * The members.
	 */
	std::vector<StrategyMember> members;
	/**
	 * Light paths traced per sample of each pixel, the count join's
	 * density is weighed with: 0.125 where the light pass traces, zero
	 * where it traces none (no sharp surface may relay a path, or no lamp
	 * starts paths).
	 */
	double joinShare;
	/**
	 * The share of the light paths each lamp starts, by power. It is
	 * empty where no member starts paths from the lights.
	 */
	std::vector<double> lampShares;
};

/**
 * How deep a path goes, and how far off a surface its rays start.
 */
struct TransportLimits {
	/**
	 * The depth roulette starts at.
	 */
	int minimumDepth;
	/**
	 * The most surfaces an eye walk stands on: one more than the most it
	 * scatters from.
	 */
	int maximumDepth;
	/**
	 * The most collisions inside scattering bodies an eye walk stands
	 * on, counted apart from its surfaces.
	 */
	int maximumCollisions;
	/**
	 * The least lift off a surface.
	 */
	double bias;
};

/**
 * What the transport reads for a frame.
 */
struct Tracing {
	/**
	 * The tree and the scene.
	 */
	const Geometry& geometry;
	/**
	 * The scene's geometry.
	 */
	const Scene& scene;
	/**
	 * The scene's looks: its materials and coats.
	 */
	const Looks& looks;
	/**
	 * The finishes.
	 */
	const Materials& materials;
	/**
	 * The lights.
	 */
	const Lights& lights;
	/**
	 * The lens.
	 */
	const Lens& lens;
	/**
	 * The Sampler members.
	 */
	const Sampling& sampling;
	/**
	 * The Strategy members.
	 */
	const Strategies& strategies;
	/**
	 * The Feature members.
	 */
	const Features& features;
	/**
	 * How deep a path goes and the least lift.
	 */
	TransportLimits limits;
	/**
	 * The frame's seed.
	 */
	std::uint64_t seed;
	/**
	 * How the drawn pixels cover the shown ones: stillFilm() for a frame
	 * drawn at the shown size.
	 */
	FilmPlan film;
	/**
	 * Whether each walk keeps what its camera ray met first
	 * (Walk.first): where a chosen Reconstruction member reads the
	 * frame's record (readsRecords()).
	 */
	bool recording;
};

/**
 * A line to a light and what it brings if nothing blocks it.
 */
struct Line {
	/**
	 * The line.
	 */
	Shadow shadow;
	/**
	 * What it adds to the path's radiance where it is clear, weight and
	 * scale folded in.
	 */
	FloatSpectrum radiance;
};

/**
 * The lines a vertex asks to test, in the order their radiance is added.
 */
struct Lines {
	/**
	 * The lines, the first count of them in use.
	 */
	std::array<Line, mostLines> items;
	/**
	 * How many.
	 */
	std::uint32_t count;
};

/**
 * The per-path state of the member plugins, carried beside the path.
 */
struct Companions {
	/**
	 * The guide HighlightGuide draws from.
	 */
	GuideState guide;
	/**
	 * What the path has met that the rules read.
	 */
	RuleState rules;
};

/**
 * Where a strategy is asked to act, and the path and lines it updates
 * in place (rule 46).
 */
struct StrategyVisit {
	/**
	 * The moment.
	 */
	StrategyMoment moment;
	/**
	 * The path, which a member adds what it gathers to.
	 */
	Path& path;
	/**
	 * The member plugins' state beside the path.
	 */
	const Companions& companions;
	/**
	 * The light subpath the walk led with, empty where it led with none.
	 */
	const LightSubpath& lead;
	/**
	 * The hit the ray in flight stopped at, or a miss.
	 */
	const Hit& hit;
	/**
	 * The lines asked for at this moment, which a member adds its own
	 * to, after the earlier members'.
	 */
	Lines& lines;
};

/**
 * A light path standing on a surface, asked for a line to the lens.
 */
struct LightStanding {
	/**
	 * The light path, standing on its surface.
	 */
	const LightPath& path;
	/**
	 * The registers the scene's programs run in.
	 */
	std::vector<double>& registers;
};

/**
 * Act as one member at one moment: the point's step dispatch.
 *
 * A member never traces a ray. What it gathers without a ray it adds
 * to the visit's path at once. What needs a clear line it adds to the
 * visit's lines, at the Vertex moment only; the walk tests them and
 * adds their radiance in order once every member has acted. A member
 * with nothing to do at the moment leaves both as they are.
 *
 * @param member The member.
 * @param tracing What the transport reads.
 * @param visit The moment, the path, the hit and the lines.
 */
void strategyStep(StrategyMember member, const Tracing& tracing, const StrategyVisit& visit);

/**
 * Return the line a member joins a light path's surface to the lens
 * with: the point's dispatch for a light walk.
 *
 * Join answers by joinedLine(). Every other member, and Join where the
 * surface is not seen, answers no line.
 *
 * @param member The member.
 * @param tracing What the transport reads.
 * @param standing The light path on its surface and the registers.
 */
LensLine lensLineFrom(StrategyMember member, const Tracing& tracing, const LightStanding& standing);

/**
 * Return a line that brings nothing.
 */
LensLine noLensLine();

/**
 * Add a line to the end of a vertex's lines, in place.
 *
 * A vertex asks for at most mostLines lines; one more is an error.
 *
 * @param lines The lines.
 * @param line The line.
 */
void addLine(Lines& lines, const Line& line);

/**
 * Return a set of no lines.
 */
Lines noLines();

/**
 * Return how many paths of each kind a sample makes.
 *
 * Aim counts one where it is on. Join counts the join share where it is
 * on. Connect counts one where it is on and some lamp starts paths.
 *
 * @param strategies The members.
 */
StrategyCounts countsOf(const Strategies& strategies);

/**
 * Return the counts a path's weighing reads for the aims of one of its
 * vertices: countsOf() the tracing's strategies, and sharedAim() of
 * them at the first vertex, which a group of samples shares.
 *
 * @param tracing What the transport reads.
 * @param path The path.
 * @param vertex How many vertices the path scattered from before the
 * one that aims.
 */
StrategyCounts aimCounts(const Tracing& tracing, const Path& path, int vertex);

/**
 * Return whether eye walks lead with a light subpath: Connect is on and
 * some lamp has a share.
 *
 * @param strategies The members.
 */
bool strategyLeads(const Strategies& strategies);

/**
 * Return whether a member is on.
 *
 * @param strategies The members.
 * @param member The member.
 */
bool strategyOn(const Strategies& strategies, StrategyMember member);

/**
 * Return the core's strategies: meet and aim, with a join share of
 * 0.125 for when join is on.
 */
Strategies coreStrategies();

/**
 * Return the chosen members with the lamp shares and the join share a
 * frame traces with (withLightShares()), from the lights and the mirror
 * balls the preparations left.
 *
 * @param chosen The chosen members.
 * @param lights The lights.
 * @param sampling The Sampler members and their states.
 */
Strategies preparedStrategies(const Strategies& chosen, const Lights& lights, const Sampling& sampling);

/**
 * Return the core's limits for a frame: roulette from depth 4, the
 * render's most surfaces and most collisions, and a lift of 4 mm.
 *
 * @param render The render settings.
 */
TransportLimits transportLimits(const Render& render);

/**
 * Return the caps an eye walk stands under.
 *
 * @param limits The limits.
 */
DepthCaps depthCaps(const TransportLimits& limits);

}
