#pragma once

#include "core/stopping.h"

namespace kodanuki {

/**
 * Whether the confidence interval may stop a pixel: false when the
 * build leaves this plugin out (CMake's `-DOFF`), and every pixel then
 * takes the fixed count. Read once at start.
 */
extern const bool confidenceIntervalBuilt;

/**
 * How many batches a trial draws.
 */
constexpr int trialBatches = 2;

/**
 * How many samples share a first vertex in a trial, where a batch
 * holds whole groups of as many.
 */
constexpr int firstSplit = 2;

/**
 * What a group's shared stretch costs against one sample's own: the
 * camera ray, the first vertex and its shadow rays against one
 * direction off it and what follows.
 */
constexpr double sharedCost = 1.5;

/**
 * Return how many samples every pixel's trial draws: two batches,
 * or zero where the tolerance is zero or the ceiling is no more than
 * that.
 *
 * @param stopping The tolerance, the batch and the ceiling.
 */
int confidenceTrial(const Stopping& stopping);

/**
 * Return how many samples a pixel needs for 1.96 standard errors of its
 * mean to span no more than twice the tolerance in steps of a byte
 * after the grade, on its worst channel, judged by its trial alone.
 *
 * Per channel the spread is the variance of the trial's batch means,
 * and the room is the widest half width around the mean, the prepared
 * light included, whose stepsBetween() stays within twice the
 * tolerance. The room is found by doubling or halving, then by 16
 * geometric bisections. The prepared light's own half width, 1.96 of
 * its standard errors, takes its share of the room: where that alone
 * spans more than twice the tolerance, the trial is judged against the
 * whole room, and otherwise against the root of the room squared less
 * that half width squared. A clipped or black channel spans no steps
 * and needs nothing, and a channel with no room needs an endless count.
 * The answer is batches times the batch size, unrounded.
 *
 * @param stopping The tolerance, the batch and the grade.
 * @param pixel The trial and the prepared light.
 */
float confidenceWanted(const Stopping& stopping, const TrialPixel& pixel);

/**
 * Return a pixel's count from its eight neighbours' trials, never from
 * its own.
 *
 * n is the mean of what the neighbours' trials want, fewer at an edge or
 * a corner. The count is sqrt(n * ceiling) rounded up to whole batches,
 * at least the trial; it is the ceiling where that reaches the ceiling.
 * A pixel with no neighbour takes the ceiling. n is first scaled by the
 * noise of a sample at the pixel's split against the trial's: with s
 * the variance of a group's shared part and o that of the rest of a
 * sample, (s k + o) / (s k0 + o) for the split k and the trial's k0.
 *
 * @param stopping The ceiling and the batch.
 * @param query The pixel and every trial.
 */
int confidenceAllotted(const Stopping& stopping, const CountQuery& query);

/**
 * Return how many samples share a first vertex in every trial:
 * firstSplit, or fewer where a batch does not hold whole groups of it.
 *
 * @param stopping The batch.
 */
int confidenceTrialSplit(const Stopping& stopping);

/**
 * Return how many samples share a first vertex in a pixel's count pass,
 * from its eight neighbours' trials, never from its own.
 *
 * With s the mean variance of a group's shared part and o that of the
 * rest of a sample, the cost of a given noise is least at
 * sqrt(o sharedCost / s) samples a group; the answer is the power of
 * two nearest to it, at most splitCeiling() of the batch, and that
 * ceiling where the shared part has no spread.
 *
 * @param stopping The batch.
 * @param query The pixel and every trial.
 */
int confidenceSplit(const Stopping& stopping, const CountQuery& query);

}
