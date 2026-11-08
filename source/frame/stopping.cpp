#include "frame/stopping.h"

#include "extras/stopping_consistent_confidence_interval/confidence_interval.h"

#include <algorithm>
#include <cmath>

namespace kodanuki {

constexpr double loosestTolerance  = 62.5;
constexpr double tightestTolerance = 0.5;
constexpr double leastCeiling      = 8;
constexpr double mostCeiling       = 4096;
constexpr double deepestStride     = 8;
const double mostEffort            = std::pow(2.0, 0.66);

double effortOf(double tolerance) {
	const double effort = std::log(loosestTolerance / tolerance) / std::log(loosestTolerance / tightestTolerance);
	return std::clamp(effort, 0.0, mostEffort);
}

Effort effortFor(double tolerance) {
	const double effort = tolerance > 0 ? effortOf(tolerance) : 1.0;
	const int stride    = int(std::lround(1 + (deepestStride - 1) * effort));
	const int batch     = stride * stride;
	const int ceiling   = int(std::lround(leastCeiling * std::pow(mostCeiling / leastCeiling, effort)));
	return Effort {
		.samples = std::max(batch, ceiling),
		.batch   = batch,
	};
}

Estimate closedBatch(const Estimate& estimate, int batch) {
	const Colour mean = estimate.batch * (1.0 / batch);
	return Estimate {
		.total   = estimate.total,
		.taken   = estimate.taken,
		.batch   = { 0, 0, 0 },
		.batches = estimate.batches + 1,
		.means   = estimate.means + mean,
		.squares = estimate.squares + mean * mean,
	};
}

Estimate foldedSample(const Estimate& estimate, const Colour& colour, int batch) {
	const Estimate folded = {
		.total   = estimate.total + colour,
		.taken   = estimate.taken + 1,
		.batch   = estimate.batch + colour,
		.batches = estimate.batches,
		.means   = estimate.means,
		.squares = estimate.squares,
	};

	return batch > 0 && folded.taken % batch == 0 ? closedBatch(folded, batch) : folded;
}

Estimate emptyEstimate() {
	return Estimate {
		.total   = { 0, 0, 0 },
		.taken   = 0,
		.batch   = { 0, 0, 0 },
		.batches = 0,
		.means   = { 0, 0, 0 },
		.squares = { 0, 0, 0 },
	};
}

int trialSamples(const Stopping& stopping) {
	switch (stopping.member) {
		case StoppingMember::FixedCount: return 0;
		case StoppingMember::ConfidenceInterval: return confidenceTrial(stopping);
	}

	return 0;
}

float wantedSamples(const Stopping& stopping, const TrialPixel& pixel) {
	switch (stopping.member) {
		case StoppingMember::FixedCount: return 0;
		case StoppingMember::ConfidenceInterval: return confidenceWanted(stopping, pixel);
	}

	return 0;
}

int samplesFor(const Stopping& stopping, const CountQuery& query) {
	if (query.trial.samples <= 0) {
		return stopping.samples;
	}
	switch (stopping.member) {
		case StoppingMember::FixedCount: return stopping.samples;
		case StoppingMember::ConfidenceInterval: return confidenceAllotted(stopping, query);
	}

	return stopping.samples;
}

}
