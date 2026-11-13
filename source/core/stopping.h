#pragma once

#include "math/colour.h"
#include "sdf/scene.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

struct Plugin;

/**
 * The most samples that may share a first vertex.
 */
constexpr int mostSplit = 8;

/**
 * One sample as an Execution member folds it.
 */
struct Sampled {
	/**
	 * The sample's colour.
	 */
	Colour colour;
	/**
	 * The part of it that the samples of its group share: what the
	 * group's first sample gathered up to the first vertex, held by that
	 * sample alone. A sample alone holds its whole colour.
	 */
	Colour shared;
};

/**
 * How a pixel's noise parts between what its groups share and what
 * each sample draws alone.
 */
struct SplitSpread {
	/**
	 * The variance of the shared part of one group.
	 */
	float shared;
	/**
	 * The variance of the rest of one sample.
	 */
	float own;
};

/**
 * What a pixel's samples add up to, and the sums of its batch means.
 *
 * It stays in double precision although a walk's state is kept in
 * single: it is the frame's sum and no step's item, a still holds it a
 * tile at a time, and a float sum stops being exact once a pixel has
 * drawn about 2^24 samples, which a viewer's history reaches.
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
	/**
	 * The shared parts of the batch being drawn, summed over its
	 * channels' mean.
	 */
	double shared;
	/**
	 * The whole batches' means of the shared part, summed.
	 */
	double sharedMeans;
	/**
	 * The squares of those means, summed.
	 */
	double sharedSquares;
	/**
	 * The squares of the whole batches' means of the rest, summed.
	 */
	double ownSquares;
};

/**
 * What a pixel's samples add up to, as a frame keeps it between its
 * rounds and for the frame after: the part of an Estimate that outlives
 * a tile.
 */
struct PixelSum {
	/**
	 * The colours of its samples summed.
	 */
	Colour total;
	/**
	 * How many samples.
	 */
	int taken;
};

/**
 * The Stopping plugin and what it reads.
 *
 * A pixel's count never reads its own samples, so the frame's mean is
 * unbiased whatever the plugin.
 */
struct Stopping {
	/**
	 * The plugin whose hooks decide the counts; null for a frame whose
	 * every pixel draws the samples below and no trial.
	 */
	const Plugin* plugin;
	/**
	 * The most samples a pixel draws in a frame.
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
	/**
	 * How many samples share a first vertex in every pixel, or zero
	 * where the member says it for each pixel.
	 */
	int split;
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
	 * How many samples shared a first vertex in the trials.
	 */
	int split;
	/**
	 * How each pixel's own trial parts its noise, by rows.
	 */
	std::vector<SplitSpread> spreads;
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
 * The shared part is summed beside it, and a closing batch adds the
 * means of the shared part and of the rest, squared, as well.
 *
 * Every Execution member folds a pixel's samples through this alone, in
 * index order.
 *
 * @param estimate The estimate so far.
 * @param sampled The sample's colour and the part its group shares.
 * @param batch How many samples a batch holds.
 */
Estimate foldedSample(const Estimate& estimate, const Sampled& sampled, int batch);

/**
 * Return how an estimate's whole batches part its noise: the variance
 * of one group's shared part and of the rest of one sample, read from
 * the spread of the batch means. Zero for both under two batches.
 *
 * @param estimate The estimate.
 * @param batch How many samples a batch holds.
 * @param split How many samples shared a first vertex.
 */
SplitSpread splitSpread(const Estimate& estimate, int batch, int split);

/**
 * Return the most samples that may share a first vertex with batches of
 * a size: the largest power of two up to mostSplit that divides it, so
 * that a batch holds whole groups and the batch means stay independent.
 *
 * @param batch How many samples a batch holds.
 */
int splitCeiling(int batch);

/**
 * Return how many samples share a first vertex in every pixel's trial:
 * the stopping's own count where it names one, the plugin's trialSplit
 * otherwise.
 *
 * @param stopping The plugin and what it reads.
 */
int trialSplit(const Stopping& stopping);

/**
 * Return how many samples share a first vertex in a pixel's count pass:
 * the stopping's own count where it names one, one where there was no
 * trial, the plugin's allottedSplit otherwise. It divides a batch.
 *
 * @param stopping The plugin and what it reads.
 * @param query The pixel and every pixel's trial.
 */
int splitFor(const Stopping& stopping, const CountQuery& query);

/**
 * Return the sample counts a tolerance asks for.
 *
 * One effort e from 0 to 0.66 power of 2 sets all three: the tolerance
 * is 62.5 x (0.5 / 62.5)^e, a batch holds the square of round(1 + 7e)
 * samples, rounded up to whole groups of mostSplit, and the ceiling is
 * 32 x 512^e samples, at least a batch. The effort is read back from
 * the tolerance, so a tolerance of 2 asks for batches of 40 and at most
 * 2732 samples. A tolerance of zero or less reads as the effort 1 with
 * the ceiling 8 x 512^e: batches of 64 and at most 4096 samples.
 *
 * @param tolerance The noise a pixel may keep, in steps of a byte.
 */
Effort effortFor(double tolerance);

/**
 * Return an estimate of no samples.
 */
Estimate emptyEstimate();

/**
 * Return how many samples every pixel's trial draws, zero for none: the
 * plugin's trialSamples, zero where there is no plugin.
 *
 * @param stopping The plugin and its settings.
 */
int trialSamples(const Stopping& stopping);

/**
 * Return whether the plugin judges a pixel's noise against the
 * tolerance: it fills wantedSamples and the tolerance is above zero.
 *
 * @param stopping The plugin and its settings.
 */
bool judgesNoise(const Stopping& stopping);

/**
 * Return whether a frame's pixels owe another round of the pixel pass.
 *
 * A plugin that draws no trial owes one round a frame, in which every
 * pixel draws the stopping's count of samples. Where a trial
 * is drawn, a frame that starts its estimates over owes two, the trial
 * and then the rest of every pixel's count; a frame that continues them
 * owes none, since they were drawn until the tolerance.
 *
 * @param stopping The plugin and its settings.
 * @param round How many rounds the frame has run.
 * @param continued Whether the frame continues the estimates of the
 *        frame before.
 */
bool nextRound(const Stopping& stopping, int round, bool continued);

/**
 * Return how many samples one pixel's own trial says it needs: the
 * plugin's wantedSamples.
 *
 * @param stopping The plugin and its settings.
 * @param pixel The trial and the prepared light.
 */
float wantedSamples(const Stopping& stopping, const TrialPixel& pixel);

/**
 * Return how many samples a pixel draws in all, its trial included:
 * the stopping's count where no trial was drawn, the plugin's
 * allottedSamples otherwise.
 *
 * @param stopping The plugin and its settings.
 * @param query The pixel and every trial.
 */
int samplesFor(const Stopping& stopping, const CountQuery& query);

}
