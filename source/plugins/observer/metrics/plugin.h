#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `observer/metrics` as the core lists it: it sums
 * the steps it is told of and the counters the plugins publish, and
 * writes them when the renderer closes to `<scene>.metrics.json` beside
 * --out (in --out for a track, in `output/` without --out).
 *
 * The report is one JSON object, schema 1:
 *
 * - `schema` (1), `scene`, `width`, `height`, `threads`, `frames` (how
 *   many were delivered), `peak_mb` (peak resident memory);
 * - `plugins`: every plugin of the build, `{name, point, effect, use}`,
 *   use `chosen` (named by the options), `off` (could be named, is
 *   not) or `built` (on whenever it is built);
 * - `steps`: rows `{frame, worker, step, plugin, calls, items,
 *   seconds}`, one per frame, worker, step and plugin; worker -1 is the
 *   frame's thread, plugin `core` the core's own step. The frame's
 *   steps NextFrame, BuildWorld, PrepareFrame, DrawFrame,
 *   ReconstructFrame and DeliverFrame add up to the frame's time;
 *   Preparations, Pass, Surfaces, Reconstruction and Keeping lie inside
 *   DrawFrame; a worker's steps add up to its share of the Pass rows;
 * - `counters`: rows `{frame, plugin, name, value}`, what each counter
 *   grew by in the frame; plugin `core` for the bytes the core holds
 *   (`bytes.pool`, `bytes.history`) at the frame's end, while the tree's
 *   are `geometry/tree`'s counter `bytes.tree`;
 * - `samples`: rows `{frame, samples}`, the frame's mean samples a
 *   pixel.
 *
 * A frame's rows are taken when the frame is delivered; the steps after
 * that (the rest of DeliverFrame, and the NextFrame that finds no more
 * frames) belong to the next frame's index.
 */
Plugin observerMetricsPlugin();

} // namespace kodanuki
