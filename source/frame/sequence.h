#pragma once

#include "frame/frame.h"
#include "frame/preparation.h"
#include "frame/reconstruction.h"
#include "frame/stopping.h"
#include "image/image.h"
#include "transport/camera.h"
#include "transport/trace.h"

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
	 * The sequence starts over: its first frame, or a new frame size.
	 */
	Cut = 2,
	/**
	 * The lights or the sky changed.
	 */
	Lights = 4,
	/**
	 * The scene's shapes or looks changed.
	 */
	Scene = 8,
};

/**
 * Where a frame stands in a sequence of frames.
 */
struct FrameStep {
	/**
	 * The frame's place in the sequence, from 0; its samples take this
	 * index.
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
	 * frame before (GuideSurvey) instead of surveying again; the other
	 * Camera preparations run again.
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
	 * What the driver knows changed beyond the camera: Lights or Scene
	 * after a reload, Cut to start over; None otherwise.
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
 * Everything a sequence carries from one frame to the next: a plain
 * value the driver owns and hands back.
 */
struct History {
	/**
	 * The last frame's record.
	 */
	FrameRecord record;
	/**
	 * Each pixel's estimate since the last change, by rows from the top
	 * left: Mean's state. Empty before the first frame.
	 */
	std::vector<Estimate> estimates;
	/**
	 * What the preparations that read only the scene (Load) left, kept
	 * while the scene and the lights hold.
	 */
	Prepared kept;
	/**
	 * What every preparation left for the last frame, kept while nothing
	 * changes.
	 */
	Prepared prepared;
	/**
	 * What the Reconstruction members kept after the last frame.
	 */
	ReconstructionHistory reconstruction;
};

/**
 * What a frame of a sequence is drawn from.
 */
struct SequenceInput {
	/**
	 * What any frame is drawn from; its lens is the step's.
	 */
	const FrameInput& frame;
	/**
	 * Where the frame stands in the sequence.
	 */
	const FrameStep& step;
	/**
	 * The Reconstruction members, in order.
	 */
	const std::vector<ReconstructionMember>& reconstruction;
	/**
	 * Whether the frame also traces each drawn pixel's first surface
	 * (ShownFrame.surfaces) for the members to read.
	 */
	bool surfaced;
};

/**
 * How long the parts of a frame of a sequence took, in seconds.
 */
struct StepTimes {
	/**
	 * Running or keeping the preparations, and the frame's basis.
	 */
	double preparations;
	/**
	 * Tracing every pixel's sample.
	 */
	double trace;
	/**
	 * Tracing each drawn pixel's first surface (surfacesOf()); zero
	 * where the frame traces none (SequenceInput.surfaced).
	 */
	double surfaces;
	/**
	 * Running the Reconstruction members.
	 */
	double reconstruction;
	/**
	 * Gathering the history the next frame reads.
	 */
	double keeping;
};

/**
 * A drawn frame of a sequence and the history the next frame reads.
 */
struct Stepped {
	/**
	 * The shown image, linear.
	 */
	Image image;
	/**
	 * The history after the frame.
	 */
	History history;
	/**
	 * How long its parts took.
	 */
	StepTimes times;
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

/**
 * Draw the next frame of a sequence and return its shown image and the
 * history after it.
 *
 * The frame draws one sample of every pixel, of the step's index, with
 * the step's film plan, recording what each sample left. Where nothing
 * changed, each pixel's sample continues its estimate; on any change
 * every estimate starts over. The Load preparations are kept until a
 * Cut, Lights or Scene change and the Camera ones until any change; the
 * first frame runs them all. The Reconstruction members then make the
 * shown image. A run of N frames on a still camera with Mean alone shows
 * the image renderFrame() draws with N samples of FixedCount, to the
 * byte.
 *
 * @param input The frame, the step and the Reconstruction members.
 * @param history The history after the frame before, handed over so
 *        that the frame may reuse its buffers.
 */
Stepped steppedFrame(const SequenceInput& input, History history);

}
