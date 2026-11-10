#pragma once

#include "transport/strategy.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What a ray is asked, which is also how it is traced.
 */
enum class RayKind : std::uint8_t {
	/**
	 * A ray from the lens: its hit is wanted.
	 */
	Camera,
	/**
	 * A ray drawn from a vertex in the open, or a glance at a glow
	 * cell: its hit is wanted.
	 */
	Scatter,
	/**
	 * A ray drawn from a vertex inside a solid, which passes filled
	 * leaves: its hit is wanted.
	 */
	Inside,
	/**
	 * A line to a light that must only be clear: only whether it met
	 * anything is wanted.
	 */
	Shadow,
};

/**
 * Where an eye walk is between two rays.
 */
enum class WalkPhase : std::uint8_t {
	/**
	 * It waits on a ray of the light subpath it leads with.
	 */
	Leading,
	/**
	 * It waits on the ray it goes on along.
	 */
	Flying,
	/**
	 * It waits on the next of its vertex's lines.
	 */
	Lighting,
	/**
	 * It stands lit on the first vertex its group shares and waits for
	 * branchedFrom() to take each sample of the group on from there.
	 */
	Branching,
	/**
	 * It has ended.
	 */
	Done,
};

/**
 * How many kinds of ray there are.
 */
constexpr int rayKindCount = 4;

/**
 * One sample of one pixel.
 */
struct PixelSample {
	/**
	 * The pixel's column.
	 */
	int x;
	/**
	 * The pixel's row.
	 */
	int y;
	/**
	 * The sample's place in the pixel's whole run.
	 */
	std::uint32_t index;
	/**
	 * The place of its group in the pixel's run of groups, its index
	 * for a sample alone.
	 */
	std::uint32_t group;
	/**
	 * How many samples share its first vertex, one for a sample alone.
	 */
	std::uint32_t branches;
};

/**
 * What a sample's camera ray met first: the surface or collision its
 * walk first stood on, which the frames of a sequence keep per pixel.
 */
struct Surface {
	/**
	 * The distance from the lens to it; infinite where the camera ray met
	 * nothing.
	 */
	float distance;
	/**
	 * Its unit shading normal, facing the camera ray; zero on a miss.
	 */
	math::Vector normal;
	/**
	 * The scene leaf, noLeaf on a miss.
	 */
	std::uint32_t leaf;
	/**
	 * Its finish, an index into Materials::finishes; noMaterial on a
	 * miss.
	 */
	std::uint32_t material;
	/**
	 * Its albedo at the point; zero on a miss.
	 */
	Colour albedo;
	/**
	 * Its finish's roughness, the square root of the finish's GGX alpha:
	 * 1 for a matte finish and on a miss.
	 */
	float roughness;
	/**
	 * The length of the walk's second leg, from the first vertex to what
	 * its scattered ray met; infinite where that ray met nothing or the
	 * walk ended on its first vertex.
	 */
	float onward;
};

/**
 * What one sample's light comes to.
 */
struct Parts {
	/**
	 * All of it: the colour the pixel's estimate folds.
	 */
	Colour total;
};

/**
 * What one sample leaves for the frames of a sequence.
 */
struct Recorded {
	/**
	 * Its light.
	 */
	Parts parts;
	/**
	 * What its camera ray met first.
	 */
	Surface surface;
};

/**
 * What one eye path brought back.
 */
struct Traced {
	/**
	 * Its wavelengths.
	 */
	Wavelengths wavelengths;
	/**
	 * What it gathered, per lane.
	 */
	Spectrum4 radiance;
};

/**
 * A path whose ray in flight stopped, updated in place (rule 46).
 */
struct Arrival {
	/**
	 * The path.
	 */
	Path& path;
	/**
	 * Where its ray stopped, or a miss.
	 */
	const Hit& hit;
};

/**
 * A path and the ray it goes on along, or its end.
 */
struct Onward {
	/**
	 * Whether the path goes on.
	 */
	bool alive;
	/**
	 * The path, with all it gathered.
	 */
	Path path;
	/**
	 * The ray it goes on along.
	 */
	Ray ray;
};

/**
 * One light's offer to an eye walk's vertex.
 */
struct Offer {
	/**
	 * What it brings if nothing stands in the way, throughput and
	 * weight folded in. For a glance it holds the cell's guess in place
	 * of the glow the glance meets.
	 */
	Spectrum4 radiance;
	/**
	 * The line to test.
	 */
	Shadow shadow;
};

/**
 * A line and what its ray met.
 */
struct LineHit {
	/**
	 * The line.
	 */
	const Shadow& shadow;
	/**
	 * What its ray met, or a miss.
	 */
	const Hit& hit;
	/**
	 * The wavelengths of the path it lights.
	 */
	const Wavelengths& wavelengths;
	/**
	 * The registers the patterns run in.
	 */
	std::vector<double>& registers;
};

/**
 * What a line does at a hit.
 */
struct LineStep {
	/**
	 * Whether it goes on past the hit.
	 */
	bool onward;
	/**
	 * The line cast again past the hit, where it goes on.
	 */
	Shadow shadow;
	/**
	 * What it kept of the light along the stretch to the hit, per lane.
	 */
	Spectrum4 kept;
};

/**
 * A glance at a glow cell and the first surface it met.
 */
struct Glance {
	/**
	 * The path that aimed it, standing on its vertex.
	 */
	const Path& path;
	/**
	 * What the path has met that the rules read.
	 */
	const RuleState& rules;
	/**
	 * The line, its cell the cell aimed at.
	 */
	const Shadow& shadow;
	/**
	 * The first surface along it within its reach, or a miss.
	 */
	const Hit& hit;
	/**
	 * The registers the surface's patterns run in.
	 */
	std::vector<double>& registers;
};

/**
 * A direction from a vertex and the material's own density of it.
 */
struct OwnDensity {
	/**
	 * The unit direction.
	 */
	math::Vector incoming;
	/**
	 * bsdfDensity() from the path's outgoing direction toward incoming.
	 */
	double density;
};

/**
 * One light offered from a vertex, with the numbers every light of the
 * vertex is aimed with.
 */
struct LightAsked {
	/**
	 * The light's number.
	 */
	std::uint32_t light;
	/**
	 * The vertex's LightU and LightV numbers, drawn once for all its
	 * lights.
	 */
	math::UnitPair pair;
	/**
	 * The vertex's surface seen from the path, worked out once for all
	 * its lights.
	 */
	const ViewedBsdf& viewed;
	/**
	 * The direction aimed toward the light, once aimed; zero before.
	 */
	math::Vector incoming;
};

/**
 * A path standing on its vertex and the extras' state beside it.
 */
struct Standing {
	/**
	 * The path.
	 */
	const Path& path;
	/**
	 * The extras' state beside it.
	 */
	const Companions& companions;
	/**
	 * The strategies' counts, countsOf() the tracing's strategies.
	 */
	StrategyCounts counts;
};

/**
 * A surface a ray met, to stand a walk on.
 */
struct Surfacing {
	/**
	 * Where the ray met the surface.
	 */
	const Hit& hit;
	/**
	 * The unit direction the ray travelled.
	 */
	math::Vector heading;
	/**
	 * The glass the ray travelled inside, or noMaterial.
	 */
	std::uint32_t medium;
	/**
	 * The walk's wavelengths.
	 */
	const Wavelengths& wavelengths;
	/**
	 * The registers the patterns run in.
	 */
	std::vector<double>& registers;
};

/**
 * A stretch of a ray through the glass it travels inside.
 */
struct MediumLeg {
	/**
	 * The glass, or noMaterial for the open.
	 */
	std::uint32_t medium;
	/**
	 * Where the stretch starts.
	 */
	math::Vector origin;
	/**
	 * The unit direction.
	 */
	math::Vector heading;
	/**
	 * How long it is.
	 */
	double distance;
	/**
	 * The walk's wavelengths.
	 */
	const Wavelengths& wavelengths;
	/**
	 * Whether the ray may collide: an eye walk's ray keeps its
	 * absorption alone, a light walk's ray or a line keeps the light the
	 * body turns aside too.
	 */
	MediumUse use;
	/**
	 * The number a walking ray's collision is drawn with.
	 */
	double number;
};

/**
 * A direction drawn at a vertex, whose density the other end would draw
 * the other way with.
 */
struct TurnBack {
	/**
	 * The surface, shaded for the walk that stands on it.
	 */
	const Bsdf& bsdf;
	/**
	 * The unit direction of the ray that met the vertex.
	 */
	math::Vector heading;
	/**
	 * The unit direction drawn.
	 */
	math::Vector drawn;
	/**
	 * The walk's wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * One eye path and everything it needs until its next ray is answered.
 *
 * A walk is a pure function of its sample and the answers to its rays,
 * so walks may be answered in any interleaving.
 */
struct Walk {
	/**
	 * The path.
	 */
	Path path;
	/**
	 * The extras' state beside the path.
	 */
	Companions companions;
	/**
	 * The light walk it leads with, one where the strategies lead and
	 * none otherwise.
	 *
	 * A light walk is larger than the rest of the walk, so a walk that
	 * leads with none keeps none.
	 */
	std::vector<LightWalk> lead;
	/**
	 * The ray it waits on while it is flying; while it is leading, its
	 * camera ray.
	 */
	Ray ray;
	/**
	 * Its vertex's lines while it is lighting.
	 */
	Lines lines;
	/**
	 * The next line to test.
	 */
	std::uint32_t line;
	/**
	 * Where it is.
	 */
	WalkPhase phase;
	/**
	 * What its camera ray met first, kept only where the tracing records
	 * (Tracing.recording); missedSurface() until then.
	 */
	Surface first;
};

/**
 * The ray a walk waits on.
 */
struct Wanted {
	/**
	 * What it asks.
	 */
	RayKind kind;
	/**
	 * The ray. A line's ray starts at the line's origin and reaches its
	 * reach.
	 */
	Ray ray;
};

/**
 * The answer to a walk's ray.
 */
struct Answer {
	/**
	 * The ray's hit. For a Shadow ray only its kind is read.
	 */
	const Hit& hit;
	/**
	 * The registers the patterns run in.
	 */
	std::vector<double>& registers;
};

/**
 * Start a sample's walk at its camera ray, with the guide its pixel
 * starts with.
 *
 * Where the strategies lead (strategyLeads()), the walk first leads with
 * a light subpath from leadStarted(); a lead that is done at once leaves
 * the walk flying.
 *
 * @param tracing What the transport reads.
 * @param sample The pixel and the sample.
 */
Walk walkStarted(const Tracing& tracing, const PixelSample& sample);

/**
 * Start a sample's walk in place, as walkStarted() starts it (rule 46).
 *
 * Only what the walk reads is written: the lead's kept surfaces and the
 * vertex's lines are emptied by their counts, so a walk whose storage is
 * reused is started without clearing it.
 *
 * @param tracing What the transport reads.
 * @param sample The pixel and the sample.
 * @param walk The walk, any earlier state, which is started over.
 */
void walkRestarted(const Tracing& tracing, const PixelSample& sample, Walk& walk);

/**
 * Return the ray a walk waits on.
 *
 * A leading walk waits on its lead's ray, lightRayWanted(). A lighting
 * walk waits on its next line, lineWanted(). A flying walk waits on its
 * ray: Camera at depth 0, Inside where the ray starts inside a solid,
 * Scatter otherwise.
 *
 * @param tracing What the transport reads.
 * @param walk The walk, not done.
 */
Wanted wantedRay(const Tracing& tracing, const Walk& walk);

/**
 * Return the ray a line waits on: a Shadow ray, whose hit is read only
 * for its kind, where the line must only be clear and no line passes a
 * shape; a Scatter ray, whose hit is read in full, for a glance and
 * wherever a line may pass a shape.
 *
 * @param tracing What the transport reads.
 * @param shadow The line.
 */
Wanted lineWanted(const Tracing& tracing, const Shadow& shadow);

/**
 * Return what a line does at the shape it hit, and what it kept of the
 * light along the stretch to it.
 *
 * A line that met nothing ends there. One that has passed fewer than
 * mostCrossings shapes goes on past a shape its way passes (a hidden
 * one toward the lens, a shadowless one toward a light), and past a
 * crossable() surface, into or out of its body. Any other hit blocks
 * it. Inside a body it keeps the light that the body neither absorbs
 * nor turns aside.
 *
 * @param tracing What the transport reads.
 * @param met The line, its hit, the wavelengths and the registers.
 */
LineStep lineStepped(const Tracing& tracing, const LineHit& met);

/**
 * Take the answer to a walk's ray and step the walk in place to its
 * next ray or its end (rule 46).
 *
 * A leading walk steps its lead by lightWalkAnswered(), and flies on its
 * camera ray once the lead is done. For a line, where it met nothing its
 * radiance is added, and for a
 * glance its radiance times the glow the glance counts; once the lines
 * are spent the path scatters. For any other ray the
 * strategies act at arrival, the path stands on the hit and takes the
 * guide it keeps there, the strategies act at the vertex, and the walk tests the lines they asked
 * for, in order, before it scatters. A ray that met nothing ends the
 * walk after arrival.
 *
 * Under the rules a camera ray passes hidden shapes, and a line passes
 * the shapes its way says (lineStepped()). A scattered ray in the open
 * that meets a shadowless shape first looks past it: it is cast on past
 * every shadowless shape, meeting the glows of the surfaces it reaches
 * there, up to the first other surface or the sky; then it is cast again
 * as drawn, or from where it last crossed a body that bends nothing,
 * meets the lights up to that reach, and stands on the shadowless
 * shape.
 *
 * @param tracing What the transport reads.
 * @param walk The walk, not done, which is stepped.
 * @param answer The ray's hit and the registers.
 */
void walkAnswered(const Tracing& tracing, Walk& walk, const Answer& answer);

/**
 * Send one sample of a group on its way from the first vertex the
 * group shares, in place (rule 46).
 *
 * The first sample of a group walks alone to that vertex, lights it and
 * waits there (WalkPhase::Branching). Every sample of the group, the
 * first included, then leaves it by a direction of its own: it takes
 * the waiting walk's path and companions and draws by its own index.
 * The first sample keeps what the shared stretch gathered, once for
 * each sample of the group, and the others start with nothing, so the
 * mean over the group is the mean of as many samples alone. The
 * weighing counts the first vertex's aims once a group (sharedAim()).
 *
 * @param tracing What the transport reads.
 * @param stem The group's first sample, waiting on the shared vertex.
 * @param index The sample's place in the pixel's whole run.
 * @param branch The sample's walk, which may be the stem itself for
 * the group's first sample.
 */
void branchedFrom(const Tracing& tracing, const Walk& stem, std::uint32_t index, Walk& branch);

/**
 * Return what a walk that ended brought, as a colour.
 *
 * The first sample of a group that ended before its first vertex, or on
 * it, brings what it gathered once for each sample of the group; the
 * group's other samples then bring nothing.
 *
 * @param walk The walk, done.
 */
Colour walkColour(const Walk& walk);

/**
 * Return the colour of what a group's samples share: what its first
 * sample gathered up to the first vertex, once for each sample of the
 * group. The stopper reads its spread apart from the rest.
 *
 * @param walk The group's first sample: waiting on the shared vertex,
 * or done without having branched.
 */
Colour sharedColour(const Walk& walk);

/**
 * Walk a walk on until it ends or waits to be branched: the step
 * machine's scalar loop, each ray traced by intersect() as it is asked.
 *
 * @param tracing What the transport reads.
 * @param walk The walk.
 */
void walkedOut(const Tracing& tracing, Walk& walk);

/**
 * Trace one sample to its end and return what it brought: the step
 * machine's scalar loop, each ray traced by intersect() as it is
 * asked.
 *
 * @param tracing What the transport reads.
 * @param sample The pixel and the sample.
 */
Traced tracePath(const Tracing& tracing, const PixelSample& sample);

/**
 * Trace one sample to its end, as tracePath() does, and return what it
 * leaves for the frames of a sequence: its colour and, where the tracing
 * records, what its camera ray met first.
 * @param tracing What the transport reads.
 * @param sample The pixel and the sample.
 */
Recorded recordedPath(const Tracing& tracing, const PixelSample& sample);

/**
 * Return what a camera ray that met nothing leaves as its first surface.
 */
Surface missedSurface();

/**
 * A camera ray and what it met first, to make the surface it shows.
 */
struct CameraHit {
	/**
	 * The camera ray.
	 */
	const Ray& ray;
	/**
	 * What it met first, or a miss.
	 */
	const Hit& hit;
	/**
	 * The registers the patterns run in.
	 */
	std::vector<double>& registers;
};

/**
 * Return the surface a camera ray shows: the surface it met, shaded and
 * worn as a walk stands on it, seen from the ray's origin;
 * missedSurface() where it met nothing.
 *
 * A walk records its first surface (Walk.first) through the same
 * steps, so the two agree wherever the walk stands on its camera ray's
 * first hit: it passes no hidden shape, crosses no body and collides in
 * no medium. Only the sides of the surface read wavelengths, and no
 * field of a Surface reads the sides, so the surface is the same for
 * any wavelengths.
 *
 * @param tracing What the transport reads.
 * @param met The ray, its hit and the registers.
 */
Surface cameraSurface(const Tracing& tracing, const CameraHit& met);

/**
 * Return what a sample that drew no path leaves: no light, and a camera
 * ray that met nothing.
 */
Recorded emptyRecord();

/**
 * Start a sample: its numbers, its wavelengths and its camera ray.
 *
 * The camera ray passes anywhere in its pixel at factor 1; above it,
 * anywhere in the shown pixel the film plan's phase names. The stream
 * hashes the frame's seed with the pixel. The wavelengths
 * step from a start the pixel draws, so a pixel's run of samples spreads
 * them evenly.
 *
 * @param tracing What the transport reads.
 * @param sample The pixel and the sample.
 */
Onward startedPath(const Tracing& tracing, const PixelSample& sample);

/**
 * Start a sample in place, as startedPath() does, and return its camera
 * ray.
 *
 * A wave restarts its walks this way, so the path is written once into
 * the walk that keeps it.
 *
 * @param tracing What the transport reads.
 * @param sample The pixel and the sample.
 * @param path The path to start.
 */
Ray startedPathIn(const Tracing& tracing, const PixelSample& sample, Path& path);

/**
 * Meet the lights along the ray in flight, weighed against the other
 * strategies.
 *
 * A camera ray that met a surface meets nothing, and one that left the
 * scene meets the sky alone. Any other ray meets every light within the
 * hit that the rules let end the path from the vertex that drew the
 * ray, each weighed by meetDensities() as if it were the only light,
 * and kept along the medium it travels in. What it meets is added to
 * the path's radiance in place.
 *
 * @param tracing What the transport reads.
 * @param arrival The path and the hit.
 * @param rules What the path has met that the rules read.
 */
void metAlong(const Tracing& tracing, const Arrival& arrival, const RuleState& rules);

/**
 * Meet the glow of the surface the path stands on, weighed against the
 * other strategies, adding it to the path's radiance in place, where
 * the rules let it end the path from the vertex that drew the ray.
 *
 * @param tracing What the transport reads.
 * @param path The path on its vertex.
 * @param rules What the path has met that the rules read.
 */
void metGlow(const Tracing& tracing, Path& path, const RuleState& rules);

/**
 * Stand the path on the surface it met, in place.
 *
 * What the ray kept along its medium since it last crossed a surface is
 * taken, the path stands on the shaded vertex, and the weighing is
 * carried over the hit from where the ray was drawn.
 *
 * @param tracing What the transport reads.
 * @param arrival The path and the hit.
 * @param vertex The surface, shaded and worn by surfaceAt().
 */
void stoodOn(const Tracing& tracing, const Arrival& arrival, const Vertex& vertex);

/**
 * Return whether a ray crosses the surface it met as if it were not
 * there: a glass of index 1, smooth or rough, without dispersion, a
 * film or a glow, whose indices on its two sides are the same at every
 * lane, such as the boundary of a fog body. A film stays a vertex,
 * since it reflects. A path's weighing, its vertex and what the rules read pass over
 * such a crossing unchanged; only the medium changes.
 *
 * @param tracing What the transport reads.
 * @param vertex The surface met, shaded.
 */
bool crossable(const Tracing& tracing, const Vertex& vertex);

/**
 * Return the vertex a walk stands on where its ray met a surface.
 *
 * The surface is shaded, the features are worn and the indices on its
 * two sides are set. Its geometric normal faces the ray, and it is lifted
 * by the bias plus twice the hit's loosened tolerance.
 *
 * @param tracing What the transport reads.
 * @param surfacing The hit, the ray and the registers.
 */
Vertex surfaceAt(const Tracing& tracing, const Surfacing& surfacing);

/**
 * Return what the glass a ray travels inside keeps of the light along a
 * stretch, per lane: one in the open.
 *
 * @param tracing What the transport reads.
 * @param leg The glass and the stretch.
 */
Spectrum4 keptOnLeg(const Tracing& tracing, const MediumLeg& leg);

/**
 * Return the density the bare material draws a drawn direction's
 * reverse with: for light arriving along the drawn direction, back
 * toward the ray that met the vertex. A direction through the surface is
 * asked of the surface seen from its other side.
 *
 * @param materials The finishes.
 * @param turn The surface, the ray met and the direction drawn.
 */
double densityBack(const Materials& materials, const TurnBack& turn);

/**
 * Return the ray that tests a line: from its origin along its direction,
 * as far as its reach, in the open.
 *
 * @param shadow The line.
 */
Ray lineRay(const Shadow& shadow);

/**
 * Return whether a drawn direction leaves on the wrong side of the
 * vertex's geometric surface: below it for a reflection, above it for a
 * passage.
 *
 * @param vertex The vertex.
 * @param drawn The direction drawn.
 */
bool below(const Vertex& vertex, const Scatter& drawn);

/**
 * Return whether light passing between two sides bends: their indices
 * differ at the hero.
 *
 * @param sides The two sides.
 */
bool bending(const Sides& sides);

/**
 * Aim at the lights from the path's vertex, weighed, and add the lines
 * to test to the vertex's lines in place.
 *
 * Only a vertex in the open, or on the inside of the glass the path
 * travels in, aims, and only toward its open side. Every light is
 * offered in order. A light with its own line comes first, in light
 * order, where its offer brings anything, and the selection is told how
 * many own lines came before. A shared light is kept in a
 * one-sample reservoir by the lane mean of its offer, and the light
 * kept comes last, scaled back by its chance.
 *
 * @param tracing What the transport reads.
 * @param standing The path on its vertex and its companions.
 * @param lines The vertex's lines.
 */
void aimedFrom(const Tracing& tracing, const Standing& standing, Lines& lines);

/**
 * Return the density the eye's draw at the path's vertex is weighed with
 * toward a direction: the direction member's with the path's guide, or,
 * past the first vertex where join or connect counts, the bare
 * material's, which the walks from the lights compute too.
 *
 * @param tracing What the transport reads.
 * @param standing The path on its vertex and its companions.
 * @param toward The unit direction and the material's own density of it.
 */
double weighedDensity(const Tracing& tracing, const Standing& standing, const OwnDensity& toward);

/**
 * Return one light's offer from the path's vertex.
 *
 * Its weight reads the eye's density for the aimed direction as
 * scatteredFrom() weighs a draw: the direction member's, or past the
 * first vertex, where join or connect counts, the bare material's.
 *
 * @param tracing What the transport reads.
 * @param standing The path on its vertex and its companions.
 * @param asked The light and the vertex's light numbers.
 */
Offer offerOf(const Tracing& tracing, const Standing& standing, const LightAsked& asked);

/**
 * Take a walk's next step, in place: on along a drawn ray, or done.
 *
 * It draws a direction by the direction member with the walk's guide,
 * carries the throughput, keeps the hero alone
 * past a parting bend, plays roulette from the minimum depth with a
 * survival of the largest lane held under 0.95, carries the weighing,
 * and enters or leaves glass. A path at its maximum depth or whose draw
 * finds nothing on the right side of the surface ends. The weighing
 * reads the density the direction was drawn with; past the first vertex,
 * where join or connect counts, it reads the bare material's density instead, which
 * the walks from the lights compute too, so both ends weigh a path
 * alike.
 *
 * @param tracing What the transport reads.
 * @param walk The walk, its path on its vertex.
 */
void scatteredFrom(const Tracing& tracing, Walk& walk);

/**
 * Return the glow a glance counts, per lane: the glow of the surface it
 * met, toward the glance, times what the cell counts of it there; zero
 * for a miss or a surface outside the cell.
 *
 * @param tracing What the transport reads.
 * @param glance The path, the line and the surface met.
 */
Spectrum4 glancedGlow(const Tracing& tracing, const Glance& glance);

}
