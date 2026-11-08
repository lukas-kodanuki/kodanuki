#pragma once

#include "frame/stopping.h"

namespace kodanuki {

/**
 * Whether the confidence interval may stop a pixel: false when the
 * build turns stopping_consistent_confidence_interval off
 * (CMake's `-DOFF`), and every pixel then takes the fixed count.
 */
#ifdef KODANUKI_OFF_STOPPING_CONSISTENT_CONFIDENCE_INTERVAL
constexpr bool confidenceIntervalBuilt = false;
#else
constexpr bool confidenceIntervalBuilt = true;
#endif

/**
 * How many batches a trial draws.
 */
constexpr int trialBatches = 2;

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
 * A pixel with no neighbour takes the ceiling.
 *
 * @param stopping The ceiling and the batch.
 * @param query The pixel and every trial.
 */
int confidenceAllotted(const Stopping& stopping, const CountQuery& query);

}
