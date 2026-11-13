#pragma once

#include "plugins/geometry/tree/classify.h"
#include "plugins/geometry/tree/cut.h"
#include "sdf/scene.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace kodanuki {

/**
 * Each leaf's own program over world space, compiled the first time a
 * cell asks about the leaf.
 *
 * On city about half the leaves are never asked about, and the programs
 * of all of them would take some 50 MB.
 */
struct LeafPrograms {
	/**
	 * The scene the leaves are compiled from, which outlives the build.
	 */
	const Scene& scene;
	/**
	 * Each leaf's program, matching Scene::leaves; null until it is first
	 * asked for. Workers that ask at once may each compile it, and the
	 * first to finish keeps its program, the same as the others'.
	 */
	std::vector<std::atomic<std::shared_ptr<const Program>>> programs;
};

/**
 * What the split hidden primitives member reads: every leaf of the
 * scene alone, and where it stands.
 */
struct HiddenPrimitives {
	/**
	 * Each leaf's own program, shared by every copy of the detail; null
	 * where the member is off.
	 */
	std::shared_ptr<LeafPrograms> leaves;
	/**
	 * Where each leaf's placement puts its origin, matching
	 * Scene::leaves; empty where the member is off.
	 */
	std::vector<std::array<double, 3>> places;
};

/**
 * One fitted cell that the member is asked about.
 */
struct HiddenQuery {
	/**
	 * The cell's plan, which holds its cut programs and its run.
	 */
	const Plan& plan;
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * How far the cell's surface may stray.
	 */
	double tolerance;
};

/**
 * Return every leaf of a scene alone, for the member: where each stands,
 * and room for its program, compiled when a cell first asks about it.
 *
 * @param scene The scene.
 */
HiddenPrimitives hiddenPrimitives(const Scene& scene);

/**
 * Return the member's state where it is off: no leaves.
 */
HiddenPrimitives noHiddenPrimitives();

/**
 * Return whether a leaf could stand in a fitted cell without the fit
 * having followed it.
 *
 * A fit reads the field at the twenty nodes and the fifteen points its
 * stray is read at, and a small shape standing between them all,
 * beside a shape the fit follows exactly, leaves a fit that strays
 * nowhere and holds nothing of it. Only a cell fitted to its whole
 * field, one patch or a seam pair, whose cut programs hold two leaves
 * or more is asked. Every leaf of those programs whose own interval
 * over the cell holds 0 is read at the 35 points, and one that comes
 * out on one side at every one of them hides. So does one read on both
 * sides where the fit strays further than the tolerance at the point
 * of the cell nearest where the leaf stands, a sixteenth of the cell in
 * from it toward the middle, so that it is no point the fit is exact
 * at.
 *
 * @param hidden Every leaf of the scene alone.
 * @param query The cell, its plan and its tolerance.
 */
bool hidesLeaf(const HiddenPrimitives& hidden, const HiddenQuery& query);

}
