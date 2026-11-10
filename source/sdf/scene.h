#pragma once

#include "math/vector.h"
#include "sdf/program.h"
#include "spectral/colour.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * How two operands of a node are joined, or a node that is a leaf.
 */
enum class Join : std::uint8_t {
	/**
	 * A leaf: one placed shape.
	 */
	Leaf,
	/**
	 * The union of the children, smooth where the radius is above zero.
	 */
	Merge,
	/**
	 * The first child minus the rest.
	 */
	Cut,
	/**
	 * What all the children share.
	 */
	Clip,
};

/**
 * What a material is made of.
 */
enum class MaterialKind : std::uint8_t {
	/**
	 * A rough diffuse surface.
	 */
	Matte,
	/**
	 * A diffuse base under a glossy dielectric coat.
	 */
	Plastic,
	/**
	 * A conductor, glossy by its roughness.
	 */
	Metal,
	/**
	 * A dielectric that light passes into, a true mirror at roughness 0.
	 */
	Glass,
	/**
	 * The phase function inside a scattering body. No scene writes it;
	 * the scattering feature makes one finish of it per body at load.
	 */
	Medium,
};

/**
 * Where a coat is worn: what its weight over a surface follows.
 */
enum class CoatMask : std::uint8_t {
	/**
	 * Past a height along a world axis (below is above along the axis
	 * turned around).
	 */
	Above,
	/**
	 * Where the normal's cosine to the axis passes the level.
	 */
	Facing,
	/**
	 * Where a function read at the world point is below zero.
	 */
	Inside,
	/**
	 * In a band about the surface of a function.
	 */
	Along,
	/**
	 * A function's value clamped to 0..1.
	 */
	Pattern,
	/**
	 * Every point, whole: a bump written without a mask.
	 */
	Everywhere,
};

/**
 * What a coat does to the look where it is worn.
 */
enum class CoatEffect : std::uint8_t {
	/**
	 * Mixes a material's albedo and glow in, and becomes it past a half.
	 */
	Paint,
	/**
	 * Multiplies the albedo by a colour.
	 */
	Tint,
	/**
	 * Darkens toward black (below 0) or lightens toward white.
	 */
	Shade,
	/**
	 * Moves the roughness toward 1 (above 0) or toward 0.
	 */
	Rough,
	/**
	 * Halves the albedo and takes the roughness down by four fifths.
	 */
	Wet,
	/**
	 * Adds light of a colour.
	 */
	Glow,
	/**
	 * Tilts the shading normal along a relief's slope.
	 */
	Bump,
};

/**
 * The height field a bump tilts the normal by.
 */
enum class Relief : std::uint8_t {
	/**
	 * Two octaves of value noise.
	 */
	Grain,
	/**
	 * Hollows where the noise passes six tenths.
	 */
	Pits,
	/**
	 * Noise drawn out along an axis, as on brushed metal.
	 */
	Streak,
	/**
	 * A function of the point, in the painted shape's space or the
	 * world's.
	 */
	Pattern,
};

/**
 * What kind of lamp a light is.
 */
enum class LightKind : std::uint8_t {
	/**
	 * A point that sends its power evenly every way.
	 */
	Point,
	/**
	 * Parallel light from one direction, with no disc.
	 */
	Directional,
	/**
	 * A glowing ball.
	 */
	Sphere,
	/**
	 * A glowing parallelogram, lit on one side.
	 */
	Quad,
	/**
	 * A far disc along a direction.
	 */
	Sun,
	/**
	 * A far disc like a sun, lit on the side that faces the first sun.
	 */
	Moon,
};

/**
 * A rule a shape says about the paths that meet it, one bit each.
 */
enum class Rule : std::uint8_t {
	/**
	 * No caustic is focused by the shape or lands on it.
	 */
	Causticless = 1,
	/**
	 * The camera's ray and a line to the lens pass through it.
	 */
	Hidden = 2,
	/**
	 * A line that ends on a light passes through it, and a scattered ray
	 * looks past it for the lights.
	 */
	Shadowless = 4,
};

/**
 * How graded colours are pressed into the displayable range.
 */
enum class Curve : std::uint8_t {
	/**
	 * A filmic shoulder that rolls highlights off.
	 */
	Filmic,
	/**
	 * Straight, clamped at one.
	 */
	Clamped,
};

/**
 * What the sky is.
 */
enum class SkyKind : std::uint8_t {
	/**
	 * A gradient from the ground through the horizon to the zenith.
	 */
	Gradient,
	/**
	 * The air over the scene lit by its first sun.
	 */
	Physical,
	/**
	 * Runs of colour for day, dusk and night, blended by how high the
	 * first sun stands.
	 */
	Painted,
};

/**
 * A point to evaluate shapes at and the registers they run in.
 */
struct Probe {
	/**
	 * The point in world space.
	 */
	math::Vector p;
	/**
	 * The scratch registers a program runs in (rule 46).
	 */
	std::vector<double>& registers;
};

/**
 * Where a leaf stands: the map from world space into its own space.
 */
struct Placement {
	/**
	 * The linear part from world space into the shape's space, by rows,
	 * applied after the origin is taken off.
	 */
	double linear[3][3];
	/**
	 * Where the shape's origin stands in world space.
	 */
	double origin[3];
	/**
	 * What the shape's distance is multiplied by: the smallest scale.
	 */
	double stretch;
};

/**
 * One placed and painted shape: a program with its arguments.
 */
struct Leaf {
	/**
	 * The program, as an index into Scene::programs.
	 */
	std::uint32_t program;
	/**
	 * Where its number arguments start in Scene::arguments.
	 */
	std::uint32_t arguments;
	/**
	 * Where it stands.
	 */
	Placement placement;
	/**
	 * Its material, as an index into Looks::materials.
	 */
	std::uint32_t material;
	/**
	 * Its coats, innermost first, as indices into Looks::coats.
	 */
	std::vector<std::uint32_t> coats;
	/**
	 * The rules it says, Rule bits or'ed.
	 */
	std::uint8_t rules;
	/**
	 * The groups it belongs to, one bit per group of Looks::groups.
	 */
	std::uint64_t groups;
};

/**
 * A node of the scene's CSG: a leaf, or a join of children.
 */
struct Node {
	/**
	 * How the children are joined, or Leaf.
	 */
	Join join;
	/**
	 * The smooth join's radius, zero for a hard one.
	 */
	double radius;
	/**
	 * The leaf of a Leaf node, as an index into Scene::leaves.
	 */
	std::uint32_t leaf;
	/**
	 * The children of a join, as indices into Scene::nodes.
	 */
	std::vector<std::uint32_t> children;
};

/**
 * A surface colour: a flat colour, or a ramp over a pattern.
 */
struct Albedo {
	/**
	 * The colours, linear, three numbers each: one for a flat colour, the
	 * ramp's stops spread evenly over 0..1 otherwise.
	 */
	std::vector<float> stops;
	/**
	 * The pattern's program, as an index into Scene::programs, or -1 for
	 * a flat colour.
	 */
	std::int32_t program;
	/**
	 * Where the pattern's arguments start in Scene::arguments.
	 */
	std::uint32_t arguments;
	/**
	 * Whether the pattern is read in world space rather than the painted
	 * shape's.
	 */
	bool world;
};

/**
 * A material as the scene says it.
 */
struct Material {
	/**
	 * What it is made of.
	 */
	MaterialKind kind;
	/**
	 * Its colour.
	 */
	Albedo albedo;
	/**
	 * How rough it is, 0 to 1.
	 */
	float roughness;
	/**
	 * The index of refraction of glass: Cauchy's A where it disperses.
	 */
	float refraction;
	/**
	 * How far a glass's index spreads with the wavelength: Cauchy's B, in
	 * square micrometres, so the index is refraction + dispersion / the
	 * wavelength in micrometres squared; 0 for none.
	 */
	float dispersion;
	/**
	 * How much of a metre of travel a glass's body turns aside, the same
	 * at every wavelength; 0 for none.
	 */
	float scatter;
	/**
	 * How forward the body's scattering leans, Henyey-Greenstein's g in
	 * (-1, 1); 0 is even every way.
	 */
	float lean;
	/**
	 * How much glass absorbs per metre, per channel.
	 */
	Colour absorb;
	/**
	 * The colour of the light it gives off, black for none.
	 */
	Colour glow;
	/**
	 * What the glow is multiplied by.
	 */
	float strength;
	/**
	 * The thin film a metal, a plastic or a glass wears: its thickness in
	 * nanometres, 0 for none, and its index.
	 */
	float film[2];
	/**
	 * Whether the highlight guide is built on it.
	 */
	bool guided;
	/**
	 * Whether paths from the lights are drawn toward it.
	 */
	bool relays;
	/**
	 * Whether a path aims at its glow rather than only meeting it.
	 */
	bool aimed;
};

/**
 * A change of a surface's look where a mask weighs it.
 */
struct Coat {
	/**
	 * What the weight follows.
	 */
	CoatMask mask;
	/**
	 * What the coat does.
	 */
	CoatEffect effect;
	/**
	 * The unit world axis of an Above or Facing mask.
	 */
	math::Vector axis;
	/**
	 * The height along the axis (Above) or the cosine (Facing) the mask
	 * turns on at.
	 */
	float level;
	/**
	 * The width of an Along mask's band.
	 */
	float width;
	/**
	 * How far the edge takes to fade, half of it either side; 0 is a hard
	 * edge.
	 */
	float soft;
	/**
	 * The function an Inside, Along or Pattern mask reads, as an albedo
	 * whose program and arguments name it.
	 */
	Albedo pattern;
	/**
	 * The material a Paint coat mixes in, as an index into
	 * Looks::materials.
	 */
	std::uint32_t material;
	/**
	 * The colour of a Tint or Glow coat.
	 */
	Colour colour;
	/**
	 * How far the effect goes: a Shade's or a Rough's amount, a Wet's, a
	 * Glow's strength, or how high a Bump's relief stands.
	 */
	float amount;
	/**
	 * How far noise moves the mask's edge, in the units its reach is
	 * measured in; zero for a clean edge.
	 */
	float ragged;
	/**
	 * How large the grain of that noise is, in metres.
	 */
	float grain;
	/**
	 * A Bump's height field.
	 */
	Relief relief;
	/**
	 * How large a noise relief's grain is, in metres.
	 */
	float span;
	/**
	 * The unit axis a Streak relief is drawn out along.
	 */
	math::Vector along;
	/**
	 * The function a Pattern relief reads as a height, as an albedo whose
	 * program and arguments name it.
	 */
	Albedo height;
};

/**
 * A light as the scene says it; each kind reads its own fields.
 */
struct Light {
	/**
	 * What kind of lamp it is.
	 */
	LightKind kind;
	/**
	 * Where a point, a sphere or a quad's corner stands.
	 */
	math::Vector at;
	/**
	 * The direction toward a sun, a moon or a directional light.
	 */
	math::Vector direction;
	/**
	 * A quad's first edge from its corner.
	 */
	math::Vector along;
	/**
	 * A quad's second edge from its corner.
	 */
	math::Vector across;
	/**
	 * A sphere's radius.
	 */
	float radius;
	/**
	 * A sun's or a moon's disc, edge to edge, in radians.
	 */
	float angle;
	/**
	 * What it sends, per channel: the power of a point, a sphere or a
	 * quad; what a sun, a full moon or a directional light lands on a
	 * surface facing it.
	 */
	Colour power;
	/**
	 * The groups whose shapes it reaches, one bit per group of
	 * Looks::groups; zero reaches every shape.
	 */
	std::uint64_t reaches;
	/**
	 * Whether a caustic may end on it.
	 */
	bool caustics;
};

/**
 * The camera.
 */
struct Camera {
	/**
	 * Where the eye stands.
	 */
	math::Vector eye;
	/**
	 * The point it looks at.
	 */
	math::Vector target;
	/**
	 * The direction that is up in the frame.
	 */
	math::Vector up;
	/**
	 * The field of view, in radians.
	 */
	float fov;
	/**
	 * The radius of the lens, 0 for a pinhole.
	 */
	float aperture;
	/**
	 * How far along the view the sharp plane lies, 0 for the target's.
	 */
	float focus;
};

/**
 * How the frame is rendered.
 */
struct Render {
	/**
	 * The width in pixels.
	 */
	int width;
	/**
	 * The height in pixels.
	 */
	int height;
	/**
	 * The noise a pixel may keep, in steps of a graded byte.
	 */
	float tolerance;
	/**
	 * The seed of every random number.
	 */
	int seed;
	/**
	 * The reach of the automatic region the tree is built over.
	 */
	float far;
	/**
	 * The most samples a pixel draws, 0 for the tolerance's.
	 */
	int samples;
	/**
	 * How many samples a batch holds, 0 for the tolerance's.
	 */
	int batch;
	/**
	 * How many samples share a first vertex: 1, 2, 4 or 8 for every
	 * pixel, or 0 where the stopping says it for each pixel.
	 */
	int split;
	/**
	 * The most surfaces a path stands on: one more than the most it
	 * scatters from, apart from its collisions.
	 */
	int surfaces;
	/**
	 * The most collisions inside scattering bodies a path stands on,
	 * apart from its surfaces.
	 */
	int collisions;
};

/**
 * How light is turned into displayed colour.
 */
struct Grade {
	/**
	 * What the light is multiplied by first.
	 */
	float exposure;
	/**
	 * The curve.
	 */
	Curve curve;
};

/**
 * One colour a painted sky shows, and the height it shows it at.
 */
struct SkyStop {
	/**
	 * How high the colour stands, as the sine of its elevation: -1
	 * straight down, 0 level, 1 straight up.
	 */
	float height;
	/**
	 * The radiance shown there, linear.
	 */
	Colour colour;
};

/**
 * The three runs of colour a painted sky blends between, each at least
 * two stops in order of height.
 */
struct PaintedSky {
	/**
	 * What the sky shows with the sun well up.
	 */
	std::vector<SkyStop> day;
	/**
	 * What it shows toward a sun on the horizon.
	 */
	std::vector<SkyStop> dusk;
	/**
	 * What it shows with the sun well down.
	 */
	std::vector<SkyStop> night;
};

/**
 * The sky as the scene says it; each kind reads its own fields.
 */
struct Sky {
	/**
	 * What the sky is.
	 */
	SkyKind kind;
	/**
	 * A gradient's radiance straight up.
	 */
	Colour zenith;
	/**
	 * A gradient's radiance at the horizon.
	 */
	Colour horizon;
	/**
	 * A gradient's radiance below the horizon, or the fraction of light
	 * the ground sends back under a physical sky.
	 */
	Colour ground;
	/**
	 * How murky a physical sky's air is, about 1 to 10.
	 */
	float haze;
	/**
	 * The colour of the glow about the first sun, black for none.
	 */
	Colour glow;
	/**
	 * What the glow's colour is multiplied by.
	 */
	float strength;
	/**
	 * How far from the sun the glow falls by a factor of e, in radians.
	 */
	float spread;
	/**
	 * A painted sky's runs of colour.
	 */
	PaintedSky painted;
};

/**
 * Points of light on the sky's sphere, one in each of about count
 * equal cells.
 */
struct Stars {
	/**
	 * Whether the scene has stars.
	 */
	bool present;
	/**
	 * How many there are over the whole sphere.
	 */
	float count;
	/**
	 * The radiance of the brightest; most are far dimmer.
	 */
	float strength;
	/**
	 * How wide one looks, edge to edge, in radians.
	 */
	float angle;
	/**
	 * The axis the sphere is laid out about.
	 */
	math::Vector axis;
};

/**
 * A layer of cloud over a round planet.
 */
struct Clouds {
	/**
	 * Whether the scene has clouds.
	 */
	bool present;
	/**
	 * How high the layer's floor is, in metres.
	 */
	float height;
	/**
	 * How thick the layer is, in metres.
	 */
	float thickness;
	/**
	 * The share of the sky it covers, 0 to 1 and past.
	 */
	float cover;
	/**
	 * The light it turns aside per metre where it is thickest.
	 */
	float density;
	/**
	 * How far across the largest clouds run, in metres.
	 */
	float span;
};

/**
 * A scene's geometry: its programs, shapes and objects.
 */
struct Scene {
	/**
	 * The compiled functions, each a program over a point in its own
	 * space and its number arguments.
	 */
	std::vector<Program> programs;
	/**
	 * Every leaf's and pattern's number arguments, end to end.
	 */
	std::vector<double> arguments;
	/**
	 * The placed shapes.
	 */
	std::vector<Leaf> leaves;
	/**
	 * The CSG nodes.
	 */
	std::vector<Node> nodes;
	/**
	 * The objects, as indices into nodes: the field is the minimum over
	 * them.
	 */
	std::vector<std::uint32_t> objects;
	/**
	 * One program for each object, its CSG compiled into one straight
	 * line of steps at load, matching the objects one for one.
	 */
	std::vector<Program> objectPrograms;
};

/**
 * What a scene's shapes look like: the materials and media they are
 * made of, the coats they wear and the groups their rules name.
 */
struct Looks {
	/**
	 * The materials, which Leaf::material indexes.
	 */
	std::vector<Material> materials;
	/**
	 * The coats, which Leaf::coats indexes.
	 */
	std::vector<Coat> coats;
	/**
	 * The names of the groups of shapes, in the order the scene first
	 * says them; a group's bit is its place here.
	 */
	std::vector<std::string> groups;
};

/**
 * What lights a scene: its lights and its sky.
 */
struct Lighting {
	/**
	 * The lights, in the order the scene says them.
	 */
	std::vector<Light> lights;
	/**
	 * The sky.
	 */
	Sky sky;
	/**
	 * The clouds.
	 */
	Clouds clouds;
	/**
	 * The stars.
	 */
	Stars stars;
};

/**
 * How a scene is taken: the camera, the render settings and the grade.
 */
struct Shot {
	/**
	 * The camera.
	 */
	Camera camera;
	/**
	 * The render settings.
	 */
	Render render;
	/**
	 * The grade.
	 */
	Grade grade;
};

/**
 * A loaded scene file: its geometry, looks, lighting and shot, each
 * passed on alone to what needs it.
 */
struct SceneFile {
	/**
	 * The geometry.
	 */
	Scene scene;
	/**
	 * The materials, coats and groups.
	 */
	Looks looks;
	/**
	 * The lights and the sky.
	 */
	Lighting lighting;
	/**
	 * The camera, the render settings and the grade.
	 */
	Shot shot;
};

/**
 * The most groups a scene names.
 */
constexpr std::size_t mostGroups = 64;

/**
 * The fewest steps an object's program has before a hard union at its
 * top is cut into objects of their own.
 */
constexpr std::size_t fewestUnionSteps = 512;

/**
 * Return a leaf's distance at a point: its program run in its own
 * space, times its stretch.
 *
 * @param scene The scene.
 * @param leaf The leaf.
 * @param probe The point and the registers.
 */
double leafDistance(const Scene& scene, const Leaf& leaf, const Probe& probe);

/**
 * Return a node's distance at a point, its children joined as the node
 * says.
 *
 * @param scene The scene.
 * @param node The node, as an index into Scene::nodes.
 * @param probe The point and the registers.
 */
double nodeDistance(const Scene& scene, std::uint32_t node, const Probe& probe);

/**
 * Return the scene's distance at a point: the minimum over its objects.
 *
 * @param scene The scene.
 * @param probe The point and the registers.
 */
double sceneDistance(const Scene& scene, const Probe& probe);

/**
 * Return the first sun of a scene, as an index into Lighting::lights, or
 * the number of lights where there is none.
 *
 * @param lighting The scene's lighting: its lights and sky.
 */
std::uint32_t firstSun(const Lighting& lighting);

/**
 * Return whether a coat of the scene may change a roughness: a paint,
 * a rough or a polish, or a wet. Every material but a matte then has a
 * ladder of roughnesses to be drawn at.
 *
 * @param looks The scene's looks: its materials and coats.
 */
bool sceneRoughens(const Looks& looks);

/**
 * Return the scene's objects with every long hard union cut into its
 * parts.
 *
 * An object whose program has fewestUnionSteps steps or more and whose
 * top node is a merge of radius 0 becomes one object per child, and a
 * child that is again such a merge is cut the same way. The field is
 * the minimum over the objects either way, so nothing a frame shows
 * changes; a cell then holds only the parts near it, and a hit runs
 * only theirs. Smooth joins, cuts and clips keep their operands in one
 * object. Shorter objects stay whole.
 *
 * @param scene The scene, its objects as the language emitted them.
 */
std::vector<std::uint32_t> unionParts(const Scene& scene);

/**
 * Return a step of two operands that belongs to no leaf.
 *
 * @param opcode What the step does.
 * @param a The first operand.
 * @param b The second operand.
 */
Step binaryStep(Opcode opcode, std::uint32_t a, std::uint32_t b);

/**
 * Compile one object's CSG into one program over world space.
 *
 * Each leaf is inlined with its placement, hard joins become minima and
 * maxima, and smooth joins one smooth minimum. Every step carries the
 * leaf it came from; a join carries its left operand's.
 *
 * @param scene The scene.
 * @param object The object, as an index into Scene::objects.
 */
Program objectProgram(const Scene& scene, std::uint32_t object);

/**
 * Compile one leaf alone, with its placement and stretch, into a
 * program over world space whose steps all carry the leaf.
 *
 * @param scene The scene.
 * @param leaf The leaf, as an index into Scene::leaves.
 */
Program leafProgram(const Scene& scene, std::uint32_t leaf);

/**
 * Compile every object of a scene, in scene order.
 *
 * @param scene The scene.
 */
std::vector<Program> objectPrograms(const Scene& scene);

/**
 * Return a world-space program's value at a point.
 *
 * @param program The program.
 * @param probe The point and the registers to run it in.
 */
double programDistance(const Program& program, const Probe& probe);

}
