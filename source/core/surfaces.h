#pragma once

#include "core/batch.h"
#include "core/camera.h"
#include "core/geometry.h"
#include "core/pixels.h"
#include "core/plugin.h"
#include "core/step.h"
#include "core/worker.h"

#include <string>
#include <vector>

namespace kodanuki {

struct Renderer;

/**
 * The side of a tile of the surface pass: a multiple of a block's
 * columns and rows, and two pools of walks.
 */
constexpr int surfaceTileSide = 32;

/**
 * How many columns a block of neighbouring pixels spans.
 */
constexpr int surfaceBlockWidth = 4;

/**
 * How many rows a block of neighbouring pixels spans: with its columns,
 * the eight rays a packet holds.
 */
constexpr int surfaceBlockRows = 2;

/**
 * Return the ray a pixel's surface is traced by: the filmRay() through
 * the point the centre names inside the pixel, at a factor of 1 the
 * pixel's pixelRay().
 *
 * @param lens The lens at the drawn size.
 * @param centre Where in each pixel its ray passes (filmCentre()).
 * @param x The pixel's column.
 * @param y The pixel's row.
 */
Ray surfaceRay(const Lens& lens, const FilmCentre& centre, int x, int y);

/**
 * Return the surface pass: one walk a pixel, which ends at its first
 * answer.
 *
 * `launch` gives each free slot the surfaceRay() of the tile's next
 * pixel as a Camera ray and leaves it Probing, so the transport's step
 * passes it by. The pixels are taken block by block, surfaceBlockWidth
 * by surfaceBlockRows neighbours at a time, so eight rays that follow
 * each other in the pool lie beside each other on the film and fill a
 * packet. `fold` writes the Transport's cameraSurface of each answered
 * slot's ray and hit into the G-buffer and frees the slot.
 */
Pass surfacePass();

/**
 * Trace each drawn pixel's first surface by the surface pass and return
 * the G-buffer.
 *
 * The surfaces come back by rows from the top left, at the drawn size,
 * each the Transport's cameraSurface of the pixel's surfaceRay(): the
 * ray from the lens's middle through the middle of the shown pixel the
 * film plan's phase aims the drawn pixel at. Each surface reads only
 * its own ray, so the vector is the same whichever Execution plugin
 * extends the rays and on any thread count. The workers take tiles of
 * surfaceTileSide pixels.
 *
 * @param renderer The renderer: its step's drawn lens and film plan,
 *        its Execution plugin, the Transport's state of the frame
 *        (Renderer.drawing.transported), its threads and observers.
 */
std::vector<Surface> tracedSurfaces(Renderer& renderer);

} // namespace kodanuki
