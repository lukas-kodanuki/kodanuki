#pragma once

#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * Whether the scene's path rules are read: false when the build leaves
 * this plugin out (CMake's `-DOFF`), and every path then follows the
 * physics alone. Read once at start.
 */
extern const bool pathRulesRead;

/**
 * What a path has met that a rule reads, one bit each.
 */
enum class RuleBit : std::uint8_t {
	/**
	 * It has stood on a vertex that is not sharp.
	 */
	Diffused = 1,
	/**
	 * The caustic it stands in is refused by a shape: the last vertex
	 * that is not sharp, or a sharp one since.
	 */
	Refused = 2,
	/**
	 * It stands on a sharp vertex.
	 */
	Sharp = 4,
	/**
	 * Its ray in flight is inside a shape it passes.
	 */
	Passing = 8,
	/**
	 * Its scattered ray looks past a shadowless shape, for the lights
	 * alone.
	 */
	Beyond = 16,
	/**
	 * Its scattered ray has looked past such a shape and is cast again
	 * to stand on it.
	 */
	Looked = 32,
};

/**
 * Which way a line or a ray runs, which says the shapes it passes.
 */
enum class Passage : std::uint8_t {
	/**
	 * A camera ray, or a line to the lens: it passes hidden shapes.
	 */
	ToLens,
	/**
	 * A line to a light, or a ray from a light to its first surface: it
	 * passes shadowless shapes.
	 */
	ToLight,
	/**
	 * A line between two surfaces of a path: it passes no shape.
	 */
	Between,
};

/**
 * What a path has met, as the rules read it.
 *
 * For a path from the eye the bits are those at the vertex it stands
 * on, stepped from the eye on. For a path from a light they say, in the
 * light's order, the run from the light to its first vertex that is not
 * sharp: whether there is one, whether a shape of the run refuses
 * caustics, and whether the first vertex is sharp.
 */
struct RuleState {
	/**
	 * The RuleBit bits or'ed, the flight bits Passing, Beyond and Looked
	 * included while a ray flies.
	 */
	std::uint8_t bits;
	/**
	 * The bits at the vertex that drew the ray in flight, without the
	 * flight bits.
	 */
	std::uint8_t drawn;
	/**
	 * The scene leaf of the vertex it stands on, or noLeaf: for a path
	 * from a light, its first vertex.
	 */
	std::uint32_t standing;
	/**
	 * The scene leaf of the vertex that drew the ray in flight, or
	 * noLeaf for a camera ray.
	 */
	std::uint32_t drawnOn;
	/**
	 * How far a ray that looked past shadowless shapes reached, from
	 * where it was drawn to the first other surface.
	 */
	float beyond;
};

/**
 * What the rules feature reads: each leaf's rules and groups, and each
 * lamp's reach.
 */
struct PathRules {
	/**
	 * Each leaf's rules, matching Scene::leaves.
	 */
	std::vector<std::uint8_t> rules;
	/**
	 * Each leaf's groups, matching Scene::leaves.
	 */
	std::vector<std::uint64_t> groups;
	/**
	 * Each lamp's reach, zero for every shape, matching Lighting::lights.
	 */
	std::vector<std::uint64_t> reaches;
	/**
	 * Whether each lamp lets caustics end on it, matching Lighting::lights.
	 */
	std::vector<bool> caustics;
};

/**
 * A vertex a path steps onto.
 */
struct RuleStep {
	/**
	 * What the path met before.
	 */
	const RuleState& state;
	/**
	 * Whether the vertex is sharp.
	 */
	bool sharp;
	/**
	 * The scene leaf whose rules the vertex reads, or noLeaf.
	 */
	std::uint32_t leaf;
};

/**
 * A path that would end on a light.
 */
struct RuleEnding {
	/**
	 * The light: a lamp's number, or any larger number for the sky and
	 * the glows.
	 */
	std::uint32_t light;
	/**
	 * The bits at the vertex next to the light.
	 */
	std::uint8_t bits;
	/**
	 * The scene leaf of the vertex next to the light, or noLeaf where
	 * there is none.
	 */
	std::uint32_t standing;
};

/**
 * A path that would end on a glowing surface.
 */
struct GlowEnding {
	/**
	 * The bits at the vertex next to the glow.
	 */
	std::uint8_t bits;
	/**
	 * The scene leaf that glows.
	 */
	std::uint32_t leaf;
};

/**
 * Return a scene's rules, groups and reaches.
 *
 * @param scene The scene: its leaves.
 * @param lighting The scene's lighting: its lights.
 */
PathRules pathRulesOf(const Scene& scene, const Lighting& lighting);

/**
 * Return whether a scene says any rule or reach, so that the feature
 * is on.
 *
 * @param scene The scene: its leaves.
 * @param lighting The scene's lighting: its lights.
 */
bool sceneHasRules(const Scene& scene, const Lighting& lighting);

/**
 * Return a path that has met nothing: no bits, standing nowhere.
 */
RuleState freshRules();

/**
 * Return what a path from the eye has met once it stands on one more
 * vertex, stepped from the bits at the vertex that drew its ray.
 *
 * A vertex that is not sharp starts a new run, refused where its shape
 * says no caustic lands on it; a sharp one adds to the run, refused
 * where its shape says none is focused by it. The flight bits are
 * cleared, and the path stands on the step's leaf.
 *
 * @param rules The leaves' rules.
 * @param step What it met, the vertex's sharpness and leaf.
 */
RuleState steppedRules(const PathRules& rules, const RuleStep& step);

/**
 * Return what a path from a light has met once it stands on one more
 * vertex, in the light's order.
 *
 * The first vertex is the one an ending stands on, and says whether it
 * is sharp. Until the first vertex that is not sharp each vertex whose
 * shape refuses caustics refuses the run; that vertex ends the run, and
 * nothing after it changes the state.
 *
 * @param rules The leaves' rules.
 * @param step What it met, the vertex's sharpness and leaf.
 */
RuleState lightSteppedRules(const PathRules& rules, const RuleStep& step);

/**
 * Return the bits at a light path's first vertex for the whole path an
 * eye path's vertex makes by connecting to it: the light path's run
 * where it holds a vertex that is not sharp, else the eye's run carried
 * over the sharp vertices of the light path.
 *
 * @param eye The bits at the eye path's vertex.
 * @param light The light path's state.
 */
std::uint8_t connectedBits(std::uint8_t eye, const RuleState& light);

/**
 * Return whether a light may end a path.
 *
 * A caustic ends where the vertex next to the light is sharp after one
 * that is not; it is refused where a shape it met or the light says
 * so. A lamp with a reach ends only paths whose vertex next to it
 * stands on a shape of its groups; a path with no such vertex is never
 * refused by it. The sky and the glows say no rule of their own.
 *
 * @param rules The leaves' rules and the lamps' reaches.
 * @param ending The light, the bits and where the path stands.
 */
bool rulesLetEnd(const PathRules& rules, const RuleEnding& ending);

/**
 * Return whether a glowing surface may end a path: the caustic is
 * refused where a shape the path met says so, or where the glowing
 * shape itself does.
 *
 * @param rules The leaves' rules.
 * @param ending The bits and the glowing leaf.
 */
bool rulesLetGlow(const PathRules& rules, const GlowEnding& ending);

/**
 * Return whether a ray or a line passes a leaf it hit: one toward the
 * lens passes a hidden leaf, one toward a light a shadowless leaf.
 *
 * @param rules The leaves' rules.
 * @param leaf The leaf hit, or noLeaf.
 * @param passage Which way it runs.
 */
bool rulesPass(const PathRules& rules, std::uint32_t leaf, Passage passage);

}
