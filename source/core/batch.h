#pragma once

#include "math/colour.h"
#include "math/vector.h"

#include <cstdint>
#include <string>
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
	 * No walk: the slot of a pool that holds none.
	 */
	Free,
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
	 * It waits on a ray whose hit only its pass reads: no walk of the
	 * transport stands behind the slot, and the pass's fold takes the
	 * answer (the surface pass).
	 */
	Probing,
	/**
	 * It stands on its vertex with its lines spent and waits to draw its
	 * next ray (scatterWalks).
	 */
	Scattering,
	/**
	 * It stands lit on the first vertex its group shares and waits for
	 * the pass to branch it (branchWalks), each sample of the group
	 * going on from there.
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
 * What a ray met.
 */
enum class HitKind : std::uint8_t {
	/**
	 * Nothing within the ray's reach.
	 */
	Missed,
	/**
	 * The surface inside a surface leaf.
	 */
	Surface,
	/**
	 * The side of a filled leaf, where a ray from outside enters it.
	 */
	Solid,
};

/**
 * How well a surface leaf is known to hold the surface.
 */
enum class LeafKind : std::uint8_t {
	/**
	 * Every face is held within the cell's tolerance: a quadric by a
	 * proof over the whole cell, a patch at the points the fit reads.
	 */
	Held,
	/**
	 * The depth cap stopped the cell while a face still strays further
	 * than its tolerance. The walk uses it anyway, and the checks skip
	 * it.
	 */
	Floor,
};

/**
 * A ray in world space, as it is kept between two steps: in single
 * precision (rule 5), 36 bytes, plain data. A kernel that reads it
 * computes in double.
 */
struct Ray {
	/**
	 * Where the ray starts.
	 */
	math::FloatVector origin;
	/**
	 * The unit direction it travels in.
	 */
	math::FloatVector direction;
	/**
	 * How far along it a hit counts; infinity where any hit counts.
	 */
	float far;
	/**
	 * Whether the ray starts inside a solid, so that it looks for the
	 * surface where the table turns outside and passes filled leaves.
	 */
	bool inside;
	/**
	 * The material of the solid a ray from inside travels in, as an index
	 * into Looks::materials. The ray stops where it passes into a solid
	 * of another material, as where a glass rests on a floor. A ray from
	 * outside does not read it.
	 */
	std::uint32_t within;
};

/**
 * Where a ray met the geometry, as it is kept between two steps: in
 * single precision (rule 5), 56 bytes, plain data. The kernel that
 * finds it computes in double and rounds what it found.
 */
struct Hit {
	/**
	 * What it met.
	 */
	HitKind kind;
	/**
	 * How far along the ray.
	 */
	float distance;
	/**
	 * The point met.
	 */
	math::FloatVector point;
	/**
	 * The unit normal pointing out of the inside: the field's gradient
	 * for a surface, the side entered for a solid. It is turned to face
	 * the ray's origin for a ray from outside, and away from it for a
	 * ray from inside, where the gradient says otherwise.
	 */
	math::FloatVector normal;
	/**
	 * The scene leaf whose value decides the field at the point, for its
	 * look, or noLeaf where no program names one.
	 */
	std::uint32_t leaf;
	/**
	 * The leaf across a smooth seam, or noLeaf away from one.
	 */
	std::uint32_t other;
	/**
	 * How much of the other leaf the surface is made of, from 0 away from
	 * a seam to a half in its middle.
	 */
	float share;
	/**
	 * How far the surface, or a patch beside it, may stand from the
	 * field here: the leaf's tolerance.
	 */
	float loosened;
	/**
	 * Whether the leaf met holds its faces within the tolerance.
	 */
	LeafKind leafKind;
	/**
	 * The surface leaf of the tree the hit lies in, noLeaf for any other
	 * hit. The glowing surfaces are kept by it.
	 */
	std::uint32_t cell;
};

/**
 * The material index that names no material: the open air.
 */
constexpr std::uint32_t noMaterial = 0xFFFFFFFFu;

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
 * walk first stood on, which the frames of a sequence keep per pixel,
 * in single precision.
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
	math::FloatVector normal;
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
	FloatColour albedo;
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
 * How many walks a worker's pool holds.
 */
constexpr int poolCapacity = 512;

/**
 * The walks a worker has in flight, as the steps of the batch loop see
 * them, updated in place (rule 46).
 *
 * A walk is a slot of the pool. The slot's ray is the one ray its walk
 * waits on; extendRays() writes the slot's hit and marks it answered.
 * Everything else about a walk is kept beside the pool, by slot.
 *
 * The walks of every pass share the phases. A light path of the light
 * pass is Leading, as an eye walk is while its light subpath is
 * walked, and Done when that ends. A ray of the surface pass is
 * Probing: the transport leaves its answer to the pass's fold.
 */
struct Pool {
	/**
	 * How many slots.
	 */
	int capacity;
	/**
	 * Where each slot's walk is; Free for a slot that holds none.
	 */
	std::vector<WalkPhase> phases;
	/**
	 * Whether each slot's ray has its hit, which its walk has not taken
	 * yet: 1 or 0.
	 */
	std::vector<std::uint8_t> answered;
	/**
	 * What each slot's ray is asked.
	 */
	std::vector<RayKind> kinds;
	/**
	 * The ray each slot's walk waits on.
	 */
	std::vector<Ray> rays;
	/**
	 * The hit of each answered slot's ray.
	 */
	std::vector<Hit> hits;
	/**
	 * The sample each slot's walk was started from (the pixel pass).
	 */
	std::vector<PixelSample> samples;
};

/**
 * Return a pool whose slots are all free.
 *
 * @param capacity How many slots.
 */
Pool emptyPool(int capacity);

/**
 * Return a ray kind's name in lower case, as `shadow`.
 *
 * @param kind The kind.
 */
std::string rayKindName(RayKind kind);

/**
 * Return whether a walk in that phase waits on a ray: Leading, Flying,
 * Lighting or Probing.
 *
 * @param phase The phase.
 */
bool waitsOnRay(WalkPhase phase);

/**
 * Return what a camera ray that met nothing leaves as its first surface.
 */
Surface missedSurface();

} // namespace kodanuki
