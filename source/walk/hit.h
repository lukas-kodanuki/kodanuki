#pragma once

#include "sdf/program.h"
#include "tree/tree.h"
#include "walk/walk.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * The field of a leaf at one point, and where its value comes from.
 */
struct Sourced {
	/**
	 * The field's value.
	 */
	double value;
	/**
	 * The field's gradient.
	 */
	double gradient[3];
	/**
	 * The scene leaf whose value decides the field, or noLeaf.
	 */
	std::uint32_t leaf;
	/**
	 * The leaf across a smooth seam, or noLeaf.
	 */
	std::uint32_t other;
	/**
	 * How much of the other leaf the point is made of.
	 */
	double share;
};

/**
 * A point a ray met, before it becomes a hit.
 */
struct HitQuery {
	/**
	 * The tree and the scene.
	 */
	const Geometry& geometry;
	/**
	 * The ray.
	 */
	const Ray& ray;
	/**
	 * How far along the ray.
	 */
	double distance;
	/**
	 * How far past the point along the ray the field is read again where
	 * programs tie: a thousandth of the smallest side of the leaf met,
	 * or of the ray's way through it at a passing.
	 */
	double inset;
};

/**
 * Where a leaf's field is read.
 */
struct FieldQuery {
	/**
	 * The tree and the scene.
	 */
	const Geometry& geometry;
	/**
	 * The leaf's program list, in Tree::lists.
	 */
	std::uint32_t list;
	/**
	 * The point.
	 */
	const double* point;
	/**
	 * The direction the ray travels in, along which a tie is broken.
	 */
	math::Vector toward;
	/**
	 * How far along it a tie is read again.
	 */
	double inset;
	/**
	 * Whether the point may lie outside the leaf's cell, so the field
	 * is read from the objects' whole programs rather than their cuts
	 * to the cell.
	 */
	bool beyond;
};

/**
 * Evaluate a leaf's programs at a point: one SDF evaluation.
 *
 * Each program is run once in duals and the least value wins. Programs
 * whose values tie with the least to the rounding of the point's
 * coordinates are coplanar there, and the tie goes to the least of them
 * a little further along the ray, the solid the ray enters, the first
 * on a tie again. The winner's own values say where it comes from
 * (sourceOf): the leaf, the leaf across a smooth seam and its share.
 *
 * @param query The leaf's programs, the point and the ray's direction.
 */
Sourced surfaceAt(const FieldQuery& query);

/**
 * A surface leaf and its field read at a point.
 */
struct LeafField {
	/**
	 * The surface leaf, in Tree::surfaces.
	 */
	std::uint32_t surface;
	/**
	 * Its field at the point.
	 */
	const Sourced& field;
};

/**
 * A surface leaf and the program of its list that is nearest at a point.
 */
struct NearestProgram {
	/**
	 * The surface leaf, in Tree::surfaces.
	 */
	std::uint32_t surface;
	/**
	 * The program, as its place in the leaf's list.
	 */
	std::uint32_t program;
};

/**
 * Return a list's program at a place in it, cut to the leaf's cell, or
 * the whole object's program where the query reads beyond the cell.
 *
 * @param query The leaf's programs.
 * @param k The place in the list.
 */
KeptSteps listedProgram(const FieldQuery& query, std::uint32_t k);

/**
 * Return the program of a list that is nearest at a point, given each
 * program's value there, as surfaceAt() chooses it: the least value,
 * ties within the point's rounding going to the least a little further
 * along the ray.
 *
 * @param query The leaf's programs, the point and the ray's direction.
 * @param values Each program's value at the point, in the list's order.
 */
std::uint32_t nearestOfValues(const FieldQuery& query, const std::vector<double>& values);

/**
 * Return a ray's point at a distance, in double.
 *
 * @param query The ray and the distance.
 */
std::array<double, 3> pointAlong(const HitQuery& query);

/**
 * Return where a leaf's field is read at a point of a ray.
 *
 * @param query The ray and the inset.
 * @param list The leaf's program list, in Tree::lists.
 * @param point The point.
 */
FieldQuery fieldQuery(const HitQuery& query, std::uint32_t list, const std::array<double, 3>& point);

/**
 * Build a hit on a surface leaf from its field read at the ray's point:
 * the normal faced as surfaceHit() faces it, and the leaf's leaf, kind
 * and loosening.
 *
 * @param query The point met.
 * @param read The surface leaf and its field there.
 */
Hit leafHit(const HitQuery& query, const LeafField& read);

/**
 * Build a hit on a surface leaf whose nearest program is known: what
 * surfaceHit() builds when its programs' values choose that program.
 *
 * @param query The point met.
 * @param nearest The surface leaf and its nearest program.
 */
Hit nearestSurfaceHit(const HitQuery& query, const NearestProgram& nearest);

/**
 * Build a hit on a surface leaf.
 *
 * The point is the ray's at the distance, in double. The leaf's
 * programs are evaluated there once: the normal is their gradient, and
 * the leaf is the one their value comes from at the point itself, so a
 * thin solid in a large cell keeps its own leaf. A ray from outside
 * that meets a patch near a silhouette may find the gradient facing
 * away from it, the patch standing off the surface within its
 * tolerance; the normal is then bent toward the ray until it faces it
 * by a small margin, rather than turned round into the solid, so the
 * point is shaded and left on the open side.
 *
 * @param query The point met.
 * @param surface The surface leaf, in Tree::surfaces.
 */
Hit surfaceHit(const HitQuery& query, std::uint32_t surface);

/**
 * Read a leaf's field a little past a point along a ray.
 *
 * The leaf's programs are evaluated once at the point moved on by the
 * inset, so the value comes from the solid the ray passes into.
 *
 * @param query The point and how far past it to read.
 * @param surface The surface leaf, in Tree::surfaces.
 */
Sourced passedInto(const HitQuery& query, std::uint32_t surface);

/**
 * Read a leaf's field a little past a point that stays in the leaf's
 * cell, from the cell's cut programs; it equals passedInto() there.
 *
 * @param query The point and how far past it to read.
 * @param surface The surface leaf, in Tree::surfaces.
 */
Sourced passedWithin(const HitQuery& query, std::uint32_t surface);

/**
 * Build a hit where a ray from inside passes into a solid of another
 * material.
 *
 * The point is the ray's at the distance; the normal and the leaf are
 * those of the solid just past it (passedInto).
 *
 * @param query The point met and how far past it to read.
 * @param surface The surface leaf, in Tree::surfaces.
 * @param within Whether the point past it stays in the leaf's cell, so
 *        the cell's cut programs read it (passedWithin).
 */
Hit passingHit(const HitQuery& query, std::uint32_t surface, bool within);

/**
 * Build a hit where a ray enters a filled leaf.
 *
 * The normal is the side entered, or the reversed ray where it entered
 * through none; the leaf is the one the programs' value comes from at
 * the point.
 *
 * @param query The point met.
 * @param solid The filled leaf, in Tree::solids.
 * @param entered The side entered, as its axis times two plus one for
 *        the upper side, or -1 for none.
 */
Hit solidHit(const HitQuery& query, std::uint32_t solid, int entered);

/**
 * Return the hit of a ray that met nothing.
 */
Hit missedHit();

}
