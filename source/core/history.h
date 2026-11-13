#pragma once

#include "core/camera.h"
#include "core/shown.h"
#include "core/state.h"
#include "core/stopping.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What changed since the frame before, as flags that combine.
 */
enum class Change : std::uint8_t {
	/**
	 * Nothing: the frame continues the one before.
	 */
	None = 0,
	/**
	 * The camera moved.
	 */
	Moved = 1,
	/**
	 * The sequence starts over: its first frame, a cut of the clip, or a
	 * new frame size.
	 */
	Cut = 2,
	/**
	 * The lights, the sky, the clouds or the stars changed.
	 */
	Lights = 4,
	/**
	 * The materials, coats or groups changed.
	 */
	Looks = 8,
	/**
	 * The shapes changed: their programs, places or count.
	 */
	Shapes = 16,
};

/**
 * Where a frame stands in a sequence of frames.
 */
struct FrameStep {
	/**
	 * The frame's place in the sequence, from 0.
	 */
	std::uint32_t index;
	/**
	 * The lens the frame draws through, at the drawn size.
	 */
	Lens lens;
	/**
	 * The lens of the frame before, the frame's own for the first.
	 */
	Lens before;
	/**
	 * How the drawn pixels cover the shown ones.
	 */
	FilmPlan film;
	/**
	 * What changed since the frame before.
	 */
	Change change;
	/**
	 * How many frames in a row, this one included, nothing changed in;
	 * zero where something did.
	 */
	std::uint32_t still;
	/**
	 * Whether a frame whose camera only moved keeps the guides of the
	 * frame before (`sampler/highlight_guide` reads it) instead of
	 * surveying again; the other Camera preparations run again.
	 */
	bool keepsGuides;
};

/**
 * What the next frame of a sequence is drawn with, as its driver knows
 * it.
 */
struct StepQuery {
	/**
	 * The lens to draw through.
	 */
	const Lens& lens;
	/**
	 * How the drawn pixels cover the shown ones.
	 */
	FilmPlan film;
	/**
	 * What the driver knows changed beyond the camera: Lights, Looks or
	 * Shapes from comparing the scene with the frame before's, Cut to
	 * start over; None otherwise.
	 */
	Change change;
	/**
	 * Whether a frame whose camera only moved keeps the guides of the
	 * frame before. A guide only steers how directions are drawn, so the
	 * mean holds; a driver that moves often (the viewer) keeps them while
	 * moving and lets them refresh once the camera holds still. Offline
	 * tracks keep none.
	 */
	bool keepsGuides;
};

/**
 * Everything a frame leaves the next: a plain value the driver owns and
 * hands back.
 */
struct History {
	/**
	 * The last frame's record; its light and surfaces are empty where no
	 * chosen Reconstruction member reads them.
	 */
	FrameRecord record;
	/**
	 * What each pixel's samples since the last change add up to, by rows
	 * from the top left: Mean's state, which the next frame's samples
	 * continue where nothing changes. Empty before the first frame.
	 */
	std::vector<PixelSum> estimates;
	/**
	 * The states the plugins keep from frame to frame: each
	 * preparation's tables, kept while nothing it reads changes, and each
	 * Reconstruction plugin's history.
	 */
	States states;
};

/**
 * Return the flags of two changes together.
 *
 * @param a One change.
 * @param b The other.
 */
Change joinedChange(Change a, Change b);

/**
 * Return whether a change holds a flag.
 *
 * @param change The change.
 * @param flag The flag.
 */
bool changedBy(Change change, Change flag);

/**
 * Return the step of a sequence's first frame: index 0, a Cut, nothing
 * still.
 *
 * @param query The lens, the film plan and what else changed.
 */
FrameStep firstStep(const StepQuery& query);

/**
 * Return the step after the last one.
 *
 * The index is one past the last. The change holds the query's, Moved
 * where any of the lens's fields differ, and Cut where its shown size
 * (the drawn size times the film plan's factor) does. A new factor at the
 * same shown size is a Moved change that keeps no guides, since the
 * estimates and the guides belong to the drawn pixels. A new phase alone
 * changes nothing. Where nothing changed, the count of still frames grows
 * by one.
 *
 * @param last The last frame's step.
 * @param query The lens, the film plan and what else changed.
 */
FrameStep nextStep(const FrameStep& last, const StepQuery& query);

/**
 * Return the history of a sequence that has drawn no frame.
 */
History emptyHistory();

}
