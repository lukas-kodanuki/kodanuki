#pragma once

#include "core/batch.h"
#include "sdf/scene.h"

namespace kodanuki {

/**
 * A box in world space that the world lies in.
 *
 * The tree's own search finds a cube; a check may give any box, whose
 * cells are then boxes of the same shape.
 */
struct Region {
	/**
	 * The corner with the least x, y and z.
	 */
	double lower[3];
	/**
	 * The length of the box along x, y and z.
	 */
	double size[3];
};

/**
 * The world rays meet, as the Geometry plugin built it (buildWorld):
 * what the core and every plugin read of it.
 *
 * The kernel finds one ray's hit. A plugin that asks more of the world
 * than one ray's hit, as the packet walk of `execution/packets`, reads
 * the Geometry plugin's own world and names that plugin among those it
 * needs (Hooks.needed).
 */
struct Geometry {
	/**
	 * The scene's geometry.
	 */
	const Scene& scene;
	/**
	 * The box the world lies in: a ray meets nothing outside it.
	 */
	Region region;
	/**
	 * The Geometry plugin's world, which only that plugin's accessor
	 * casts.
	 */
	const void* world;
	/**
	 * The Geometry plugin's kernel for one ray: its first hit.
	 */
	Hit (*kernel)(const Geometry& geometry, const Ray& ray);
};

/**
 * Find the first surface along a ray, by the Geometry plugin's kernel.
 *
 * @param geometry The world.
 * @param ray The ray.
 */
Hit intersect(const Geometry& geometry, const Ray& ray);

/**
 * Return the hit of a ray that met nothing.
 */
Hit missedHit();

} // namespace kodanuki
