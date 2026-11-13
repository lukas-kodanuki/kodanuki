#pragma once

#include "core/plugin.h"

#include <cstdint>
#include <string>

namespace kodanuki {

/**
 * What a run is busy with, for its progress line.
 */
enum class Stage : std::uint8_t {
	/**
	 * Building the tree.
	 */
	Tree,
	/**
	 * Drawing: the preparations until the first tile pass begins, then the
	 * tiles.
	 */
	Frame,
};

/**
 * What the progress line shows: one look at what the observer was told,
 * and the times the drawing thread saw each part begin.
 */
struct Snapshot {
	/**
	 * What the run is busy with.
	 */
	Stage stage;
	/**
	 * How many tiles are finished.
	 */
	int finished;
	/**
	 * How many tiles the passes begun so far draw.
	 */
	int total;
	/**
	 * How many frames of a sequence are finished.
	 */
	int frame;
	/**
	 * How many frames the sequence draws; 0 for a still frame.
	 */
	int frames;
	/**
	 * The seconds since the stage began.
	 */
	double staged;
	/**
	 * The seconds since the first tile pass began, or for a sequence
	 * since its first frame began; 0 before.
	 */
	double drawing;
	/**
	 * The seconds left, estimated from the tiles (or frames) finished in
	 * the last ten seconds, or from all of them where none finished
	 * since; negative while nothing is finished.
	 */
	double left;
};

/**
 * Return the progress line of one look at a run: the tree's or the
 * preparations' running time, or a bar of finished tiles (or frames of a
 * sequence) with the share done, the time so far and the time left.
 *
 * @param snapshot The look.
 */
std::string progressLine(const Snapshot& snapshot);

/**
 * Return the plugin `observer/progress` as the core lists it: where
 * stderr is a terminal, a thread of its own draws the progress line
 * about four times a second from the steps it is told of (the tree
 * built, the passes begun, the tiles placed, the frames drawn), and
 * clears it when the renderer closes.
 */
Plugin observerProgressPlugin();

} // namespace kodanuki
