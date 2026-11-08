#pragma once

#include "extras/build_correct_cut_long_programs/cut_long_programs.h"
#include "extras/build_correct_face_slabs/face_slabs.h"
#include "extras/build_correct_faced_hits/faced_hits.h"
#include "extras/build_correct_shared_coefficients/shared_coefficients.h"
#include "extras/build_correct_shared_subtrees/shared_subtrees.h"
#include "extras/build_correct_split_beside_finer/split_beside_finer.h"
#include "tree/cut.h"
#include "tree/grow.h"
#include "tree/tree.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * A pass that rewrites a finished subtree: the extension point Build
 * pass.
 *
 * `--use passes=...` picks the members; they run in this order. Every
 * pass leaves each hit within the tolerance, and the compression
 * passes and the face slabs leave every hit the same to the bit.
 */
enum class PassMember : std::uint8_t {
	/**
	 * Splits a leaf fitted to its whole field with a neighbour more than
	 * one level finer where the fit strays at a 162-point grid
	 * (extras/build_correct_split_beside_finer).
	 */
	SplitBesideFiner,
	/**
	 * Stores a leaf's object programs cut to its cell where the cut
	 * keeps a share of the steps or fewer (extras/build_correct_cut_long_programs).
	 */
	CutLongPrograms,
	/**
	 * Keeps a slab after every face that is not a plane, which the leaf's
	 * solve asks before it solves the face
	 * (extras/build_correct_face_slabs). It is the last grown member,
	 * so the cells the passes before it grow again keep slabs too.
	 */
	FaceSlabs,
	/**
	 * A face run equal to one the tree holds names that run instead
	 * (extras/build_correct_shared_coefficients).
	 */
	SharedCoefficients,
	/**
	 * A packed node equal to one the tree holds becomes a copy of it,
	 * and its subtree is dropped (extras/build_correct_shared_subtrees).
	 */
	SharedSubtrees,
};

/**
 * When a pass runs.
 */
enum class PassMoment : std::uint8_t {
	/**
	 * In the subtree's worker, on the grown octree, before it is packed.
	 */
	Grown,
	/**
	 * In the order subtrees were handed out, on the packed subtree once
	 * its indices are the tree's, before it is written into the tree.
	 */
	Placed,
};

/**
 * The typed state of every pass that keeps state.
 *
 * It lives in the BuildContext and is touched only at the placed
 * moment, which runs one subtree at a time.
 */
struct PassState {
	/**
	 * The index over the tree's face runs.
	 */
	SharedRuns coefficients;
	/**
	 * The index over the tree's packed nodes.
	 */
	SharedNodes subtrees;
};

/**
 * What a pass reads.
 */
struct PassQuery {
	/**
	 * What every cell reads, for passes that grow cells again.
	 */
	const GrowContext& context;
	/**
	 * The subtree to rewrite.
	 */
	const Subtree& subtree;
	/**
	 * The programs the subtree's root cuts, read at the grown moment;
	 * the placed moment is asked with none.
	 */
	const CellPrograms& programs;
	/**
	 * The tree so far, read at the placed moment; the grown moment is
	 * asked with an empty tree.
	 */
	const Tree& tree;
};

/**
 * Return when a member runs.
 *
 * @param member The member.
 */
PassMoment passMoment(PassMember member);

/**
 * Run one pass over a subtree: the point's one dispatch.
 *
 * A grown pass does not touch the state, since workers run it at once;
 * each worker asks it with an empty state of its own.
 *
 * A mutable reference, as rule 46 allows for build-time accumulators.
 *
 * @param member The member.
 * @param query The subtree and what the pass reads.
 * @param state The state of the passes that keep it.
 */
Subtree passSubtree(PassMember member, const PassQuery& query, PassState& state);

/**
 * Return the empty state the build starts with.
 */
PassState emptyPassState();

/**
 * Return a leg through a leaf's cell as the faces' per-face data is
 * asked about it, once for each leaf a ray solves.
 *
 * A run that is not slabbed, or a build with the face slabs off, gets a
 * leg that is never answered.
 *
 * @param header The leaf's run header.
 * @param base Where the leg enters, in the cell's coordinates.
 * @param rate How far it moves across the cell.
 */
SlabLeg passLeg(const RunHeader& header, const float* base, const float* rate);

/**
 * Return which side of 0 one face keeps to along a whole leg, by what
 * the Build pass keeps after it: 1 outside, -1 inside, 0 where it
 * cannot tell and the face must be solved.
 *
 * Asked only for a face that is not a plane of a slabbed run.
 *
 * @param leg The leg, as passLeg() returns it.
 * @param slab The first word the pass keeps after the face.
 */
int passFaceSide(const SlabLeg& leg, const std::uint32_t* slab);

/**
 * Return the members that are on unless `--use passes=` says
 * otherwise: all five.
 */
/**
 * The owner of a face no one program of its leaf's list gives alone.
 */
constexpr std::uint32_t noFaceOwner = 0xFu;

/**
 * What the Build pass members know of the face a ray landed on.
 */
struct LandedFace {
	/**
	 * Whether the face is the field's own surface and paints one scene
	 * leaf, so a hit on it takes its gradient and leaf and evaluates no
	 * program.
	 */
	bool exact;
	/**
	 * The face's gradient at the landing, in the leaf's unit coordinates,
	 * where it is exact and a point was given.
	 */
	std::array<float, 3> gradient;
	/**
	 * The scene leaf a hit on an exact face paints.
	 */
	std::uint32_t leaf;
	/**
	 * The program of the leaf's list a hit on the face evaluates alone,
	 * as its index in the list, or noFaceOwner.
	 */
	std::uint32_t owner;
};

/**
 * Return the words the Build pass members write after a run's table
 * (RunHeader::passWords counts them): build_correct_faced_hits' owners
 * and exact faces.
 *
 * @param query The run's faces, its cell and the cell's cut programs.
 */
std::vector<std::uint32_t> passRunWords(const RunQuery& query);

/**
 * Return a run's pass words once its faces are folded to those kept.
 *
 * @param words The run's pass words.
 * @param count How many there are.
 * @param kept The faces kept, face j's bit 1 << j.
 */
std::vector<std::uint32_t> passFoldedWords(const std::uint32_t* words, std::uint32_t count, std::uint32_t kept);

/**
 * Return what the pass words of a surface leaf's run say of the face a
 * ray landed on.
 *
 * @param run The run, from its header word.
 * @param face The face, or -1 where no one face was landed on.
 * @param unit The landing in the leaf's unit coordinates, or nullptr
 *        where the gradient is not wanted.
 */
LandedFace passLandedFace(const std::uint32_t* run, int face, const float* unit);

/**
 * Return whether the field just past a passing that stays in its cell
 * is read from the cell's cut programs, which equal the whole programs
 * there, instead of the whole programs.
 */
bool passReadsCellPast();

std::vector<PassMember> defaultPassMembers();

/**
 * Return the member a `--use passes=` name names, or nothing for an
 * unknown name.
 *
 * @param name The member's name: split_beside_finer,
 *        cut_long_programs, face_slabs, shared_coefficients or
 *        shared_subtrees.
 */
std::optional<PassMember> passMemberNamed(const std::string& name);

}
