#include "plugins/stopping/confidence_interval/confidence_interval.h"

#include "core/plugin.h"
#include "shared/image.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace kodanuki {

const bool confidenceIntervalBuilt = pluginBuilt("stopping/confidence_interval");

constexpr double confidence  = 1.96;
constexpr double smallest    = 1e-30;
constexpr double largest     = 1e30;
constexpr int refinements    = 16;
constexpr double endlessRoom = std::numeric_limits<double>::infinity();

struct RoomQuery {
	double mean;
	double bar;
	const Grade& grade;
};

int confidenceTrial(const Stopping& stopping) {
	const int samples = trialBatches * stopping.batch;
	return confidenceIntervalBuilt && stopping.tolerance > 0 && samples < stopping.samples ? samples : 0;
}

bool roomFits(const RoomQuery& query, double width) {
	return stepsBetween(query.mean - width, query.mean + width, query.grade) <= query.bar;
}

double bisectedRoom(const RoomQuery& query, double low, double high) {
	for (int step = 0; step < refinements; ++step) {
		const double middle = std::sqrt(low * high);
		const bool fits     = roomFits(query, middle);
		low                 = fits ? middle : low;
		high                = fits ? high : middle;
	}
	return low;
}

double grownRoom(const RoomQuery& query, double start) {
	double low  = start;
	double high = start;
	while (roomFits(query, high)) {
		if (high > largest) {
			return endlessRoom;
		}
		low  = high;
		high = high * 2;
	}
	return bisectedRoom(query, low, high);
}

double shrunkRoom(const RoomQuery& query, double start) {
	double low  = start;
	double high = start;
	while (!roomFits(query, low)) {
		if (low < smallest) {
			return 0;
		}
		high = low;
		low  = low * 0.5;
	}
	return bisectedRoom(query, low, high);
}

double roomAround(const RoomQuery& query) {
	const double start = std::max(std::abs(query.mean), smallest);
	return roomFits(query, start) ? grownRoom(query, start) : shrunkRoom(query, start);
}

struct ChannelTrial {
	double mean;
	double spread;
	double unsure;
};

double ownRoom(const RoomQuery& query, double unsure) {
	const double whole  = roomAround(query);
	const double theirs = confidence * std::sqrt(unsure);
	if (!(theirs > 0)) {
		return whole;
	}
	const bool alone = stepsBetween(query.mean - theirs, query.mean + theirs, query.grade) > query.bar;
	return alone ? whole : std::sqrt(std::max(whole * whole - theirs * theirs, 0.0));
}

double batchesNeeded(const Stopping& stopping, const ChannelTrial& channel) {
	if (!(channel.spread > 0)) {
		return 0;
	}

	const RoomQuery query = {
		.mean  = channel.mean,
		.bar   = 2 * stopping.tolerance,
		.grade = stopping.grade,
	};

	const double own = ownRoom(query, channel.unsure);
	if (!(own > 0)) {
		return endlessRoom;
	}
	return channel.spread * (confidence / own) * (confidence / own);
}

double batchSpread(double means, double squares, int batches) {
	const double count  = double(batches);
	const double mean   = means / count;
	const double second = squares / count;
	return std::max(second - mean * mean, 0.0) * (count / (count - 1));
}

ChannelTrial trialChannel(const TrialPixel& pixel, int channel) {
	const Estimate& estimate = pixel.estimate;
	const Colour mean        = estimate.total * (1.0 / estimate.taken) + pixel.prepared;
	const double means[3]    = { estimate.means.red, estimate.means.green, estimate.means.blue };
	const double squares[3]  = { estimate.squares.red, estimate.squares.green, estimate.squares.blue };
	const double middles[3]  = { mean.red, mean.green, mean.blue };
	const double unsure[3]   = { pixel.spread.red, pixel.spread.green, pixel.spread.blue };
	return ChannelTrial {
		.mean   = middles[channel],
		.spread = batchSpread(means[channel], squares[channel], estimate.batches),
		.unsure = unsure[channel],
	};
}

float confidenceWanted(const Stopping& stopping, const TrialPixel& pixel) {
	double batches = 0;
	for (int channel = 0; channel < 3; ++channel) {
		batches = std::max(batches, batchesNeeded(stopping, trialChannel(pixel, channel)));
	}
	return float(batches * stopping.batch);
}

struct Around {
	double sum;
	int count;
};

Around neighboursWanted(const CountQuery& query) {
	const Trial& trial = query.trial;

	Around around = {
		.sum   = 0,
		.count = 0,
	};

	for (int row = std::max(query.y - 1, 0); row <= std::min(query.y + 1, trial.height - 1); ++row) {
		for (int column = std::max(query.x - 1, 0); column <= std::min(query.x + 1, trial.width - 1); ++column) {
			if (row == query.y && column == query.x) {
				continue;
			}
			around.sum += trial.wanted[std::size_t(row) * std::size_t(trial.width) + std::size_t(column)];
			around.count += 1;
		}
	}
	return around;
}

int confidenceTrialSplit(const Stopping& stopping) {
	return std::min(firstSplit, splitCeiling(stopping.batch));
}

SplitSpread neighboursSpread(const CountQuery& query) {
	const Trial& trial = query.trial;
	double shared      = 0;
	double own         = 0;
	int count          = 0;
	for (int row = std::max(query.y - 1, 0); row <= std::min(query.y + 1, trial.height - 1); ++row) {
		for (int column = std::max(query.x - 1, 0); column <= std::min(query.x + 1, trial.width - 1); ++column) {
			if (row == query.y && column == query.x) {
				continue;
			}
			const SplitSpread& spread = trial.spreads[std::size_t(row) * std::size_t(trial.width) + std::size_t(column)];
			shared += spread.shared;
			own += spread.own;
			count += 1;
		}
	}
	return SplitSpread { float(shared / std::max(count, 1)), float(own / std::max(count, 1)) };
}

int splitNear(double ideal, int ceiling) {
	int split = 1;
	while (split < ceiling && split * std::sqrt(2.0) <= ideal) {
		split = split * 2;
	}
	return split;
}

int confidenceSplit(const Stopping& stopping, const CountQuery& query) {
	const SplitSpread spread = neighboursSpread(query);
	const int ceiling        = splitCeiling(stopping.batch);
	if (!(spread.shared > 0)) {
		return ceiling;
	}
	return splitNear(std::sqrt(double(spread.own) * sharedCost / double(spread.shared)), ceiling);
}

double splitNoise(const Stopping& stopping, const CountQuery& query) {
	const SplitSpread spread = neighboursSpread(query);
	const double tried       = double(spread.shared) * query.trial.split + double(spread.own);
	if (!(tried > 0)) {
		return 1;
	}
	return (double(spread.shared) * confidenceSplit(stopping, query) + double(spread.own)) / tried;
}

int confidenceAllotted(const Stopping& stopping, const CountQuery& query) {
	const Around around = neighboursWanted(query);
	if (around.count == 0) {
		return stopping.samples;
	}
	const double wanted  = around.sum / around.count * splitNoise(stopping, query);
	const double batches = std::ceil(std::sqrt(wanted * stopping.samples) / stopping.batch);
	if (!(batches * stopping.batch < stopping.samples)) {
		return stopping.samples;
	}
	return std::max(int(batches) * stopping.batch, query.trial.samples);
}

}
