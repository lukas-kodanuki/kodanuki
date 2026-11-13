#pragma once

#include "core/batch.h"
#include "core/pixels.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace kodanuki {

struct Block;
struct Renderer;

/**
 * A rectangle of pixels, the unit a worker takes.
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
 * The workers a backend brings to a pass.
 */
struct Crew {
	/**
	 * How many workers.
	 */
	int workers;
	/**
	 * The slots of each worker's pool, at most poolCapacity.
	 */
	int capacity;
};

/**
 * One worker of a pass and what it works with, updated in place by the
 * steps (rule 46).
 */
struct Worker {
	/**
	 * Its place among the pass's workers, from 0.
	 */
	int index;
	/**
	 * Its backend's place among the chosen backends
	 * (Renderer.backends), which runs its steps.
	 */
	std::uint32_t backend;
	/**
	 * The Execution plugin's place in everyPlugin(), which its
	 * extendRays reports.
	 */
	std::uint32_t executor;
	/**
	 * The Transport plugin's place in everyPlugin(), which its steps
	 * report.
	 */
	std::uint32_t transporter;
	/**
	 * The tile it holds.
	 */
	Tile tile;
	/**
	 * That tile's place among the pass's tiles.
	 */
	std::size_t taken;
	/**
	 * Its walks in flight, as the steps see them.
	 */
	Pool pool;
	/**
	 * The Transport plugin's state of the worker, its walk of each slot
	 * of the pool (openWorker), which only its own accessor casts.
	 */
	std::shared_ptr<void> walks;
	/**
	 * The registers the patterns run in, sized when the worker starts
	 * for the scene's longest program, so no pattern grows them.
	 */
	std::vector<double> registers;
	/**
	 * What the pixel pass holds of its tile.
	 */
	PixelWork pixels;
	/**
	 * What another pass holds of its tile, which only that pass's own
	 * accessor casts; null until the pass opens its first tile.
	 */
	std::shared_ptr<void> held;
};

/**
 * A launch and a fold around the steps every pass shares.
 */
struct Pass {
	/**
	 * Runs once when a worker has taken a tile: sets what the tile owes.
	 */
	void (*opened)(const Renderer& renderer, Worker& worker);
	/**
	 * Fills free slots with walks the tile owes and branches the groups
	 * that wait; false when the tile owes nothing and no walk is alive.
	 */
	bool (*launch)(const Renderer& renderer, Worker& worker);
	/**
	 * Takes the walks that are done into what the pass gathers and frees
	 * their slots.
	 */
	void (*fold)(const Renderer& renderer, Worker& worker);
	/**
	 * Runs once per finished tile, under the pass's lock: puts the
	 * tile's result into the frame.
	 */
	void (*placed)(Renderer& renderer, const Worker& worker);
};

/**
 * Return the tiles covering a frame, by rows from the top left.
 *
 * @param width The frame's width.
 * @param height The frame's height.
 * @param size The tile side.
 */
std::vector<Tile> tilesOf(int width, int height, int size);

/**
 * Run a pass over the renderer's running tiles (Renderer.drawing.tiles)
 * on the workers every chosen backend opens (openWorkers), hosted by
 * the fallback backend (hostWorkers): every worker takes tiles off one
 * counter until none are left, opens each (openTile()), runs
 * tracedTile() on it and places it under one lock (placeTile()). The
 * pass is a step of the frame (Step::Pass).
 *
 * @param renderer The renderer.
 * @param pass The pass.
 */
void onWorkers(Renderer& renderer, const Pass& pass);

/**
 * Run a pass on the worker's tile: launch walks, extend their rays,
 * let the answered walks arrive at their hits, connect to the lights
 * and scatter, and fold the ended ones, until the tile owes nothing
 * and no walk is alive.
 *
 * @param renderer The renderer.
 * @param pass The pass.
 * @param worker The worker, its tile opened.
 */
void tracedTile(const Renderer& renderer, const Pass& pass, Worker& worker);

/**
 * Tell every chosen backend that a block the kernels read is new or
 * changed (uploadBlock).
 *
 * @param renderer The renderer.
 * @param block The block.
 */
void blockChanged(const Renderer& renderer, const Block& block);

} // namespace kodanuki
