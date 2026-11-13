#pragma once

#include "plugins/transport/paths/strategy.h"

namespace kodanuki {

/**
 * Return the strategies with the lamps' shares and the join share the
 * frame traces with.
 *
 * The lamp shares are lampShares() where Connect is on, or where Join
 * is on and some sharp surface may relay a path (the mirror balls are
 * not empty), and empty otherwise or where no lamp starts paths. The
 * join share is zero unless Join is on, the balls are not empty and some
 * lamp has a share.
 *
 * @param strategies The members.
 * @param lights The lights.
 * @param mirrors The mirror balls.
 */
Strategies withLightShares(const Strategies& strategies, const Lights& lights, const MirrorBalls& mirrors);

/**
 * Return the line that joins a light path's surface to the lens: the
 * Join member at a light walk.
 *
 * - The point on the lens is the eye through a pinhole, or a point drawn
 *   over the lens by JoinLensU/V at the path's event.
 * - The surface must lie ahead of the lens, and its line must land
 *   inside the frame (rasterOf()).
 * - The lens and the light that arrived must lie on one side of the
 *   surface.
 * - The surface must not be sharp, and the shading normal must face
 *   both.
 *
 * The weight is the share joinDensities() gives join. Its eye density is
 * the direction member's density for an eye path standing there, arrived
 * along the line from the lens, with the guide of the pixel the line
 * lands in. Its lens density is lensDensity() times the cosine at the
 * surface over the squared distance.
 *
 * The radiance is the throughput times the surface's response toward
 * the lens, times the cosine at the surface, times the importance. The
 * importance is the pixel count times lensDensity() at the view cosine,
 * over the squared distance, with the correction a shading normal needs
 * for light carried forward. Where the line is no longer than the lift,
 * its lifted start lies inside a solid, or it brings nothing, there is
 * no line.
 *
 * @param tracing What the transport reads.
 * @param standing The light path on its surface and the registers.
 */
LensLine joinedLine(const Tracing& tracing, const LightStanding& standing);

}
