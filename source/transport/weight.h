#pragma once

#include <cstdint>

namespace kodanuki {

/**
 * Which lights a ray an eye walk drew could have been aimed toward.
 */
enum class Aimed : std::uint8_t {
	/**
	 * None. The ray was drawn exactly, or it leaves a surface on a side
	 * the vertex aims at nothing on.
	 */
	None,
	/**
	 * Every light. The ray leaves a surface on the side its vertex aims
	 * into.
	 */
	Every,
	/**
	 * None, and the ray is a camera ray, which meets the sky alone.
	 */
	Camera,
};

/**
 * How many paths of each kind one sample of a pixel makes.
 *
 * A strategy's density for a path is its count times what it drew the
 * path with. A strategy that is off counts zero.
 */
struct StrategyCounts {
	/**
	 * Aims per vertex, one or zero.
	 */
	double aim;
	/**
	 * Light paths joined to the lens per sample of each pixel: the light
	 * share.
	 */
	double join;
	/**
	 * Light subpaths connected per eye path, one or zero.
	 */
	double connect;
};

/**
 * How many vertices of each kind a path stands on, the last included.
 */
struct VertexCounts {
	/**
	 * The surfaces.
	 */
	int surfaces;
	/**
	 * The collisions inside scattering bodies.
	 */
	int collisions;
};

/**
 * The most vertices of each kind an eye walk stands on.
 *
 * A walk scatters on from a vertex only while it has stood on fewer
 * surfaces and fewer collisions than these.
 */
struct DepthCaps {
	/**
	 * The most surfaces.
	 */
	int surfaces;
	/**
	 * The most collisions.
	 */
	int collisions;
};

/**
 * The density every strategy makes one path with, in one measure.
 *
 * Every field is relative to the same part of the path, which the
 * strategies share, so only their ratios mean anything. Connections come
 * as one sum of squares, since the power heuristic needs no more. A
 * strategy that cannot make the path has density zero.
 */
struct Densities {
	/**
	 * Meeting the light from the eye walk.
	 */
	double meet;
	/**
	 * Aiming at the light from the eye walk's last vertex.
	 */
	double aim;
	/**
	 * A light path joined to the lens.
	 */
	double join;
	/**
	 * The squares of every connection between a light subpath of two or
	 * more vertices and an eye subpath of two or more, summed.
	 */
	double connections;
};

/**
 * What an eye walk carries so that every strategy's density for the
 * path it ends can be written down at its end.
 *
 * It is the recursive form of the power heuristic over the strategies
 * of s vertices from the light and t from the eye. The fields hold
 * ratios of area densities, each kept over the solid-angle density of
 * the newest edge where that edge is not yet known at both ends, so
 * nothing is stored per vertex.
 */
struct Weighing {
	/**
	 * The density join makes the walk so far with, its count included,
	 * over the density the walk made it with; zero where join cannot make
	 * it.
	 */
	double join;
	/**
	 * The squared density ratios of the connections cutting the edges
	 * before the newest one, summed.
	 */
	double connections;
	/**
	 * One over the squared density the walk made its newest vertex with,
	 * the connection cutting the edge that reaches it.
	 */
	double cut;
	/**
	 * The solid-angle density the ray in flight is weighed with, as
	 * ScatterFactors::forward gives it, zero for a camera ray or an
	 * exact draw.
	 */
	double drawn;
	/**
	 * Whether the vertex that drew the ray in flight is sharp, which join
	 * needs of the vertex next to the light.
	 */
	bool relays;
	/**
	 * Which lights the ray in flight could have been aimed toward.
	 */
	Aimed aimed;
};

/**
 * What the eye walk's weighing reads where its ray meets a surface.
 */
struct HitFactors {
	/**
	 * The absolute cosine at the surface toward the ray.
	 */
	double cosine;
	/**
	 * How far the ray went from where it was drawn.
	 */
	double distance;
	/**
	 * Whether join may join the surface to the lens: not sharp and in the
	 * open. Only a camera ray's surface reads it.
	 */
	bool joinable;
	/**
	 * Whether a connection may end at the surface: it is not exact, and
	 * the ray met it through the open. The cut is zeroed where it is not.
	 */
	bool connectable;
};

/**
 * What a walk's weighing reads where it draws a direction.
 */
struct ScatterFactors {
	/**
	 * The density the walk weighs its draw with: the density it drew the
	 * direction with, or, for an eye walk past its first vertex where
	 * light paths count, the bare material's, which a walk from a light
	 * can compute too.
	 */
	double forward;
	/**
	 * The density the other end draws the other way with: for an eye
	 * walk the bare material from the drawn direction back along the ray
	 * met, for a walk from a light the eye's direction draw.
	 */
	double reverse;
	/**
	 * The absolute cosine at the surface toward the drawn direction.
	 */
	double cosine;
	/**
	 * Whether the draw is exact, so no density describes it.
	 */
	bool exact;
	/**
	 * Whether the vertex is sharp.
	 */
	bool sharp;
	/**
	 * Whether a connection may end at the vertex: the draw is not exact,
	 * reflects, and the ray met the vertex through the open. One that may
	 * not adds no cut and leaves none, as an exact draw.
	 */
	bool connectable;
	/**
	 * Which lights the drawn ray could be aimed toward.
	 */
	Aimed aimed;
};

/**
 * A light met along the eye walk's ray in flight.
 */
struct MeetEnding {
	/**
	 * The walk's weighing.
	 */
	const Weighing& weighing;
	/**
	 * The solid-angle density aiming from the vertex draws the direction
	 * with, zero where the ray was not aimable.
	 */
	double aim;
	/**
	 * The density a path from the light starts with toward the vertex:
	 * its lamp's share times the area density of the point times the
	 * solid-angle density of the direction, over the cosine at the light;
	 * zero for a light that starts no paths.
	 */
	double emission;
	/**
	 * How many paths of each kind a sample makes.
	 */
	const StrategyCounts& counts;
};

/**
 * A light aimed at from the eye walk's vertex.
 */
struct AimEnding {
	/**
	 * The walk's weighing at the vertex.
	 */
	const Weighing& weighing;
	/**
	 * The density the eye's draw at the vertex is weighed with for the
	 * aimed direction: the direction sampler's, or, past the first
	 * vertex where light paths count, the bare material's.
	 */
	double forward;
	/**
	 * The density the bare material draws back toward the ray that met
	 * the vertex, for light arriving along the aimed direction.
	 */
	double reverse;
	/**
	 * The solid-angle density of the aim, zero for a light with no
	 * extent.
	 */
	double aim;
	/**
	 * The density a path from the light starts with toward the vertex,
	 * as in MeetEnding; for a point light over the squared distance
	 * rather than the cosine.
	 */
	double emission;
	/**
	 * The absolute cosine at the vertex toward the light.
	 */
	double cosine;
	/**
	 * Whether the vertex is sharp.
	 */
	bool sharp;
	/**
	 * Whether the path is at a depth cap (atDepthCap), so meet, which
	 * would have to scatter past its last vertex, cannot make it.
	 */
	bool capped;
	/**
	 * How many paths of each kind a sample makes.
	 */
	const StrategyCounts& counts;
};

/**
 * What a walk from a light carries for the same purpose.
 *
 * It mirrors Weighing with the ends swapped: each field is a density of
 * an eye strategy over the density the light walk made the path with.
 * Join and the connections ask it, and the core tests it against a
 * brute-force sum.
 */
struct LightWeighing {
	/**
	 * The density meeting the light from an eye walk makes the walk so
	 * far with, relative to the light walk; zero for a light with no
	 * extent.
	 */
	double meet;
	/**
	 * The density aiming at the light makes it with, its count included,
	 * relative to the light walk.
	 */
	double aim;
	/**
	 * The squared density ratios of the connections cutting the edges
	 * before the newest one, summed.
	 */
	double connections;
	/**
	 * One over the squared density the light walk made its newest vertex
	 * with.
	 */
	double cut;
	/**
	 * Whether the walk has not yet scattered: its first surface is the
	 * one aiming would aim at the light from.
	 */
	bool fresh;
};

/**
 * Where a walk from a light starts.
 */
struct LampStart {
	/**
	 * The share of the lamp times the area density of the point times the
	 * solid-angle density of the direction it leaves in.
	 */
	double emission;
	/**
	 * The absolute cosine at the light toward the direction, one for a
	 * point light.
	 */
	double cosine;
	/**
	 * Whether the light has no extent, so no eye walk meets it.
	 */
	bool singular;
};

/**
 * What a walk from a light reads where its ray meets a surface.
 */
struct LightHitFactors {
	/**
	 * The absolute cosine at the surface toward the ray.
	 */
	double cosine;
	/**
	 * How far the ray went.
	 */
	double distance;
	/**
	 * At the first surface, what aiming at the light from it weighs: its
	 * solid-angle density, the squared distance for a point light, or
	 * zero where the surface aims at nothing.
	 */
	double aim;
	/**
	 * Whether a connection may end at the surface, as in HitFactors.
	 */
	bool connectable;
};

/**
 * A walk from a light joined to the lens at its surface.
 */
struct JoinEnding {
	/**
	 * The light walk's weighing at the surface.
	 */
	const LightWeighing& weighing;
	/**
	 * The density the eye's direction sampler draws, at the surface, the
	 * direction the light came from for a ray from the lens.
	 */
	double forward;
	/**
	 * The area density a camera ray finds the surface with: the lens
	 * density times the cosine at the surface over the squared distance.
	 */
	double lens;
	/**
	 * Whether the path is at a depth cap (atDepthCap), so meet, which
	 * would have to scatter past its last vertex, cannot make it.
	 */
	bool capped;
	/**
	 * How many paths of each kind a sample makes.
	 */
	const StrategyCounts& counts;
};

/**
 * An eye walk's vertex connected to a light walk's kept surface.
 *
 * The densities are as the strategies draw them: solid angle at the
 * vertex that draws, before any conversion to area.
 */
struct ConnectEnding {
	/**
	 * The eye walk's weighing at its vertex.
	 */
	const Weighing& eye;
	/**
	 * The light walk's weighing at its surface.
	 */
	const LightWeighing& light;
	/**
	 * The density the eye's draw at its vertex is weighed with toward the
	 * light surface.
	 */
	double eyeForward;
	/**
	 * The density the bare material at the eye's vertex draws back toward
	 * the ray that met it, for light arriving from the light surface.
	 */
	double eyeReverse;
	/**
	 * The density the light walk's draw at its surface draws the direction
	 * toward the eye's vertex with.
	 */
	double lightForward;
	/**
	 * The density the eye's draw at the light surface is weighed with
	 * back along the light's arrival, for a ray from the eye's vertex.
	 */
	double lightReverse;
	/**
	 * The absolute cosine at the eye's vertex toward the light surface.
	 */
	double eyeCosine;
	/**
	 * The absolute cosine at the light surface toward the eye's vertex.
	 */
	double lightCosine;
	/**
	 * How far apart the two are.
	 */
	double distance;
	/**
	 * Whether the light walk's first surface is sharp, so that join could
	 * make the path.
	 */
	bool relayed;
	/**
	 * Whether the path is at a depth cap (atDepthCap), so meet, which
	 * would have to scatter past its last vertex, cannot make it.
	 */
	bool capped;
	/**
	 * How many paths of each kind a sample makes.
	 */
	const StrategyCounts& counts;
};

/**
 * Return the share of a path one strategy keeps.
 *
 * This is the power heuristic with exponent two: the own density
 * squared over the sum of the squares, connections included. A path no
 * strategy has a density for gives zero. An infinite density keeps the
 * whole where it is the only one.
 *
 * @param densities Every strategy's density.
 * @param own The density of the strategy asking, one of the fields.
 */
double weightOf(const Densities& densities, double own);

/**
 * Return whether a path standing on these vertices is at a cap, so the
 * walk ends there and meet, which would have to scatter past the last
 * vertex, cannot make the path.
 *
 * It is at a cap where it has stood on as many surfaces as the caps
 * allow, or collided as many times.
 *
 * @param counts The vertices the path stands on.
 * @param caps The caps.
 */
bool atDepthCap(const VertexCounts& counts, const DepthCaps& caps);

/**
 * Return the weighing a camera ray starts with.
 *
 * Join's ratio starts at its count over the lens density.
 *
 * @param counts The counts.
 * @param lensDensity The density the camera ray was drawn with.
 */
Weighing weighingAtLens(const StrategyCounts& counts, double lensDensity);

/**
 * Carry a weighing over the ray that just met a surface.
 *
 * Join's ratio is divided by the cosine, and for a camera ray it is
 * multiplied by the squared distance too, or zeroed where the surface is
 * not joinable. The cut is multiplied by the distance to the fourth over
 * the squared cosine, or zeroed where the surface is not connectable,
 * and the connections are divided by the squared cosine.
 *
 * @param weighing The weighing.
 * @param factors The cosine, the distance and the surface.
 */
Weighing weighingAtHit(const Weighing& weighing, const HitFactors& factors);

/**
 * Carry a weighing past a drawn direction.
 *
 * Join's ratio is multiplied by the reverse density times the cosine
 * over the forward density, the reverse density counting one at the
 * first surface. The connections become the squared cosine over the
 * squared forward density, times the connections times the squared
 * reverse density plus the cut. The cut becomes one over the squared
 * forward density. An exact draw counts both densities as one, adds no
 * cut, and leaves no cut; a draw that is not connectable adds no cut and
 * leaves none either.
 *
 * @param weighing The weighing.
 * @param factors The densities, the cosine and the vertex.
 */
Weighing weighingAfterScatter(const Weighing& weighing, const ScatterFactors& factors);

/**
 * Return every strategy's density for a path the eye walk ended by
 * meeting a light.
 *
 * Meet is the drawn density, one for an exact draw. Aim is the aim
 * density times its count. Join is its ratio times the drawn density
 * times the emission, where the drawing vertex relays. The connections
 * are the squared emission times the squared drawn density times the
 * connections, times the squared count.
 *
 * @param ending The weighing, the light end and the counts.
 */
Densities meetDensities(const MeetEnding& ending);

/**
 * Return every strategy's density for a path the eye walk ended by
 * aiming at a light.
 *
 * Meet is the forward density, zero for a light with no extent and at
 * the depth cap. Aim is the aim density, one for a light with no
 * extent, times its count.
 * Join is its ratio times the reverse density, the cosine and the
 * emission, where the vertex is sharp. The connections are the emission
 * times the cosine, squared, times the cut plus the connections times
 * the squared reverse density, times the squared count.
 *
 * @param ending The weighing, the light end and the counts.
 */
Densities aimDensities(const AimEnding& ending);

/**
 * Return whether meetDensities() reads the emission density: where join
 * counts at a vertex that relays, or the connections count. Elsewhere
 * the emission may be left zero, and the densities are the same.
 *
 * @param weighing The path's weighing.
 * @param counts How many paths each strategy makes a sample.
 */
bool meetReadsEmission(const Weighing& weighing, const StrategyCounts& counts);

/**
 * Return whether aimDensities() reads the emission density: where join
 * counts at a sharp vertex, or the connections count. Elsewhere the
 * emission may be left zero, and the densities are the same.
 *
 * @param weighing The path's weighing.
 * @param counts How many paths each strategy makes a sample.
 * @param sharp Whether the vertex aimed from is sharp.
 */
bool aimReadsEmission(const Weighing& weighing, const StrategyCounts& counts, bool sharp);

/**
 * Return the weighing a walk from a light starts with.
 *
 * Meet's ratio starts at the cosine over the emission, zero for a light
 * with no extent. Aim's starts at its count over the emission, times the
 * cosine for a light with an extent.
 *
 * @param start The emission, the cosine and the light.
 * @param counts The counts.
 */
LightWeighing weighingAtLamp(const LampStart& start, const StrategyCounts& counts);

/**
 * Carry a light walk's weighing over the ray that just met a surface.
 *
 * Meet's and aim's ratios are divided by the cosine, and at the first
 * surface aim's is multiplied by what aiming from it weighs. The cut and
 * the connections change as for the eye walk.
 *
 * @param weighing The weighing.
 * @param factors The cosine, the distance and the aim.
 */
LightWeighing lightWeighingAtHit(const LightWeighing& weighing, const LightHitFactors& factors);

/**
 * Carry a light walk's weighing past a drawn direction.
 *
 * Meet's ratio is multiplied by the reverse density times the cosine
 * over the forward density, and so is aim's, without the reverse density
 * at the first surface, where aiming replaces the eye's draw. The cut
 * and the connections change as for the eye walk.
 *
 * @param weighing The weighing.
 * @param factors The light walk's draw, the eye's density the other way
 *        and the cosine.
 */
LightWeighing lightWeighingAfterScatter(const LightWeighing& weighing, const ScatterFactors& factors);

/**
 * Return every strategy's density for a path made by connecting an eye
 * walk's vertex to a light walk's surface.
 *
 * Every field is relative to the connection's own density, its count,
 * the density the caller asks weightOf() with. Each side's ratios are
 * carried over the connecting edge as if the other side had drawn it:
 * - The light walk's meet, aim and connections are carried with the
 *   eye's forward density, the light's cosine and the squared distance,
 *   then past the light surface with the light's reverse density; aim's
 *   without it where the light surface is the light walk's first.
 * - The eye walk's join and connections are carried with the light's
 *   forward density, the eye's cosine and the squared distance, then
 *   past the eye's vertex with the eye's reverse density, which counts
 *   one at the eye's first surface for join; join only where the light
 *   walk's first surface is sharp.
 * - The two cuts give the connections that cut the edges next to the
 *   connecting one.
 * - Meet is zero at the depth cap.
 *
 * The connections sum includes the connection's own square.
 *
 * @param ending Both weighings, the four densities, the geometry and the
 *        counts.
 */
Densities connectDensities(const ConnectEnding& ending);

/**
 * Return every strategy's density for a path a light walk ended by
 * joining its surface to the lens.
 *
 * Join is its count. Meet is its ratio times the eye's density at the
 * surface times the lens density, zero at the depth cap, and aim
 * likewise, without the eye's
 * density where the surface is the light walk's first. The connections
 * are the squared lens density times the cut plus the connections times
 * the squared eye density, times the squared count.
 *
 * @param ending The weighing, the eye's density, the lens and the
 *        counts.
 */
Densities joinDensities(const JoinEnding& ending);

}
