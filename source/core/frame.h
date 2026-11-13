#pragma once

#include "core/batch.h"
#include "core/camera.h"
#include "core/geometry.h"
#include "core/history.h"
#include "core/pixels.h"
#include "core/preparation.h"
#include "core/reconstruction.h"
#include "core/step.h"
#include "core/stopping.h"
#include "core/worker.h"
#include "sdf/scene.h"
#include "shared/image.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kodanuki {

struct Renderer;

/**
 * The frame being drawn, as the Renderer keeps it: what the steps of
 * the batch loop and the passes read, and where the tiles' results go.
 */
struct FrameDrawing {
	/**
	 * The scene's stopping, which says how fine the frame is meant to
	 * be: what the preparations read.
	 */
	Stopping stopping;
	/**
	 * How the frame's pixel pass decides when a pixel is done: the
	 * scene's stopping, or FixedCount of the options' samples a frame
	 * (Options.perFrame).
	 */
	Stopping pixels;
	/**
	 * The Transport plugin's state of the frame (openFrame), with the
	 * prepared members, which only its own accessor casts; during a
	 * preparation, the one that preparation reads; null between frames.
	 */
	std::shared_ptr<void> transported;
	/**
	 * Whether the walks keep what their camera rays met first
	 * (Transport's firstSurface).
	 */
	bool recording;
	/**
	 * Every pixel's trial: no samples where the stopping draws none.
	 * Its sample count and split stand from the start; what each trial
	 * wants and how it parts its noise are filled as the tiles of the
	 * first round are placed, and read by the second.
	 */
	Trial trial;
	/**
	 * How many rounds of the pixel pass the frame ran before the running
	 * one.
	 */
	int round;
	/**
	 * The index of the first sample of estimates that start over with
	 * this frame, so that no frame of a sequence draws the samples of an
	 * earlier one again; a pixel's next sample takes this plus how many
	 * its estimate holds.
	 */
	std::uint32_t offset;
	/**
	 * What each pixel's samples add up to, by rows from the top left:
	 * what a tile's pixels start from, replaced as the tile is placed.
	 */
	std::vector<PixelSum> estimates;
	/**
	 * What the camera ray of each pixel's last sample met first, by
	 * rows, filled as the tiles are placed; empty where the walks keep
	 * none (recording).
	 */
	std::vector<Surface> firsts;
	/**
	 * The running pass's tiles, in the order the workers take them.
	 */
	std::vector<Tile> tiles;
	/**
	 * What the running pass gathers into, if not the pixel pass, which
	 * only that pass's own accessor casts; null for the pixel pass.
	 */
	std::shared_ptr<void> gathered;
};

/**
 * What the Transport plugin opens its state of a frame from
 * (openFrame).
 */
struct TransportFrame {
	/**
	 * Its state of the loaded scene (stateOf()).
	 */
	const void* world;
	/**
	 * What rays meet.
	 */
	const Geometry& geometry;
	/**
	 * The scene's looks.
	 */
	const Looks& looks;
	/**
	 * The scene's lighting.
	 */
	const Lighting& lighting;
	/**
	 * The scene's shot.
	 */
	const Shot& shot;
	/**
	 * The lens the frame draws through.
	 */
	const Lens& lens;
	/**
	 * How the frame's pixels cover the shown frame's.
	 */
	FilmPlan film;
	/**
	 * Whether the walks keep what their camera rays met first.
	 */
	bool recording;
	/**
	 * The plugins' states, as the preparations so far left them.
	 */
	States& states;
};

/**
 * Draw the renderer's frame at its step (Renderer.step): leave its
 * shown image in Renderer.drawn and the history after it in
 * Renderer.history. The one frame of the still, the track and the
 * viewer.
 *
 * It works out the frame's stoppings (Renderer.drawing.stopping and .pixels)
 * and runs or keeps the preparations (preparesAgain()): the Load ones are kept
 * until a Cut, Lights or Scene change and the Camera ones until any change; the
 * first frame runs them all. Where nothing changed, each pixel's estimate
 * continues the history's; on any change every estimate starts over.
 *
 * The pixel pass (pixelPass()) then runs over 16-pixel tiles by
 * passedTiles() once for every round the Stopping member owes
 * (nextRound()): one round of the stopping's count of samples, or
 * every pixel's trial and then the rest of every pixel's count, set
 * from the trials around it. Each tile's walks go through the batch
 * loop (tracedTile()) with the Execution member's plugin, and a pixel's
 * samples are folded in index order by one thread, so the frame is the
 * same within noise at any thread count and with any member.
 *
 * Where a Reconstruction member reads what the samples left
 * (readsRecords()), the frame records each pixel's light of this frame
 * and the first surface of its last sample; where one reads the
 * G-buffer (readsSurfaces()), each drawn pixel's first surface is
 * traced. The Reconstruction members then make the shown image. N
 * frames of one sample on a still camera with Mean alone show the image
 * of one frame of N samples of FixedCount, within noise.
 *
 * @param renderer The renderer, with its world built and its step set;
 *        its history is the one after the frame before.
 */
void drawnFrame(Renderer& renderer);

/**
 * Return how many tiles the pixel passes of the renderer's next frame
 * place: its 16-pixel tiles once for every round it owes.
 *
 * @param renderer The renderer, with its world built and its step set.
 */
std::uint32_t frameTiles(const Renderer& renderer);

/**
 * Return the Transport plugin's state of the renderer's frame
 * (openFrame), with the prepared members as the history's states hold
 * them.
 *
 * @param renderer The renderer.
 * @param film How the frame's pixels cover the shown frame's.
 * @param recording Whether the walks keep what their camera rays met
 *        first.
 */
std::shared_ptr<void> frameTransport(Renderer& renderer, FilmPlan film, bool recording);

/**
 * Return the frame state of a renderer that draws no frame yet.
 */
FrameDrawing idleDrawing();

/**
 * Return a pixel's colour: its estimate's mean, zero for no samples,
 * plus what the preparations brought it.
 *
 * @param estimate What the pixel's samples add up to.
 * @param prepared The preparations.
 * @param pixel The pixel, by rows from the top left.
 */
Colour pixelColour(const PixelSum& estimate, const Preparations& prepared, std::size_t pixel);

/**
 * Run a pass over its tiles by onWorkers(): the renderer keeps the
 * tiles and what the pass gathers into (Renderer.drawing.tiles, .gathered)
 * while it runs, and drops them after.
 *
 * @param renderer The renderer.
 * @param pass The pass.
 * @param tiles The pass's tiles, in the order the workers take them.
 * @param gathered What the pass gathers into; null for the pixel pass.
 */
void passedTiles(Renderer& renderer, const Pass& pass, std::vector<Tile> tiles, std::shared_ptr<void> gathered);

/**
 * Return the pixel pass of a frame.
 *
 * A tile opens with one job a pixel: the samples the pixel owes in the
 * drawing's round, continued from what its estimate holds. Where the
 * stopping draws no trial, that is its count of samples; otherwise the
 * trial in the first round and, in the second, the rest of the count
 * the trials around the pixel set. A slot of the worker's pixels takes
 * the next job and launches slotSamples of its samples at a time, the
 * first of each group; a walk that waits on its group's first vertex is
 * branched into the group's walks. When the last walk of a launch is
 * done, the launch's colours are folded in index order by
 * foldedSample(), and the slot launches the job's next samples or takes
 * the next job. A placed tile leaves what each pixel's samples add up
 * to in the drawing's estimates, in the round of the trials what each
 * trial wants, and where the tracing records, each pixel's first
 * surface.
 */
Pass pixelPass();

} // namespace kodanuki
