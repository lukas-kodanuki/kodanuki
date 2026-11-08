#pragma once

#include "tree/cut.h"

namespace kodanuki {

/**
 * What the focus on view member reads: where the eye can see a cell.
 *
 * It is the frame alone: a cell is unseen where it stands behind the
 * eye or wholly outside the frame and a few pixels around it.
 */
struct Screen {
	/**
	 * Where the frame is seen from.
	 */
	View view;
	/**
	 * The share of a pixel's footprint an unseen cell may stray by, 0
	 * where every cell is taken as seen.
	 */
	double unseen;
	/**
	 * How many pixels around the frame still count as inside it.
	 */
	double margin;
};

/**
 * Return the screen of a frame seen through a view.
 *
 * A lens with an aperture sees past the frame's edges through its rim,
 * so there every cell is taken as seen.
 *
 * @param view Where the frame is seen from.
 */
Screen frameScreen(const View& view);

/**
 * Return a screen that takes every cell as seen.
 */
Screen emptyScreen();

/**
 * Return whether the eye sees none of a cell: all eight corners stand
 * behind the eye, or beyond one side of the frame and its margin.
 *
 * @param screen Where the eye can see.
 * @param box The cell.
 */
bool cellBeyond(const Screen& screen, const Box& box);

/**
 * Return the share of a pixel's footprint a cell may stray by for the
 * focus on view member: the unseen share where the eye cannot see the
 * cell, else 0 for no opinion.
 *
 * @param screen Where the eye can see.
 * @param box The cell.
 */
double focusOnViewShare(const Screen& screen, const Box& box);

}
