#include "core/stopping.h"

#include "core/plugin.h"

#include <algorithm>
#include <cmath>

namespace kodanuki {

constexpr double loosestTolerance  = 62.5;
constexpr double tightestTolerance = 0.5;
constexpr double leastCeiling      = 8;
constexpr double mostCeiling       = 4096;
constexpr double deepestStride     = 8;
constexpr double adaptiveRoom      = 4;
const double mostEffort            = std::pow(2.0, 0.66);

double effortOf(double tolerance) {
	const double effort = std::log(loosestTolerance / tolerance) / std::log(loosestTolerance / tightestTolerance);
	return std::clamp(effort, 0.0, mostEffort);
}

Effort effortFor(double tolerance) {
	const double effort = tolerance > 0 ? effortOf(tolerance) : 1.0;
	const int stride    = int(std::lround(1 + (deepestStride - 1) * effort));
	const int batch     = (stride * stride + mostSplit - 1) / mostSplit * mostSplit;
	const double room   = tolerance > 0 ? adaptiveRoom : 1;
	const int ceiling   = int(std::lround(room * leastCeiling * std::pow(mostCeiling / leastCeiling, effort)));
	return Effort {
		.samples = std::max(batch, ceiling),
		.batch   = batch,
	};
}

Estimate closedBatch(const Estimate& estimate, int batch) {
	const Colour mean   = estimate.batch * (1.0 / batch);
	const double shared = estimate.shared / batch;
	const double own    = channelMean(mean) - shared;
	return Estimate {
		.total         = estimate.total,
		.taken         = estimate.taken,
		.batch         = { 0, 0, 0 },
		.batches       = estimate.batches + 1,
		.means         = estimate.means + mean,
		.squares       = estimate.squares + mean * mean,
		.shared        = 0,
		.sharedMeans   = estimate.sharedMeans + shared,
		.sharedSquares = estimate.sharedSquares + shared * shared,
		.ownSquares    = estimate.ownSquares + own * own,
	};
}

Estimate foldedSample(const Estimate& estimate, const Sampled& sampled, int batch) {
	const Estimate folded = {
		.total         = estimate.total + sampled.colour,
		.taken         = estimate.taken + 1,
		.batch         = estimate.batch + sampled.colour,
		.batches       = estimate.batches,
		.means         = estimate.means,
		.squares       = estimate.squares,
		.shared        = estimate.shared + channelMean(sampled.shared),
		.sharedMeans   = estimate.sharedMeans,
		.sharedSquares = estimate.sharedSquares,
		.ownSquares    = estimate.ownSquares,
	};

	return batch > 0 && folded.taken % batch == 0 ? closedBatch(folded, batch) : folded;
}

Estimate emptyEstimate() {
	return Estimate {
		.total         = { 0, 0, 0 },
		.taken         = 0,
		.batch         = { 0, 0, 0 },
		.batches       = 0,
		.means         = { 0, 0, 0 },
		.squares       = { 0, 0, 0 },
		.shared        = 0,
		.sharedMeans   = 0,
		.sharedSquares = 0,
		.ownSquares    = 0,
	};
}

double meansSpread(double means, double squares, int batches) {
	const double count = double(batches);
	const double mean  = means / count;
	return std::max(squares / count - mean * mean, 0.0) * (count / (count - 1));
}

SplitSpread splitSpread(const Estimate& estimate, int batch, int split) {
	if (estimate.batches < 2) {
		return SplitSpread { 0, 0 };
	}
	const double own    = channelMean(estimate.means) - estimate.sharedMeans;
	const double groups = double(batch) / double(split);
	return SplitSpread {
		.shared = float(meansSpread(estimate.sharedMeans, estimate.sharedSquares, estimate.batches) * groups),
		.own    = float(meansSpread(own, estimate.ownSquares, estimate.batches) * double(batch)),
	};
}

int splitCeiling(int batch) {
	int split = 1;
	while (split < mostSplit && batch > 0 && batch % (split * 2) == 0) {
		split = split * 2;
	}
	return split;
}

int trialSplit(const Stopping& stopping) {
	if (stopping.split > 0) {
		return stopping.split;
	}
	return stopping.plugin->hooks.trialSplit(stopping);
}

int splitFor(const Stopping& stopping, const CountQuery& query) {
	if (stopping.split > 0) {
		return stopping.split;
	}
	if (query.trial.samples <= 0) {
		return 1;
	}
	return stopping.plugin->hooks.allottedSplit(stopping, query);
}

int trialSamples(const Stopping& stopping) {
	return stopping.plugin ? stopping.plugin->hooks.trialSamples(stopping) : 0;
}

bool judgesNoise(const Stopping& stopping) {
	return stopping.plugin && stopping.plugin->hooks.wantedSamples && stopping.tolerance > 0;
}

bool nextRound(const Stopping& stopping, int round, bool continued) {
	if (trialSamples(stopping) <= 0) {
		return round == 0;
	}
	return round < 2 && !continued;
}

float wantedSamples(const Stopping& stopping, const TrialPixel& pixel) {
	return stopping.plugin->hooks.wantedSamples(stopping, pixel);
}

int samplesFor(const Stopping& stopping, const CountQuery& query) {
	if (query.trial.samples <= 0) {
		return stopping.samples;
	}
	return stopping.plugin->hooks.allottedSamples(stopping, query);
}

}
