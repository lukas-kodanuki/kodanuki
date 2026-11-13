#pragma once

#include "plugins/geometry/tree/tree.h"
#include "sdf/numbers.h"
#include "sdf/program.h"
#include "sdf/scene.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kodanuki {

/**
 * What a program's sign does over a box, as an interval evaluation
 * reads it.
 */
enum class Occupancy : std::uint8_t {
	/**
	 * Above 0 throughout the box, so the box is outside.
	 */
	Empty,
	/**
	 * Below 0 throughout the box, so the box is inside.
	 */
	Filled,
	/**
	 * The interval holds 0, so the surface may cross the box.
	 */
	Surface,
};

/**
 * An axis-aligned box in world space.
 */
struct Box {
	/**
	 * The box's extent along x, y and z.
	 */
	math::Interval axes[3];
};

/**
 * Where the frame is seen from, as far as how finely it is seen goes.
 */
struct View {
	/**
	 * Where the eye stands.
	 */
	math::Vector eye;
	/**
	 * The unit direction the eye looks along.
	 */
	math::Vector forward;
	/**
	 * The unit direction to the right of the frame.
	 */
	math::Vector right;
	/**
	 * The unit direction to the top of the frame.
	 */
	math::Vector up;
	/**
	 * How wide one pixel in the middle of the frame is at one unit from
	 * the eye.
	 */
	double spread;
	/**
	 * How much narrower a pixel in a corner of the frame is, as a
	 * share of one in the middle.
	 */
	double corner;
	/**
	 * How far the frame reaches to either side at one unit along the
	 * view.
	 */
	double wide;
	/**
	 * How far the frame reaches up and down at one unit along the view.
	 */
	double tall;
	/**
	 * Whether the lens has an aperture, so that a place outside the
	 * frame through its middle may still be seen through its rim.
	 */
	bool open;
};

/**
 * A program cut down to what it answers inside one box.
 *
 * Every crease whose sides are apart over the box is resolved, every
 * step whose interval is one number becomes that number, and what
 * nothing reads is dropped. The cut answers exactly what the whole
 * program does at every point of the box.
 */
struct Cut {
	/**
	 * The cut program, empty when the box is empty.
	 */
	Program program;
	/**
	 * The interval of the program over the box.
	 */
	math::Interval range;
	/**
	 * What the program's sign does over the box.
	 */
	Occupancy occupancy;
};

/**
 * The programs one cell is built from: one for each object that may
 * decide the cell's surface.
 *
 * The cell's field is their minimum. Each is its object's whole
 * program cut to the cell, so children cut their parent's cuts and the
 * programs shrink down the tree. A program the cut leaves as it was is
 * shared with the parent, not copied.
 */
struct CellPrograms {
	/**
	 * The objects, as indices into Scene::objectPrograms, in scene
	 * order.
	 */
	std::vector<std::uint32_t> objects;
	/**
	 * Each object's program cut to the cell, matching the objects one
	 * for one, shared with the cells that hold the same program.
	 */
	std::vector<std::shared_ptr<const Program>> programs;
};

/**
 * What cutting a cell's programs to a box found.
 */
struct CellCut {
	/**
	 * Filled where any object fills the box, empty where none reaches
	 * it, else surface.
	 */
	Occupancy occupancy;
	/**
	 * The programs that may decide the surface in the box; for a filled
	 * box, the first object that fills it alone.
	 */
	CellPrograms programs;
};

/**
 * The intervals of a program's first steps over a box, known from a
 * program that begins with the same steps.
 */
struct KnownSteps {
	/**
	 * The intervals of the other program's steps over the box.
	 */
	const std::vector<math::Interval>& intervals;
	/**
	 * How many of its first steps the program shares.
	 */
	std::uint32_t count;
};

/**
 * Every step of a program as a cut rewrites it, before what nothing
 * reads is dropped.
 */
struct Rewritten {
	/**
	 * The rewritten steps, one for each step of the program, reading
	 * the steps their operands stand for.
	 */
	std::vector<Step> steps;
	/**
	 * For each step, the step its readers read in its place: itself, or
	 * the operand a decided step reduces to.
	 */
	std::vector<std::uint32_t> alias;
};

/**
 * The rewrite of a program's first steps, known from a program that
 * begins with the same steps and was rewritten by the same intervals.
 */
struct KnownRewrite {
	/**
	 * The rewritten first steps.
	 */
	const Step* steps;
	/**
	 * The alias of each first step, or null where each is its own.
	 */
	const std::uint32_t* alias;
	/**
	 * How many first steps are known.
	 */
	std::uint32_t count;
};

/**
 * What a cut of a program starts from.
 */
struct CutFrom {
	/**
	 * The interval of each step of the program over the box.
	 */
	const std::vector<math::Interval>& intervals;
	/**
	 * The rewrite of its first steps.
	 */
	KnownRewrite known;
};

/**
 * Return the inputs that evaluate a program over a box: the box's spans
 * as the point, a zero normal and no arguments.
 *
 * @param box The box.
 */
Inputs<math::Interval> boxInputs(const Box& box);

/**
 * Return the interval of every step of a program over a box.
 *
 * @param program The program.
 * @param box The box.
 */
std::vector<math::Interval> intervalsOver(const Program& program, const Box& box);

/**
 * Cut a program to a box, with every rewrite the cut makes.
 *
 * @param program The program.
 * @param box The box.
 */
Cut cutProgram(const Program& program, const Box& box);

/**
 * Return the interval of every step of a program over a box, the first
 * ones known from a program that shares them.
 *
 * @param program The program.
 * @param box The box.
 * @param known The intervals of the steps it shares.
 */
std::vector<math::Interval> intervalsAfter(const Program& program, const Box& box, const KnownSteps& known);

/**
 * Cut a program by the intervals of its steps over a box, returning no
 * program where the box is empty.
 *
 * @param program The program.
 * @param intervals The interval of each of its steps over the box.
 */
Cut occupiedCutBy(const Program& program, const std::vector<math::Interval>& intervals);

/**
 * Cut a program to a box, returning no program where the box is empty.
 *
 * @param program The program.
 * @param box The box.
 */
Cut occupiedCut(const Program& program, const Box& box);

/**
 * Cut a program by the intervals of its steps, as occupiedCutBy does,
 * its first steps rewritten as known, writing the cut's steps into the
 * given storage.
 *
 * @param program The program.
 * @param from The intervals of its steps and the rewrite of its first
 *        ones.
 * @param storage The vector whose room the cut's steps take; its
 *        contents are dropped.
 */
Cut occupiedCutInto(const Program& program, const CutFrom& from, std::vector<Step> storage);

/**
 * Rewrite every step of a program as a cut by the intervals of its
 * steps does, before what nothing reads is dropped.
 *
 * @param program The program.
 * @param intervals The interval of each of its steps over the box.
 * @param rewritten The rewrite, filled in place; its contents are
 *        dropped.
 */
void rewriteInto(const Program& program, const std::vector<math::Interval>& intervals, Rewritten& rewritten);

/**
 * Return a pointer to a program that something else owns and keeps
 * for as long as the pointer is held, without copying it.
 *
 * @param program The program.
 */
std::shared_ptr<const Program> borrowedProgram(const Program& program);

/**
 * Return the programs of the region's root cell: every object's whole
 * program, borrowed from the scene, which outlives the build.
 *
 * @param scene The scene.
 */
CellPrograms rootPrograms(const Scene& scene);

/**
 * Cut a cell's programs to a box inside it.
 *
 * Each program is cut. An object whose cut is empty is dropped, and so
 * is one whose interval lies wholly above another's, since it can never
 * be the minimum in the box. This is the old prune of the node DAG at
 * the level of objects; the prune inside an object is the cut. A cut
 * that is the same program as its parent's keeps the parent's pointer.
 *
 * @param programs The parent cell's programs.
 * @param box The box, inside the parent cell.
 */
CellCut cutCell(const CellPrograms& programs, const Box& box);

/**
 * Return one octant of a box.
 *
 * @param box The box.
 * @param octant The octant: x in bit 0, y in bit 1, z in bit 2, set
 *        meaning the upper half.
 */
Box octantBox(const Box& box, int octant);

/**
 * Return the box a region covers.
 *
 * @param region The region.
 */
Box regionBox(const Region& region);

/**
 * Return the box of a placed leaf's cell.
 *
 * @param region The tree's region.
 * @param leaf The leaf and its cell.
 */
Box placedBox(const Region& region, const PlacedLeaf& leaf);

/**
 * Return a pixel's footprint at one point: its distance from the eye
 * times the spread, narrowed as toward a corner of the frame.
 *
 * @param view Where the frame is seen from.
 * @param p The point.
 */
double footprintAt(const View& view, const math::Vector& p);

/**
 * Return a cell's field at a point, the minimum of its programs.
 *
 * @param programs The cell's programs.
 * @param p The point.
 */
double cellField(const std::vector<Program>& programs, const math::Vector& p);

/**
 * Return a cell's field at a point, the minimum of its programs.
 *
 * @param programs The cell's programs.
 * @param p The point.
 */
double cellProgramsField(const CellPrograms& programs, const math::Vector& p);

}
