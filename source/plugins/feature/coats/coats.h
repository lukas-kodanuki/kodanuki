#pragma once

#include "core/geometry.h"
#include "math/vector.h"
#include "plugins/transport/paths/material/material.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * Whether a coat may change a roughness: false when the build leaves
 * this plugin out (CMake's `-DOFF`), and every material then keeps its
 * own roughness, so no ladder dithers it between rungs. Read once at
 * start.
 */
extern const bool coatsRoughen;

/**
 * The number of steps a roughness ladder climbs from 0 to 1.
 */
constexpr int roughnessSteps = 16;

/**
 * What the coats feature reads: each material's ladder of roughnesses.
 *
 * Where a coat of the scene may change a roughness, every material but
 * a matte is drawn again at roughnessSteps + 1 roughnesses, rung k at
 * k / roughnessSteps; the rungs follow every finish made before them.
 */
struct CoatLadders {
	/**
	 * Each material's rung at roughness 0, as an index into the featured
	 * finishes, or noMaterial where it has no ladder. Matches
	 * Looks::materials one for one.
	 */
	std::vector<std::uint32_t> ladders;
	/**
	 * Each material's own roughness, 1 for a matte.
	 */
	std::vector<double> roughness;
	/**
	 * The materials the rungs are drawn as, ladder by ladder, rung by
	 * rung.
	 */
	std::vector<Material> rungs;
	/**
	 * The scene material each ladder climbs, in the rungs' order.
	 */
	std::vector<std::uint32_t> laddered;
	/**
	 * The index of the first rung among the featured finishes.
	 */
	std::uint32_t first;
};

/**
 * A point to weigh a coat at, with the registers its programs run in.
 */
struct CoatProbe {
	/**
	 * The point, its unit normal facing the ray, and the leaf whose own
	 * space a pattern is read in.
	 */
	const PatternSpot& spot;
	/**
	 * The registers (rule 46).
	 */
	std::vector<double>& registers;
};

/**
 * A shaded surface to lay a leaf's coats over.
 */
struct CoatWear {
	/**
	 * The scene and the featured finishes.
	 */
	const Shading& shading;
	/**
	 * The hit.
	 */
	const Hit& hit;
	/**
	 * The shading the core gave the hit.
	 */
	Shaded shaded;
	/**
	 * The unit heading of the ray.
	 */
	math::Vector heading;
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
 * Return each material's ladder and roughness, the rungs numbered from
 * a first index.
 *
 * @param looks The scene's looks: its materials and coats.
 * @param first The index the first rung will have among the featured
 *        finishes.
 */
CoatLadders coatLaddersOf(const Looks& looks, std::uint32_t first);

/**
 * Return whether any leaf of a scene wears a coat, so that the feature
 * is on.
 *
 * @param scene The scene.
 */
bool sceneWearsCoats(const Scene& scene);

/**
 * Return the finishes with every rung's finish appended, at the indices
 * the ladders name.
 *
 * @param ladders The rungs.
 * @param materials The finishes so far.
 * @param smooth How a glass of roughness 0 answers.
 */
Materials withRungs(const CoatLadders& ladders, const Materials& materials, SmoothGlass smooth);

/**
 * Return the scene material a finish is drawn as: the material a rung
 * climbs, or the finish itself where it is no rung.
 *
 * @param ladders The rungs.
 * @param material The finish, as an index into the featured finishes.
 */
std::uint32_t ladderBody(const CoatLadders& ladders, std::uint32_t material);

/**
 * Return how much of a coat a point wears, from 0 to 1.
 *
 * The mask's reach plus the ragged noise is faded over soft by a smooth
 * step, or a hard edge where soft is 0; a pattern mask's value is
 * clamped to 0..1 instead. The reach is the shape's distance turned
 * around (inside), the band's half width less the distance's size
 * (along), the normal's cosine to the axis less the level (facing), the
 * point's height along the axis less the level (above), or the
 * pattern's value; the noise is ragged times twice the grain's value
 * noise less one.
 *
 * @param probe The point, its normal, its leaf and the registers.
 * @param coat The coat.
 */
double coatWeight(const CoatProbe& probe, const Coat& coat);

/**
 * Return the rung either side of a roughness, picked by a hash of the
 * point: the upper one at the share the roughness lies past the
 * lower. A material with no ladder, or at its own roughness, stays
 * itself.
 *
 * @param ladders The ladders and the materials' roughnesses.
 * @param material The scene material.
 * @param roughness The roughness worn, clamped to 0..1.
 * @param point The point.
 */
std::uint32_t ladderedMaterial(const CoatLadders& ladders, std::uint32_t material, double roughness, const math::Vector& point);

/**
 * Lay the hit's leaves' coats over its shading.
 *
 * Each leaf's coats are laid on innermost first, each as far as its
 * weight, over the leaf's own albedo, glow and material. A paint mixes
 * its albedo, glow and roughness in and becomes the material from a
 * weight of a half; a tint, a shade, a rough or a polish, a wet and a
 * glow change the albedo, the roughness and the glow; a bump adds its
 * relief's slope along the surface, times the weight, to a tilt. After
 * all of them, the shading normal is the normal less the tilt, kept
 * only where it still faces the ray, and a changed roughness picks a
 * rung (ladderedMaterial()). Across a smooth seam the other leaf is worn
 * the same way, and its albedo and glow are mixed in by the hit's
 * share. A hit whose leaves wear no coat keeps its shading.
 *
 * @param ladders The ladders.
 * @param wear The scene, the hit, its shading, the heading and the
 *        registers.
 */
Shaded wornCoats(const CoatLadders& ladders, const CoatWear& wear);

}
