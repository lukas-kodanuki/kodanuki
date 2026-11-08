#pragma once

#include "frame/execution.h"
#include "frame/preparation.h"
#include "frame/stopping.h"
#include "image/image.h"
#include "light/light.h"
#include "material/material.h"
#include "sdf/scene.h"
#include "transport/camera.h"
#include "transport/trace.h"
#include "walk/walk.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A rectangle of pixels, the unit a thread takes.
 */
struct Tile {
	/**
	 * The left column.
	 */
	int x;
	/**
	 * The top row.
	 */
	int y;
	/**
	 * How many columns.
	 */
	int width;
	/**
	 * How many rows.
	 */
	int height;
};

/**
 * What a tile's pixels left, by rows inside the tile: what the frame's
 * pass reads of their estimates, which are dropped once the tile is done.
 */
struct TileResult {
	/**
	 * The tile.
	 */
	Tile tile;
	/**
	 * After the count pass, each pixel's mean with what the preparations
	 * brought it added, three channels a pixel; empty after the trial.
	 */
	std::vector<float> rgb;
	/**
	 * After the trial, how many samples each pixel's own trial says it
	 * needs; empty after the count pass.
	 */
	std::vector<float> wanted;
	/**
	 * After the trial, the colours each pixel's trial summed; empty after
	 * the count pass.
	 */
	std::vector<Colour> totals;
	/**
	 * After a step of a sequence, each pixel's estimate; empty after the
	 * passes of a still frame.
	 */
	std::vector<Estimate> estimates;
	/**
	 * After a step of a sequence, what each pixel's sample left; empty
	 * after the passes of a still frame.
	 */
	std::vector<Recorded> records;
	/**
	 * How many samples the tile's pixels drew in all.
	 */
	double samples;
};

/**
 * How far a frame's tiles have come: a count the frame keeps as it
 * draws, which a front end may read from another thread meanwhile.
 *
 * The caller owns it and hands it to the frame; a sequence's frames add
 * to one count.
 */
struct Progress {
	/**
	 * How many tiles are finished.
	 */
	std::atomic<int> finished;
	/**
	 * How many tiles the passes begun so far draw in all: a still frame
	 * adds its tiles once for each pass, the trial and the count, when it
	 * starts drawing.
	 */
	std::atomic<int> total;
};

/**
 * What a frame is drawn from.
 */
struct FrameInput {
	/**
	 * The tree and the scene.
	 */
	const Geometry& geometry;
	/**
	 * The scene's looks: its materials and coats.
	 */
	const Looks& looks;
	/**
	 * The scene's lighting: its lights and sky.
	 */
	const Lighting& lighting;
	/**
	 * The scene's shot: the camera, render settings and grade the frame
	 * reads.
	 */
	const Shot& shot;
	/**
	 * The lens the frame draws through; its size is the frame's, which
	 * may differ from the shot's render size.
	 */
	const Lens& lens;
	/**
	 * The members `--use` chose.
	 */
	const Prepared& chosen;
	/**
	 * The preparations to run, in order.
	 */
	const std::vector<PreparationMember>& preparations;
	/**
	 * The Feature members.
	 */
	const Features& features;
	/**
	 * The Stopping member.
	 */
	StoppingMember stopping;
	/**
	 * How glass of roughness 0 answers, `--use smooth_glass=`.
	 */
	SmoothGlass smoothGlass;
	/**
	 * The Execution member.
	 */
	ExecutionMember execution;
	/**
	 * How many threads.
	 */
	int threads;
	/**
	 * The count of finished tiles the frame keeps.
	 */
	Progress& progress;
};

/**
 * What a frame works out from its input before any preparation runs.
 */
struct FrameBasis {
	/**
	 * The finishes, with the features applied.
	 */
	Materials materials;
	/**
	 * The lights.
	 */
	Lights lights;
	/**
	 * How the frame decides when a pixel is done.
	 */
	Stopping stopping;
};

/**
 * What the tiled pass of a frame reads.
 */
struct TiledRun {
	/**
	 * The transport, with the prepared members.
	 */
	const Tracing& tracing;
	/**
	 * How the frame decides when a pixel is done.
	 */
	const Stopping& stopping;
	/**
	 * What the preparations left.
	 */
	const Prepared& prepared;
	/**
	 * The Execution member.
	 */
	ExecutionMember execution;
	/**
	 * How many threads.
	 */
	int threads;
	/**
	 * The count of finished tiles the pass adds to.
	 */
	Progress& progress;
};

/**
 * What the pass of one frame of a sequence reads.
 */
struct SteppedRun {
	/**
	 * The transport, with the prepared members, which records.
	 */
	const Tracing& tracing;
	/**
	 * The stopping, whose batch the pixels fold by.
	 */
	const Stopping& stopping;
	/**
	 * What the preparations left.
	 */
	const Prepared& prepared;
	/**
	 * Each pixel's estimate so far, by rows from the top left; empty to
	 * start every pixel afresh.
	 */
	const std::vector<Estimate>& estimates;
	/**
	 * The index of the one sample each pixel draws: the frame's place in
	 * the sequence.
	 */
	std::uint32_t index;
	/**
	 * The Execution member.
	 */
	ExecutionMember execution;
	/**
	 * How many threads.
	 */
	int threads;
	/**
	 * The count of finished tiles the pass adds to.
	 */
	Progress& progress;
};

/**
 * What one frame of a sequence's pixels left, by rows from the top left.
 */
struct SteppedPass {
	/**
	 * Each pixel's estimate with the frame's sample folded in.
	 */
	std::vector<Estimate> estimates;
	/**
	 * What each pixel's sample left.
	 */
	std::vector<Recorded> records;
};

/**
 * Which of a frame's preparations to run, and from what.
 */
struct PreparationRun {
	/**
	 * The preparations so far, which the run starts from.
	 */
	const Prepared& start;
	/**
	 * What the preparations run read: only the input's preparations that
	 * read this run, in the input's order.
	 */
	PreparationReads reads;
};

/**
 * A drawn frame.
 */
struct Rendered {
	/**
	 * The linear image.
	 */
	Image image;
	/**
	 * The mean samples per pixel.
	 */
	double samples;
};

/**
 * Draw a frame.
 *
 * It works out the finishes and the lights (frameBasis), runs the
 * preparations (preparedFrame), draws the tiles (tiledPass) and places
 * them (placedTiles). Where the Stopping member draws a trial, every
 * pixel's trial is drawn first and judged; then every pixel draws the
 * rest of its count of samples, continuing from its trial. Both passes
 * run over 16-pixel tiles. Threads take tiles off one counter, and each
 * tile's pixels are run by the Execution member. What each tile's
 * estimates give the pass is kept by tile index and placed at the end,
 * and a pixel's samples are folded in index order by one thread, so the
 * frame is the same to the byte at any thread count and with any member.
 *
 * @param input What the frame is drawn from.
 */
Rendered renderFrame(const FrameInput& input);

/**
 * Return a count of no tiles.
 */
Progress freshProgress();

/**
 * Work out a frame's finishes, lights and stopping from its input.
 *
 * @param input What the frame is drawn from.
 */
FrameBasis frameBasis(const FrameInput& input);

/**
 * Return the transport of a frame with the given prepared members.
 *
 * @param input What the frame is drawn from.
 * @param basis The frame's finishes, lights and stopping.
 * @param prepared The members the transport samples with.
 */
Tracing frameTracing(const FrameInput& input, const FrameBasis& basis, const Prepared& prepared);

/**
 * Run the input's preparations in order, starting from the chosen
 * members, and return what they leave.
 *
 * Each preparation reads the transport as the ones before it left it.
 *
 * @param input What the frame is drawn from.
 * @param basis The frame's finishes, lights and stopping.
 */
Prepared preparedFrame(const FrameInput& input, const FrameBasis& basis);

/**
 * Run the input's preparations that read what the run names, in order,
 * starting from the run's preparations, and return what they leave.
 *
 * A sequence keeps what the Load preparations leave while only the
 * camera moves and reruns the Camera ones from it. Where every Camera
 * preparation follows every Load one, as in defaultPreparations(), the
 * Load run and then the Camera run leave what preparedFrame() leaves.
 *
 * @param input What the frame is drawn from.
 * @param basis The frame's finishes, lights and stopping.
 * @param run The preparations to start from and which to run.
 */
Prepared preparedFrom(const FrameInput& input, const FrameBasis& basis, const PreparationRun& run);

/**
 * Draw one sample of every pixel of a frame of a sequence over 16-pixel
 * tiles: the sample of the run's index, continuing each pixel's estimate.
 *
 * Each pixel's job is its one sample, started from its estimate so far,
 * so a run of frames on a still camera folds each pixel's samples in the
 * order and with the batches of one job of them all.
 *
 * @param run What the pass reads.
 */
SteppedPass steppedPass(const SteppedRun& run);

/**
 * Return a pixel's colour: its estimate's mean, zero for no samples,
 * plus what the preparations brought it.
 *
 * @param estimate The pixel's estimate.
 * @param prepared The preparations.
 * @param pixel The pixel, by rows from the top left.
 */
Colour pixelColour(const Estimate& estimate, const Prepared& prepared, std::size_t pixel);

/**
 * Draw every pixel of a frame over 16-pixel tiles and return the tiles'
 * results in tile order.
 *
 * Where the Stopping member draws a trial, every pixel's trial is drawn
 * and judged first; then every pixel draws the rest of its count,
 * continuing from its trial.
 *
 * @param run What the pass reads.
 */
std::vector<TileResult> tiledPass(const TiledRun& run);

/**
 * Return the tiles covering a frame, by rows from the top left.
 *
 * @param width The frame's width.
 * @param height The frame's height.
 * @param size The tile side.
 */
std::vector<Tile> tilesOf(int width, int height, int size);

/**
 * Place every tile's pixel colours into an image of the frame's size.
 *
 * @param results The tiles of the count pass.
 * @param width The frame's width.
 * @param height The frame's height.
 */
Image placedTiles(const std::vector<TileResult>& results, int width, int height);

/**
 * Draw the hits' normals as colours, one ray a pixel, for the geometry
 * checks.
 *
 * Each channel is half the normal's component plus a half, and a miss
 * is black.
 *
 * @param geometry The tree and the scene.
 * @param lens The lens.
 * @param threads How many threads.
 */
Image normalsImage(const Geometry& geometry, const Lens& lens, int threads);

}
