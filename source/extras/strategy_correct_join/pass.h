#pragma once

#include "frame/execution.h"
#include "frame/stopping.h"
#include "transport/strategy.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * How many light paths a chunk of the light pass holds.
 */
constexpr std::uint32_t chunkPaths = 4096;

/**
 * What the light pass brought the frame.
 */
struct CarriedLight {
	/**
	 * Each pixel's splats summed, by rows from the top left; empty where
	 * nothing was traced.
	 */
	std::vector<Colour> sums;
	/**
	 * Each pixel's splats squared and summed; empty where the stopping
	 * reads no spread (a fixed count, which draws no trial).
	 */
	std::vector<Colour> squares;
	/**
	 * How many paths were traced, every path of a chunk counted whether
	 * it launched or not.
	 */
	std::uint64_t paths;
};

/**
 * What the light pass reads.
 */
struct LightPassQuery {
	/**
	 * What the transport reads, with the preparations so far and the
	 * light shares.
	 */
	const Tracing& tracing;
	/**
	 * The tolerance, the batch, the ceiling and the grade the rounds stop
	 * by.
	 */
	const Stopping& stopping;
	/**
	 * The member that runs the paths.
	 */
	ExecutionMember execution;
	/**
	 * How many threads it may use.
	 */
	int threads;
};

/**
 * Trace the light pass and return what it brought.
 *
 * The pass is traced in rounds of whole chunks. A round holds the join
 * share times the batch times the pixels in paths. The ceiling is the
 * join share times the most samples times the pixels. Under a fixed
 * count, or a tolerance of zero, every round is traced. Under the
 * confidence interval the pass stops after the first round that leaves
 * the light settled. Threads take chunks off one counter, and each
 * chunk's splats are folded into the sums in chunk order as soon as the
 * chunks before it are, so the sums are the same to the byte on any
 * thread count and with any Execution member. Nothing is traced where
 * Join is off or the join share is zero.
 *
 * @param query The transport, the stopping, the member and the threads.
 */
CarriedLight carriedLight(const LightPassQuery& query);

/**
 * Return whether the light pass has brought every pixel it reached to
 * the tolerance.
 *
 * Per pixel and channel, 1.96 standard errors of the carried mean each
 * way may span no more than twice the tolerance in steps of a byte after
 * the grade. Pixels the pass never reached are not judged.
 *
 * @param light What the pass brought so far.
 * @param stopping The tolerance and the grade.
 */
bool lightSettled(const CarriedLight& light, const Stopping& stopping);

/**
 * Return what the pass brings a pixel: its sum over the paths traced,
 * zero where nothing was traced.
 *
 * @param light What the pass brought.
 * @param pixel The pixel, by rows from the top left.
 */
Colour carriedMean(const CarriedLight& light, std::size_t pixel);

/**
 * Return the variance of carriedMean(), per channel: the spread of the
 * splats over the paths traced, over that count again; zero where the
 * pass kept no squares.
 *
 * @param light What the pass brought.
 * @param pixel The pixel, by rows from the top left.
 */
Colour carriedSpread(const CarriedLight& light, std::size_t pixel);

/**
 * Return a pass that brought nothing.
 */
CarriedLight noCarriedLight();

}
