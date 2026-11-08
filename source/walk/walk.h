#pragma once

#include "math/vector.h"
#include "sdf/scene.h"
#include "tree/tree.h"

#include <cstdint>

namespace kodanuki {

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
 * A ray in world space.
 */
struct Ray {
	/**
	 * Where the ray starts.
	 */
	math::Vector origin;
	/**
	 * The unit direction it travels in.
	 */
	math::Vector direction;
	/**
	 * How far along it a hit counts.
	 */
	double far;
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
 * Where a ray met the geometry.
 */
struct Hit {
	/**
	 * What it met.
	 */
	HitKind kind;
	/**
	 * How far along the ray.
	 */
	double distance;
	/**
	 * The point met.
	 */
	math::Vector point;
	/**
	 * The unit normal pointing out of the inside: the field's gradient
	 * for a surface, the side entered for a solid. It is turned to face
	 * the ray's origin for a ray from outside, and away from it for a
	 * ray from inside, where the gradient says otherwise.
	 */
	math::Vector normal;
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
	double share;
	/**
	 * How far the surface, or a patch beside it, may stand from the
	 * field here: the leaf's tolerance.
	 */
	double loosened;
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
 * What the walk reads: the tree, and the scene its programs are in.
 */
struct Geometry {
	/**
	 * The scene's geometry.
	 */
	const Scene& scene;
	/**
	 * The packed tree.
	 */
	const Tree& tree;
};

/**
 * Find the first surface along a ray.
 *
 * The walk takes one ray at a time, in single precision and in the
 * order of operations the packet walk keeps, so both meet the same
 * bits. A cell is the ray's parameters at its near and far plane on
 * each axis, and a child's planes are the middles of its parent's. The
 * ray is clipped to the region, then each packed node's busy coarse
 * children are walked in the order the ray meets them, and inside each
 * its octants likewise. A filled leaf ends a ray from outside at its
 * side, a surface leaf is solved in its own coordinates (walk/leaf.h),
 * a branch is walked in turn, and an empty leaf is passed. A ray from
 * inside also ends at a passing of a surface leaf where the solid just
 * past it, read from the leaf's programs, is of another material than
 * the one it travels in. The first
 * hit ends the walk, and the leaf's programs are evaluated once at the
 * point (walk/hit.h).
 *
 * @param geometry The tree and the scene.
 * @param ray The ray.
 */
Hit intersect(const Geometry& geometry, const Ray& ray);

/**
 * Walk a ray to its first stop, without building the hit.
 *
 * It is the walk intersect() runs, and the stop it builds its hit from.
 *
 * @param geometry The tree and the scene.
 * @param ray The ray.
 */
Met firstMet(const Geometry& geometry, const Ray& ray);

}
