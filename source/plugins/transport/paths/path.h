#pragma once

#include "core/geometry.h"
#include "math/colour.h"
#include "math/spectrum.h"
#include "math/vector.h"
#include "plugins/transport/paths/feature.h"
#include "plugins/transport/paths/material/material.h"
#include "plugins/transport/paths/weight.h"

#include <array>
#include <cstdint>

namespace kodanuki {

/**
 * The most surfaces a light path joins or keeps: as many as an eye path
 * scatters from, nine.
 */
constexpr std::uint32_t mostLightVertices = 9;

/**
 * The lamp of a light walk or subpath that has none.
 */
constexpr std::uint32_t noLamp = 0xFFFFFFFFu;

/**
 * The role of one number a path draws.
 *
 * The pixel takes the first pair of a sample's coordinates and the lens
 * the second. Each event after takes eight, in this order from LightU
 * to Pick. The same coordinate then means the same thing across a
 * pixel's samples, and a sequence member can stratify it. The roles
 * from MixturePart to AimJitterV are plain: they lie far past the eight
 * an event, so a sequence never gives them. The roles from LightHue on,
 * which only paths from the lights draw, lie in a second plain block.
 */
enum class Draw : std::uint8_t {
	/**
	 * The offset across the pixel.
	 */
	PixelX,
	/**
	 * The offset down the pixel.
	 */
	PixelY,
	/**
	 * The first number of the point on the lens.
	 */
	LensU,
	/**
	 * The second number of the point on the lens.
	 */
	LensV,
	/**
	 * The first number of the point on the light an event aims at.
	 */
	LightU,
	/**
	 * The second number of that point.
	 */
	LightV,
	/**
	 * The first number of the direction a material draws.
	 */
	DirectionU,
	/**
	 * The second number of that direction.
	 */
	DirectionV,
	/**
	 * The number a material picks a lobe with.
	 */
	Lobe,
	/**
	 * The number glass picks reflection or passage with.
	 */
	Side,
	/**
	 * The number roulette decides with.
	 */
	Survival,
	/**
	 * The number light selection keeps a light with.
	 */
	Pick,
	/**
	 * The number a direction mixture picks its part with.
	 */
	MixturePart,
	/**
	 * The number the guide's part picks its lump with.
	 */
	MixtureLump,
	/**
	 * The number an aim picks a chart cell, a glow cell or the even
	 * share with.
	 */
	AimPart,
	/**
	 * The first jitter inside an aim's chart cell.
	 */
	AimJitterU,
	/**
	 * The second jitter inside an aim's chart cell.
	 */
	AimJitterV,
	/**
	 * The number a light pass's path draws its wavelengths with.
	 */
	LightHue,
	/**
	 * The first number of the point on the lens a light path joins a
	 * surface to.
	 */
	JoinLensU,
	/**
	 * The second number of that point.
	 */
	JoinLensV,
	/**
	 * The number a walk inside a scattering body draws the distance to
	 * its next collision with: hashed, in a block of its own.
	 */
	Collision,
};

/**
 * What a walk from a light is for.
 */
enum class LightUse : std::uint8_t {
	/**
	 * A path of the light pass. It ends at its first surface unless that
	 * surface is sharp. From its second surface on, at every surface that
	 * is not sharp and lies in the open, it asks the Strategy members for
	 * a line to the lens.
	 */
	Joining,
	/**
	 * The light subpath an eye walk leads with. It goes on past every
	 * surface and keeps each one it stands on, as long as there is room
	 * (Connect's subpath).
	 */
	Leading,
};

/**
 * Where a light walk is between two rays.
 */
enum class LightPhase : std::uint8_t {
	/**
	 * It waits on the ray it goes on along.
	 */
	Flying,
	/**
	 * It waits on its line to the lens.
	 */
	Joining,
	/**
	 * It has ended.
	 */
	Done,
};

/**
 * One number of a sample, named by its event and its role.
 */
struct Coordinate {
	/**
	 * The event: how many surfaces the path has scattered from.
	 */
	int event;
	/**
	 * The role.
	 */
	Draw draw;
};

/**
 * What names every number one path draws.
 */
struct PathNumbers {
	/**
	 * The stream: the frame's seed hashed with the pixel, or with the
	 * light path.
	 */
	std::uint64_t stream;
	/**
	 * The sample's place in its pixel's whole run.
	 */
	std::uint32_t index;
	/**
	 * The place of the sample's group in its pixel's run of groups: the
	 * samples that share a first vertex draw the pixel, the lens, the
	 * wavelengths and that vertex's lights by it. A sample alone, or a
	 * light path, holds its index.
	 */
	std::uint32_t group;
	/**
	 * How many samples share the first vertex, one for a sample alone.
	 */
	std::uint32_t branches;
	/**
	 * Whether a sequence member may give the coordinates: eye paths yes,
	 * light paths no.
	 */
	bool sequenced;
};

/**
 * Where an eye walk stands: a surface it met and how it answers light.
 */
struct Vertex {
	/**
	 * The point.
	 */
	math::FloatVector point;
	/**
	 * The unit geometric normal, facing the ray that met it.
	 */
	math::FloatVector surface;
	/**
	 * The surface's look, after features.
	 */
	Bsdf bsdf;
	/**
	 * What the surface gives off toward the ray that met it.
	 */
	FloatColour glow;
	/**
	 * How far off the surface a ray leaving it starts: the bias plus
	 * twice the hit's loosened tolerance.
	 */
	float lift;
	/**
	 * The scene leaf met.
	 */
	std::uint32_t leaf;
	/**
	 * The surface leaf of the tree the point lies in, the hit's cell.
	 */
	std::uint32_t cell;
	/**
	 * Whether the material is sharp.
	 */
	bool sharp;
	/**
	 * Whether the vertex is a collision inside a body rather than a
	 * point on a surface: it has no surface normal and no lift, and
	 * stands on the body's phase finish.
	 */
	bool collided;
};

/**
 * One path from the eye, held between its rays.
 *
 * Member plugins keep per-path state of their own beside it
 * (Companions).
 */
struct Path {
	/**
	 * The four wavelengths, drawn once.
	 */
	Wavelengths wavelengths;
	/**
	 * What a further contribution is worth, per lane.
	 */
	FloatSpectrum throughput;
	/**
	 * What the path has gathered, per lane.
	 */
	FloatSpectrum radiance;
	/**
	 * Whether the path was drawn exactly so far: true for a camera ray,
	 * kept through every exact surface, false after any other draw.
	 */
	bool exact;
	/**
	 * Whether the path keeps its hero alone past a bend that parted the
	 * wavelengths.
	 */
	bool dispersed;
	/**
	 * The vertex the path stands on.
	 */
	Vertex vertex;
	/**
	 * The unit direction of the ray in flight.
	 */
	math::FloatVector heading;
	/**
	 * Where the ray in flight was drawn: the lens point or the vertex's
	 * point.
	 */
	math::FloatVector drawnAt;
	/**
	 * Where the stretch of the ray in flight through its medium starts:
	 * where it was drawn, or where it last crossed a surface that bends
	 * nothing.
	 */
	math::FloatVector legFrom;
	/**
	 * How many surfaces that bend nothing the ray in flight has crossed.
	 */
	std::uint32_t crossings;
	/**
	 * The half angle the ray in flight was drawn to, zero for a camera
	 * ray.
	 */
	float sharpness;
	/**
	 * The solid-angle density the direction member drew the ray in
	 * flight with, zero for a camera ray or an exact draw.
	 */
	float density;
	/**
	 * How many vertices the path has scattered from, surfaces and
	 * collisions alike.
	 */
	int depth;
	/**
	 * How many collisions inside scattering bodies the path has stood
	 * on, the vertex it stands on included.
	 */
	int collisions;
	/**
	 * The glass the path travels inside, or noMaterial.
	 */
	std::uint32_t medium;
	/**
	 * What the MIS engine carries.
	 */
	Weighing weighing;
	/**
	 * What names its numbers.
	 */
	PathNumbers numbers;
	/**
	 * Whether the path left its first vertex as one branch of its group.
	 */
	bool branched;
};

/**
 * The most shapes a line passes before it counts as blocked.
 */
constexpr std::uint32_t mostCrossings = 8;

/**
 * A line from a point to a light, to be tested: a shadow line, a glance
 * at a glow cell, a line to the lens, or a connection.
 *
 * A line that hits a shape it passes is cast again past it, in place.
 */
struct Shadow {
	/**
	 * Where it starts, lifted off the surface.
	 */
	math::FloatVector origin;
	/**
	 * The unit direction.
	 */
	math::FloatVector direction;
	/**
	 * How far it must be clear, infinity for a distant light. For a
	 * glance, how far it looks: to where it leaves the cell's box.
	 */
	float reach;
	/**
	 * The glow cell a glance counts the first surface it meets in, or
	 * noCell for a line that must only be clear.
	 */
	std::uint32_t cell;
	/**
	 * The material of the solid it passes through, or noMaterial outside
	 * every solid.
	 */
	std::uint32_t within;
	/**
	 * The body whose medium it travels in, or noMaterial in the open.
	 */
	std::uint32_t medium;
	/**
	 * Which way it runs, which says the shapes it passes.
	 */
	Passage passage;
	/**
	 * How many shapes it has passed.
	 */
	std::uint32_t crossings;
};

/**
 * One path from a light, held between its rays.
 *
 * Its event is one more than the surfaces it has scattered from: event 0
 * is the launch (Pick, LightU/V, MixturePart, DirectionU/V), and the
 * draws at its k-th surface are event k.
 */
struct LightPath {
	/**
	 * The four wavelengths: the light pass draws its own, a leading walk
	 * takes the eye's.
	 */
	Wavelengths wavelengths;
	/**
	 * What the light carries, per lane: the emission over its densities,
	 * times every weight since.
	 */
	FloatSpectrum throughput;
	/**
	 * The largest lane of the throughput as it left the lamp, which
	 * roulette measures against.
	 */
	float start;
	/**
	 * Whether the hero alone carries on past a bend that parted the
	 * wavelengths.
	 */
	bool dispersed;
	/**
	 * The surface it stands on.
	 */
	Vertex vertex;
	/**
	 * The unit direction of the ray in flight.
	 */
	math::FloatVector heading;
	/**
	 * Where the ray in flight was drawn: the point on the lamp or the
	 * vertex's point.
	 */
	math::FloatVector drawnAt;
	/**
	 * Where the stretch of the ray in flight through its medium starts,
	 * as in Path.
	 */
	math::FloatVector legFrom;
	/**
	 * How many surfaces that bend nothing the ray in flight has crossed.
	 */
	std::uint32_t crossings;
	/**
	 * How many surfaces it has scattered from.
	 */
	int depth;
	/**
	 * The glass it travels inside, or noMaterial.
	 */
	std::uint32_t medium;
	/**
	 * The lamp it left.
	 */
	std::uint32_t lamp;
	/**
	 * Whether its first surface is sharp, so that join could make the
	 * paths it continues.
	 */
	bool relayed;
	/**
	 * What the rules read of the surfaces it met, in the light's order.
	 */
	RuleState rules;
	/**
	 * What the MIS engine carries.
	 */
	LightWeighing weighing;
	/**
	 * What names its numbers; never sequenced.
	 */
	PathNumbers numbers;
};

/**
 * What one light path brings one pixel.
 */
struct Splat {
	/**
	 * The pixel, by rows from the top left.
	 */
	std::uint32_t pixel;
	/**
	 * What it brings, weighed, as a colour.
	 */
	FloatColour colour;
};

/**
 * Where one light path lands on the film: its splats, in the order of
 * its surfaces.
 */
struct FilmLanding {
	/**
	 * The splats, the first count of them in use.
	 */
	std::array<Splat, mostLightVertices> splats;
	/**
	 * How many.
	 */
	std::uint32_t count;
};

/**
 * A surface a leading light walk stood on, kept so that the eye walk's
 * vertices can connect to it.
 */
struct LightVertex {
	/**
	 * The surface, shaded as an eye walk shades it.
	 */
	Vertex vertex;
	/**
	 * The unit direction the light arrived from, toward the previous
	 * vertex.
	 */
	math::FloatVector arrival;
	/**
	 * What the light brings the surface, per lane.
	 */
	FloatSpectrum throughput;
	/**
	 * The MIS engine's ratios at the surface, carried over the ray that
	 * met it.
	 */
	LightWeighing weighing;
	/**
	 * How many surfaces the walk met before this one.
	 */
	int depth;
	/**
	 * Whether the hero alone carries the light.
	 */
	bool dispersed;
	/**
	 * Whether a connection may end at it: it is not exact, and the light
	 * arrived through the open.
	 */
	bool connectable;
	/**
	 * Whether the walk's first surface is sharp.
	 */
	bool relayed;
	/**
	 * What the rules read of the walk's surfaces up to this one, in the
	 * light's order.
	 */
	RuleState rules;
};

/**
 * The surfaces a leading light walk kept, nearest the lamp first.
 */
struct LightSubpath {
	/**
	 * The surfaces, the first count of them in use.
	 */
	std::array<LightVertex, mostLightVertices> items;
	/**
	 * How many.
	 */
	std::uint32_t count;
	/**
	 * The lamp the walk left.
	 */
	std::uint32_t lamp;
};

/**
 * A line from a light path's surface to the lens, and what it brings if
 * nothing blocks it.
 */
struct LensLine {
	/**
	 * Whether there is a line: the surface is seen through the frame and
	 * brings anything.
	 */
	bool found;
	/**
	 * The line, lifted off the surface, reaching the lens.
	 */
	Shadow shadow;
	/**
	 * What it brings the pixel, per lane: the throughput, the response,
	 * the importance and the weight folded in.
	 */
	FloatSpectrum radiance;
	/**
	 * The pixel it lands in, by rows from the top left.
	 */
	std::uint32_t pixel;
};

/**
 * Where a light walk starts.
 */
struct LightStart {
	/**
	 * What it is for.
	 */
	LightUse use;
	/**
	 * What names its numbers.
	 */
	PathNumbers numbers;
	/**
	 * Its wavelengths.
	 */
	Wavelengths wavelengths;
};

/**
 * One walk from a light and everything it needs until its next ray is
 * answered.
 *
 * It is a pure function of its numbers and the answers to its rays, so
 * light walks may be answered in any interleaving.
 */
struct LightWalk {
	/**
	 * What it is for.
	 */
	LightUse use;
	/**
	 * The path.
	 */
	LightPath path;
	/**
	 * The ray it waits on while it is flying.
	 */
	Ray ray;
	/**
	 * The line to the lens it waits on while it is joining.
	 */
	LensLine line;
	/**
	 * What its clear lines to the lens brought, in order.
	 */
	FilmLanding landing;
	/**
	 * The surfaces a leading walk kept.
	 */
	LightSubpath kept;
	/**
	 * Where it is.
	 */
	LightPhase phase;
};

/**
 * Return the coordinate of a draw at an event.
 *
 * @param event The event.
 * @param draw The role.
 */
Coordinate coordinateOf(int event, Draw draw);

/**
 * Return a coordinate's place along its sample: the pixel and the lens
 * first, then eight numbers an event. The plain roles lie from 2^20 on,
 * five an event, and the light paths' own roles from 2^21 on, three an
 * event.
 *
 * @param coordinate The coordinate.
 */
std::uint64_t dimensionOf(const Coordinate& coordinate);

/**
 * Return whether the samples that share a first vertex share a
 * coordinate: the pixel, the lens and what the first vertex aims at
 * its lights with. They draw it by their group, every other one by
 * their own index.
 *
 * @param coordinate The event and the role.
 */
bool sharedDraw(const Coordinate& coordinate);

/**
 * Return the ray that leaves a vertex along a direction.
 *
 * It starts lifted to the side it leaves on, inside a solid where it
 * travels in a medium.
 *
 * @param vertex The vertex.
 * @param direction The unit direction.
 * @param medium The material it travels in, or noMaterial in the open.
 */
Ray leavingRay(const Vertex& vertex, const math::Vector& direction, std::uint32_t medium);

/**
 * Return a vertex on no surface: no material, at the origin, unlifted.
 */
Vertex blankVertex();

/**
 * Return a path that has met nothing yet.
 *
 * Its throughput is one at every lane, it holds no radiance, travels in
 * the open, and has not scattered.
 *
 * @param numbers What names its numbers.
 * @param wavelengths Its wavelengths.
 */
Path freshPath(const PathNumbers& numbers, const Wavelengths& wavelengths);

/**
 * Return how many surfaces and collisions a path standing on its vertex
 * has stood on, that vertex included.
 *
 * @param path The path.
 */
VertexCounts standingCounts(const Path& path);

}
