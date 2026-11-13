#pragma once

#include "core/pixels.h"
#include "core/preparation.h"
#include "core/state.h"
#include "core/step.h"
#include "core/stopping.h"
#include "core/worker.h"
#include "plugins/transport/paths/strategy.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

struct Renderer;

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
 * Trace the light pass and return what it brought.
 *
 * The pass is traced in rounds of whole chunks. A round holds the join
 * share times the batch times the pixels in paths. The ceiling is the
 * join share times the most samples times the pixels. Under a fixed
 * count, or a tolerance of zero, every round is traced. Under the
 * confidence interval the pass stops after the first round that leaves
 * the light settled. A round is the light pass (lightPass()) run over
 * its chunks by onWorkers(): workers take chunks off one counter, and
 * each chunk's splats are folded into the sums in chunk order as soon
 * as the chunks before it are, so the sums are the same on any thread
 * count and with any Execution plugin. Nothing is traced where Join is
 * off or the join share is zero.
 *
 * @param renderer The renderer: the Transport's state of the frame
 *        (Renderer.drawing.transported), the scene's stopping
 * (Renderer.drawing.stopping), the Execution plugin and the threads.
 */
CarriedLight carriedLight(Renderer& renderer);

/**
 * Return the light pass: a tile is a chunk of chunkPaths light paths,
 * its column the chunk's place in the whole pass.
 *
 * `launch` starts the chunk's next path in each free slot by
 * lightPathRestarted(), so the slot is Leading and the transport's
 * step walks the light path as it walks an eye walk's lead. `fold`
 * keeps each ended path's landing by its index in the chunk. A placed
 * chunk hands its splats over by path index, each path's in the order
 * of its surfaces.
 */
Pass lightPass();

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

/**
 * Return what the light pass brought, which the plugin keeps among the
 * kept states, opened as nothing where no light pass ran yet: the
 * plugin's one accessor.
 *
 * @param states The kept states.
 */
CarriedLight& carriedLightState(States& states);

} // namespace kodanuki
