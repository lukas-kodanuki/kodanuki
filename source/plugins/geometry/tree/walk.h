#pragma once

#include "core/geometry.h"
#include "math/vector.h"
#include "plugins/geometry/tree/tree.h"
#include "sdf/scene.h"

#include <cstdint>

namespace kodanuki {

/**
 * Where a walk stopped, before the hit is built.
 */
enum class MetKind : std::uint8_t {
	/**
	 * Nowhere: the ray went through.
	 */
	Missed,
	/**
	 * On the surface inside a surface leaf.
	 */
	Surface,
	/**
	 * Inside a surface leaf, where a ray from inside passes into a solid
	 * of another material.
	 */
	Passing,
	/**
	 * On the side of a filled leaf.
	 */
	Solid,
};

/**
 * Where a walk stopped and what the hit is built from.
 */
struct Met {
	/**
	 * What it stopped on.
	 */
	MetKind kind;
	/**
	 * How far along the ray.
	 */
	double distance;
	/**
	 * The surface leaf or the filled leaf stopped in.
	 */
	std::uint32_t payload;
	/**
	 * The side of a filled leaf entered, as its axis times two plus one
	 * for the upper side, or -1 for none; at a passing, 1 where the point
	 * past it stays in the cell, so the cell's cut programs may read it.
	 */
	int entered;
	/**
	 * The smallest side of the cell stopped in, measured along the ray,
	 * or the ray's way through the cell at a passing.
	 */
	double side;
	/**
	 * The face of the surface leaf the ray landed on, or -1 where no
	 * one face did (or the walk stopped on anything but a surface).
	 */
	int face;
	/**
	 * Where it landed in the leaf's unit coordinates, for the face's
	 * normal.
	 */
	float unit[3];
};

/**
 * Return the world of a tree: the scene, the tree's region, the tree
 * as the plugin's world and firstHit() as the kernel.
 *
 * @param scene The scene the tree's programs are in.
 * @param tree The packed tree, which the world points at.
 */
Geometry treeGeometry(const Scene& scene, const Tree& tree);

/**
 * Return the tree of a world treeGeometry() made: the one place that
 * casts the world.
 *
 * @param geometry The world.
 */
const Tree& treeOf(const Geometry& geometry);

/**
 * Return whether a world is a tree's, so that treeOf() may read it.
 *
 * @param geometry The world.
 */
bool holdsTree(const Geometry& geometry);

/**
 * Find the first surface along a ray: the tree's kernel for one ray.
 *
 * The walk takes one ray at a time, in single precision and in the
 * order of operations the packet walk keeps, so both meet the same
 * bits. A cell is the ray's parameters at its near and far plane on
 * each axis, and a child's planes are the middles of its parent's. The
 * ray is clipped to the region, then each packed node's busy coarse
 * children are walked in the order the ray meets them, and inside each
 * its octants likewise. A filled leaf ends a ray from outside at its
 * side, a surface leaf is solved in its own coordinates (plugins/geometry/tree/leaf.h),
 * a branch is walked in turn, and an empty leaf is passed. A ray from
 * inside also ends at a passing of a surface leaf where the solid just
 * past it, read from the leaf's programs, is of another material than
 * the one it travels in. The first
 * hit ends the walk, and the leaf's programs are evaluated once at the
 * point (plugins/geometry/tree/hit.h).
 *
 * @param geometry The tree and the scene.
 * @param ray The ray.
 */
Hit firstHit(const Geometry& geometry, const Ray& ray);

/**
 * Walk a ray to its first stop, without building the hit.
 *
 * It is the walk firstHit() runs, and the stop it builds its hit from.
 *
 * @param geometry The tree and the scene.
 * @param ray The ray.
 */
Met firstMet(const Geometry& geometry, const Ray& ray);

}
