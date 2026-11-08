#pragma once

#include "extras/detail_correct_focus_on_view/screen.h"
#include "extras/detail_correct_pilot/pilot.h"
#include "extras/detail_correct_split_crowded_leaves/split_crowded_leaves.h"
#include "extras/detail_correct_split_hidden_primitives/split_hidden_primitives.h"
#include "sdf/scene.h"
#include "tree/classify.h"
#include "tree/cut.h"
#include "tree/tree.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * A way of saying how fine a cell must be: the extension point Detail.
 *
 * `--use detail=...` picks the members. With none, every cell is held
 * to the least tolerance.
 */
enum class DetailMember : std::uint8_t {
	/**
	 * A share of the footprint of a pixel at the cell (core). Every
	 * other tolerance member needs it.
	 */
	ByDistance,
	/**
	 * A wider share where the eye cannot see the cell
	 * (extras/detail_correct_focus_on_view).
	 */
	FocusOnView,
	/**
	 * At most a share of the narrowest footprint the pilot's paths
	 * measured in the cell, through mirrors and at shadow edges
	 * (extras/detail_correct_pilot).
	 */
	Pilot,
	/**
	 * Splits a cell fitted to its whole field whose fit missed a leaf
	 * standing between the points it reads
	 * (extras/detail_correct_split_hidden_primitives).
	 */
	SplitHiddenPrimitives,
	/**
	 * Split a surface leaf of more than a few faces while it is large
	 * against its tolerance (extras/detail_correct_split_crowded_leaves).
	 */
	SplitCrowdedLeaves,
};

/**
 * What a Detail member is asked.
 */
enum class DetailQuestion : std::uint8_t {
	/**
	 * How far a cell may stray, asked before it is classified.
	 */
	Tolerance,
	/**
	 * Whether a cell the fit kept must still split, asked after.
	 */
	Verdict,
};

/**
 * What a member says of a classified cell.
 */
enum class Verdict : std::uint8_t {
	/**
	 * The member has nothing against the plan.
	 */
	Keep,
	/**
	 * The cell must split, though its fit held.
	 */
	Split,
};

/**
 * Everything Detail's members read: the view, the shares and one
 * typed field for each extra that keeps state.
 */
struct Detail {
	/**
	 * The members that are on, in the order they are asked.
	 */
	std::vector<DetailMember> members;
	/**
	 * Where the frame is seen from.
	 */
	View view;
	/**
	 * The least any cell may stray by, in world units.
	 */
	double least;
	/**
	 * The share of a pixel's footprint a seen cell may stray by.
	 */
	double share;
	/**
	 * Where the eye can see, for the focus on view member.
	 */
	Screen screen;
	/**
	 * The widths the pilot measured, none where it is off.
	 */
	Pilot pilot;
	/**
	 * Every leaf of the scene alone, for the split hidden primitives
	 * member, none where it is off.
	 */
	HiddenPrimitives hidden;
};

/**
 * What a member is asked about one cell.
 */
struct DetailQuery {
	/**
	 * Which question is asked.
	 */
	DetailQuestion question;
	/**
	 * The cell.
	 */
	Box box;
	/**
	 * The cell's plan, read only for the verdict. The tolerance
	 * question is asked with pendingPlan().
	 */
	const Plan& plan;
	/**
	 * How far the cell may stray, read only for the verdict.
	 */
	double tolerance;
	/**
	 * Whether the cell may still split.
	 */
	bool deeper;
};

/**
 * One member's answer.
 *
 * The answers fold into one tolerance and one verdict: the tolerance is
 * the least tolerance, or more where ByDistance is on, namely the
 * largest share times the footprint over the cell, capped by the
 * smallest limit; the verdict is the first that is not Keep.
 */
struct DetailAnswer {
	/**
	 * A share of the footprint the member allows, 0 for no opinion.
	 */
	double share;
	/**
	 * A cap on the tolerance in world units, infinity for none.
	 */
	double limit;
	/**
	 * What the member says of the plan, Keep before it is classified.
	 */
	Verdict verdict;
};

/**
 * What the Detail of a frame is worked out from.
 */
struct SceneDetailQuery {
	/**
	 * The scene's geometry, for the hidden primitives.
	 */
	const Scene& scene;
	/**
	 * The scene's shot: its camera, frame size and tolerance.
	 */
	const Shot& shot;
	/**
	 * The region the tree covers.
	 */
	const Region& region;
	/**
	 * The members that are on.
	 */
	const std::vector<DetailMember>& members;
};

/**
 * Ask one member about a cell: the point's one dispatch.
 *
 * ByDistance answers here in core; every other member answers through
 * its extra.
 *
 * @param member The member.
 * @param detail What the members read.
 * @param query The cell and the question.
 */
DetailAnswer answerDetail(DetailMember member, const Detail& detail, const DetailQuery& query);

/**
 * Return how far a cell may stray, folding every member's answer.
 *
 * @param detail What the members read.
 * @param box The cell.
 */
double cellTolerance(const Detail& detail, const Box& box);

/**
 * Return how far the surface may stray at one point, as a cell of no
 * size there would: what a ray leaving a surface there starts off by.
 *
 * @param detail What the members read.
 * @param p The point.
 */
double pointTolerance(const Detail& detail, const math::Vector& p);

/**
 * Return what the members say of a classified cell: the first answer
 * that is not Keep.
 *
 * A cell that may not split is always kept.
 *
 * @param detail What the members read.
 * @param query The cell and its plan.
 */
Verdict cellVerdict(const Detail& detail, const DetailQuery& query);

/**
 * Return a plan that holds nothing, which the tolerance question is
 * asked with.
 */
Plan pendingPlan();

/**
 * Return the narrowest a pixel's footprint is anywhere in a cell.
 *
 * It is the nearest distance from the eye to the cell times the spread,
 * narrowed as in a corner of the frame, so that a cell outside the
 * frame is held as finely as the corner. It is 0 where the eye is in
 * the cell.
 *
 * @param view Where the frame is seen from.
 * @param box The cell.
 */
double footprintOver(const View& view, const Box& box);

/**
 * Return where a scene's frame is seen from.
 *
 * @param shot The scene's shot: its camera and render settings.
 */
View sceneView(const Shot& shot);

/**
 * Return the share of a pixel's footprint a seen cell may stray by,
 * from the noise a pixel may keep.
 *
 * It follows the square root of the noise, 0.08 of a pixel at 7 steps,
 * held between 0.02 and 0.25.
 *
 * @param noise The scene's render tolerance, in steps of a byte.
 */
double footprintShare(double noise);

/**
 * Return the members that are on unless `--use detail=...` says
 * otherwise: ByDistance, FocusOnView, Pilot, SplitHiddenPrimitives and
 * SplitCrowdedLeaves.
 */
std::vector<DetailMember> defaultDetailMembers();

/**
 * Return the member a `--use detail=` name names, or nothing for an
 * unknown name.
 *
 * @param name The member's name: by_distance, focus_on_view, pilot,
 *        split_hidden_primitives or split_crowded_leaves.
 */
std::optional<DetailMember> detailMemberNamed(const std::string& name);

/**
 * Return the Detail of a scene's frame over a region: its view, the
 * share from its render tolerance and a least tolerance of 1e-4 of the
 * region's longest side.
 *
 * @param query The scene, its shot, the region and the members.
 */
Detail sceneDetail(const SceneDetailQuery& query);

}
