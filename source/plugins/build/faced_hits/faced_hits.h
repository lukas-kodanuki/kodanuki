#pragma once

#include "plugins/geometry/tree/classify.h"
#include "plugins/geometry/tree/tree.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * Whether the build writes faced hits: false when the build leaves
 * this plugin out (CMake's `-DOFF`), and every hit then evaluates its
 * leaf's programs. Read once at start.
 */
extern const bool facedHitsBuilt;

/**
 * What the faced hits know of one face of a run.
 */
struct FacedFace {
	/**
	 * Whether the face is the field's own surface (a plane, a quadric of
	 * the square-root rule, a true quadric, a ring) and paints one scene
	 * leaf, so its gradient is the hit's normal.
	 */
	bool exact;
	/**
	 * The scene leaf a hit on an exact face paints.
	 */
	std::uint32_t leaf;
	/**
	 * The program of the leaf's list the face comes from alone, as its
	 * index in the list, or 0xF where no one program gives it.
	 */
	std::uint32_t owner;
};

/**
 * Return the words the faced hits write after a run's table: a word of
 * owners (four bits a face), then up to three words of exact faces (an
 * 8-bit mask and the 24-bit scene leaf they paint). The leaf of a face is
 * read by the field's own source rule at points of the face that lie on
 * the cell's surface; faces whose leaf is not one (pieces of several
 * leaves on one side, merged faces, duplicated surfaces, ties that are
 * not hidden contact) are left to the field. Nothing is written where
 * nothing is known.
 *
 * @param query The run's faces, its cell and the cell's cut programs.
 */
std::vector<std::uint32_t> facedWords(const RunQuery& query);

/**
 * Return the faced words of a run whose faces are folded to those kept.
 *
 * @param words The run's faced words.
 * @param count How many there are.
 * @param kept The faces kept, face j's bit 1 << j.
 */
std::vector<std::uint32_t> foldedFacedWords(const std::uint32_t* words, std::uint32_t count, std::uint32_t kept);

/**
 * Read what the faced words of a run say of one face.
 *
 * @param run The run, from its header word.
 * @param face The face.
 */
FacedFace facedFace(const std::uint32_t* run, std::uint32_t face);

/**
 * Return a plane's, quadric's or ring's gradient at a point in the leaf's
 * unit coordinates.
 *
 * @param run The run, from its header word.
 * @param face The face.
 * @param unit The point.
 */
std::array<float, 3> facedGradient(const std::uint32_t* run, std::uint32_t face, const float* unit);

}
