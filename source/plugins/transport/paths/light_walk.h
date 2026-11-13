#pragma once

#include "plugins/transport/paths/strategy.h"
#include "plugins/transport/paths/trace.h"

#include <cstdint>

namespace kodanuki {

/**
 * Start a walk from a light, with its lamp, its point and its first ray.
 *
 * Pick at event 0 chooses the lamp by the strategies' lamp shares.
 * LightU/V place the point evenly on it. MixturePart and DirectionU/V
 * draw the direction by the emission members. The throughput is the
 * lamp's emission times the cosine at the lamp over the share, the
 * point's density and the direction's density, and the weighing starts
 * by weighingAtLamp(). Without a lamp that starts paths, or where the
 * emission draws nothing in front of the lamp, the walk is done at once.
 *
 * @param tracing What the transport reads.
 * @param start The use, the numbers and the wavelengths.
 */
LightWalk lightWalkStarted(const Tracing& tracing, const LightStart& start);

/**
 * Start the light pass's path of an index: a Joining walk on the stream
 * of the frame's seed hashed with the pass's tag, unsequenced, its
 * wavelengths drawn by LightHue at event 0.
 *
 * @param tracing What the transport reads.
 * @param index The path's index across the whole pass.
 */
LightWalk passWalkStarted(const Tracing& tracing, std::uint32_t index);

/**
 * Start the light subpath an eye walk leads with: a Leading walk on the
 * stream of the eye's hashed with the lead's tag, indexed by the eye's
 * sample, unsequenced, on the eye's wavelengths.
 *
 * @param tracing What the transport reads.
 * @param path The eye path, fresh.
 */
LightWalk leadStarted(const Tracing& tracing, const Path& path);

/**
 * Return a light walk that is done and kept nothing: an eye walk's lead
 * where it leads with none.
 */
LightWalk idleLead();

/**
 * Return the ray a light walk waits on.
 *
 * A joining walk waits on its line to the lens, lineWanted(). A flying
 * walk waits on its ray as an Inside ray where it starts inside a solid,
 * and as a Scatter ray otherwise.
 *
 * @param tracing What the transport reads.
 * @param walk The walk, not done.
 */
Wanted lightRayWanted(const Tracing& tracing, const LightWalk& walk);

/**
 * Take the answer to a light walk's ray and step the walk in place to
 * its next ray or its end (rule 46).
 *
 * For the line to the lens, a clear line adds its radiance, as a colour,
 * to the walk's landing, and the walk scatters. For any other ray:
 * - A miss ends the walk.
 * - Otherwise the path takes what its medium kept along the ray and
 *   stands on the surface, shaded and worn as an eye walk's surface is.
 * - A joining walk at its first surface that is not sharp ends there.
 * - The weighing is carried over the hit; at the first surface with
 *   what aiming at the lamp from it weighs.
 * - A leading walk keeps the surface while it has room.
 * - Where a member gives a line to the lens, the walk waits on it.
 *   Otherwise it scatters.
 *
 * To scatter, the material draws as a walk from a light (FromLight) with
 * the event's DirectionU/V, Lobe and Side, and the throughput takes the
 * correction a shading normal needs for light carried forward. The hero
 * alone carries on past a parting bend. Roulette plays from the minimum
 * depth with a survival of the largest lane over the start, held under
 * 0.95, drawn by Survival. The weighing is carried by
 * lightWeighingAfterScatter(), whose reverse density is the bare
 * material's draw back toward the previous vertex. The walk ends at the
 * maximum depth.
 *
 * @param tracing What the transport reads.
 * @param walk The walk, not done, which is stepped.
 * @param answer The ray's hit and the registers.
 */
void lightWalkAnswered(const Tracing& tracing, LightWalk& walk, const Answer& answer);

}
