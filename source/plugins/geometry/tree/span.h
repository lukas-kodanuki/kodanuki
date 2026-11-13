#pragma once

#include "plugins/geometry/tree/hit.h"
#include "plugins/geometry/tree/leaf.h"
#include "plugins/geometry/tree/tree.h"
#include "plugins/geometry/tree/walk.h"

#include <cstdint>

namespace kodanuki {

/**
 * The smallest direction part a slope is the reciprocal of; a smaller
 * part takes the reciprocal of this with its sign.
 */
constexpr double leastPart = 1e-12;

/**
 * A ray's stretch through one cell, in single precision.
 *
 * Each axis keeps the ray's parameter at the cell's near and far plane,
 * near being the plane the ray reaches first. The ray is in the cell
 * from lower to upper.
 */
struct CellSpan {
	/**
	 * The parameter at each axis's near plane.
	 */
	float near[3];
	/**
	 * Where the ray enters the cell.
	 */
	float lower;
	/**
	 * The parameter at each axis's far plane.
	 */
	float far[3];
	/**
	 * Where the ray leaves the cell.
	 */
	float upper;
};

/**
 * The middle planes of a cell, where its children meet.
 */
struct Middles {
	/**
	 * The parameter at each axis's middle plane.
	 */
	float at[3];
};

/**
 * One ray as a walk reads it.
 */
struct Walker {
	/**
	 * The tree and the scene.
	 */
	const Geometry& geometry;
	/**
	 * The ray.
	 */
	const Ray& ray;
	/**
	 * The octant the ray runs toward, one bit an axis, set where it
	 * falls.
	 */
	int descending;
};

/**
 * A surface leaf with its face run read apart.
 */
struct SurfaceRun {
	/**
	 * The surface leaf, in Tree::surfaces.
	 */
	std::uint32_t surface;
	/**
	 * Its face run.
	 */
	const LeafRun& run;
};

/**
 * Return the larger of two floats, the second on a tie or a NaN.
 *
 * @param a The first.
 * @param b The second.
 */
float largerOf(float a, float b);

/**
 * Return the smaller of two floats, the second on a tie or a NaN.
 *
 * @param a The first.
 * @param b The second.
 */
float smallerOf(float a, float b);

/**
 * Return the octant a ray runs toward: bit k is set where its direction
 * falls along axis k.
 *
 * @param ray The ray.
 */
int descendingOf(const Ray& ray);

/**
 * Return a ray's stretch through the tree's region.
 *
 * The planes are found in double and narrowed to float, in the axis
 * order x, y, z, and the stretch starts at 0 and ends at the ray's
 * reach. A ray that misses the region has upper below lower.
 *
 * @param walker The ray.
 */
CellSpan rootSpan(const Walker& walker);

/**
 * Return the middle planes of a cell: half the sum of its near and far
 * planes on each axis.
 *
 * @param span The cell's span.
 */
Middles middlesOf(const CellSpan& span);

/**
 * Return a child's span.
 *
 * The child is the step-th in the order the ray meets them: bit k of
 * step set takes the far half along axis k. Each axis in turn narrows
 * the planes and the stretch through largerOf() and smallerOf().
 *
 * @param span The parent's span.
 * @param middles The parent's middle planes.
 * @param step The child, in the ray's order.
 */
CellSpan halvedSpan(const CellSpan& span, const Middles& middles, int step);

/**
 * Return whether a ray is in a cell for any stretch: lower below upper.
 *
 * @param span The cell's span.
 */
bool crosses(const CellSpan& span);

/**
 * Return a slot's kind and payload.
 *
 * @param node The packed node.
 * @param slot The slot, from 0 to 63.
 */
CellEntry slotEntry(const PackedNode& node, int slot);

/**
 * Walk a ray through one cell of the tree, to its first stop there.
 *
 * @param walker The ray.
 * @param entry The cell's kind and payload.
 * @param span The ray's stretch through the cell.
 */
Met walkSlot(const Walker& walker, const CellEntry& entry, const CellSpan& span);

/**
 * Return the ray's stretch through a cell in the cell's own coordinates.
 *
 * @param walker The ray.
 * @param span The cell's span.
 */
Leg legOf(const Walker& walker, const CellSpan& span);

/**
 * Return where a walk stops in a surface leaf: the leaf is solved along
 * the ray's leg (plugins/geometry/tree/leaf.h), or the ray goes through.
 *
 * @param walker The ray.
 * @param surface The surface leaf, in Tree::surfaces.
 * @param span The leaf's span.
 */
Met surfaceMet(const Walker& walker, std::uint32_t surface, const CellSpan& span);

/**
 * Return a surface leaf's face run read apart.
 *
 * @param tree The packed tree.
 * @param surface The surface leaf, in Tree::surfaces.
 */
LeafRun surfaceLeafRun(const Tree& tree, std::uint32_t surface);

/**
 * Return where a walk stops in a surface leaf whose run is read apart,
 * as surfaceMet() does.
 *
 * @param walker The ray.
 * @param leaf The surface leaf and its run.
 * @param span The leaf's span.
 */
Met surfaceRunMet(const Walker& walker, const SurfaceRun& leaf, const CellSpan& span);

/**
 * Return whether a ray from outside stops in a surface leaf whose run is
 * read apart, as surfaceRunMet() finds, without where.
 *
 * @param walker The ray, from outside.
 * @param leaf The surface leaf and its run.
 * @param span The leaf's span.
 */
bool surfaceReached(const Walker& walker, const SurfaceRun& leaf, const CellSpan& span);

/**
 * Return where a walk stops on a filled leaf: where the ray enters it.
 *
 * @param walker The ray.
 * @param solid The filled leaf, in Tree::solids.
 * @param span The leaf's span.
 */
Met solidMet(const Walker& walker, std::uint32_t solid, const CellSpan& span);

/**
 * Return the stop of a walk that met nothing.
 */
Met missedMet();

/**
 * A surface leaf a ray stops in, and its stretch there.
 */
struct LeafPlace {
	/**
	 * The tree, the scene and the ray.
	 */
	const Walker& walker;
	/**
	 * The surface leaf, in Tree::surfaces.
	 */
	std::uint32_t surface;
	/**
	 * The ray's stretch through the leaf's cell.
	 */
	const CellSpan& span;
};

/**
 * Return the stop of a leaf solve's landing in a surface leaf: the first
 * passing into another material, else the landing, else a miss.
 *
 * @param place The ray, the leaf and the stretch.
 * @param landing What the leaf solve found along the leg.
 */
Met landingMet(const LeafPlace& place, const Landing& landing);

/**
 * Return where a walk's stop is read: the ray, the distance and the
 * inset, a share of the stop's side.
 *
 * @param walker The tree, the scene and the ray.
 * @param met The stop.
 */
HitQuery hitQueryOf(const Walker& walker, const Met& met);

/**
 * Build the hit of a walk's stop: one SDF evaluation at the point
 * (plugins/geometry/tree/hit.h).
 *
 * @param walker The ray.
 * @param met Where the walk stopped.
 */
Hit hitOf(const Walker& walker, const Met& met);

}
