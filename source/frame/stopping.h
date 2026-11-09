#pragma once

#include "sdf/scene.h"
#include "spectral/colour.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A way of deciding how many samples a pixel draws.
 *
 * A pixel's count never reads its own samples, so the frame's mean is
 * unbiased whatever the member.
 */
enum class StoppingMember : std::uint8_t {
	/**
	 * Every pixel draws the scene's sample count.
	 */
	FixedCount,
	/**
	 * Every pixel first draws a trial of two batches. Its count is then
	 * set from what the trials of the eight pixels around it want
	 * (extras/stopping_consistent_confidence_interval).
	 */
	ConfidenceInterval,
};

/**
 * What a pixel's samples add up to, and the sums of its batch means.
 */
struct Estimate {
	/**
	 * The colours of its samples summed, in sample order.
	 */
	Colour total;
	/**
	 * How many samples.
	 */
	int taken;
	/**
	 * The colours of the batch being drawn, summed.
	 */
	Colour batch;
	/**
	 * How many whole batches.
	 */
	int batches;
	/**
	 * The means of the whole batches, summed.
	 */
	Colour means;
	/**
	 * The squares of those means, summed.
	 */
	Colour squares;
};

/**
 * The member and what it reads.
 */
struct Stopping {
	/**
	 * The member.
	 */
	StoppingMember member;
	/**
	 * The most samples a pixel draws.
	 */
	int samples;
	/**
	 * How many samples a batch holds.
	 */
	int batch;
	/**
	 * Half the width, in steps of a byte after the grade, that a pixel's
	 * interval may span.
	 */
	double tolerance;
	/**
	 * The grade the noise is judged through.
	 */
	Grade grade;
};

/**
 * The sample counts a tolerance asks for where the scene names none.
 */
struct Effort {
	/**
	 * The most samples a pixel draws.
	 */
	int samples;
	/**
	 * How many samples a batch holds.
	 */
	int batch;
};

/**
 * Every pixel's trial, empty where there is none.
 */
struct Trial {
	/**
	 * How many samples each trial drew, zero for none.
	 */
	int samples;
	/**
	 * The frame's width.
	 */
	int width;
	/**
	 * The frame's height.
	 */
	int height;
	/**
	 * How many samples each pixel's own trial says it needs, by rows
	 * from the top left. Infinite where no count would do.
	 */
	std::vector<float> wanted;
	/**
	 * Each trial's colours summed, by rows, which its pixel's count pass
	 * continues from.
	 */
	std::vector<Colour> totals;
};

/**
 * A pixel whose trial is judged.
 */
struct TrialPixel {
	/**
	 * Its trial.
	 */
	const Estimate& estimate;
	/**
	 * What the preparations brought it directly.
	 */
	Colour prepared;
	/**
	 * The variance of what the preparations brought it, per channel.
	 */
	Colour spread;
};

/**
 * A pixel whose count is asked.
 */
struct CountQuery {
	/**
	 * The pixel's column.
	 */
	int x;
	/**
	 * The pixel's row.
	 */
	int y;
	/**
	 * Every pixel's trial.
	 */
	const Trial& trial;
};

/**
 * Return an estimate with one more sample folded in: its colour added to
 * the total and to the batch, and one more taken. A batch closes every
 * batch samples: its mean and that mean squared are added to the sums.
 *
 * Every Execution member folds a pixel's samples through this alone, in
 * index order.
 *
 * @param estimate The estimate so far.
 * @param colour The sample's colour.
 * @param batch How many samples a batch holds.
 */
Estimate foldedSample(const Estimate& estimate, const Colour& colour, int batch);

/**
 * Return the sample counts a tolerance asks for.
 *
 * One effort e from 0 to 0.66 power of 2 sets all three: the tolerance
 * is 62.5 x (0.5 / 62.5)^e, a batch holds the square of round(1 + 7e)
 * samples and the ceiling is 32 x 512^e samples, at least a batch. The
 * effort is read back from the tolerance, so a tolerance of 2 asks for
 * batches of 36 and at most 2732 samples. A tolerance of zero or less
 * reads as the effort 1 with the ceiling 8 x 512^e: batches of 64 and
 * at most 4096 samples.
 *
 * @param tolerance The noise a pixel may keep, in steps of a byte.
 */
Effort effortFor(double tolerance);

/**
 * Return an estimate of no samples.
 */
Estimate emptyEstimate();

/**
 * Return how many samples every pixel's trial draws, zero for none.
 *
 * FixedCount answers zero. ConfidenceInterval answers two batches,
 * unless the tolerance is zero or the ceiling is no more than that.
 *
 * @param stopping The member and its settings.
 */
int trialSamples(const Stopping& stopping);

/**
 * Return how many samples one pixel's own trial says it needs.
 *
 * FixedCount answers zero.
 *
 * @param stopping The member and its settings.
 * @param pixel The trial and the prepared light.
 */
float wantedSamples(const Stopping& stopping, const TrialPixel& pixel);

/**
 * Return how many samples a pixel draws in all, its trial included.
 *
 * FixedCount answers the scene's count, and so does every member where
 * no trial was drawn.
 *
 * @param stopping The member and its settings.
 * @param query The pixel and every trial.
 */
int samplesFor(const Stopping& stopping, const CountQuery& query);

}
