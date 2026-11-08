#pragma once

#include "transport/strategy.h"

namespace kodanuki {

/**
 * An eye walk's vertex and one surface of its light subpath.
 */
struct Connection {
	/**
	 * The path, standing on its vertex.
	 */
	const Path& path;
	/**
	 * The extras' state beside it, for the eye's direction density.
	 */
	const Companions& companions;
	/**
	 * The kept light surface.
	 */
	const LightVertex& light;
	/**
	 * The lamp the light walk left.
	 */
	std::uint32_t lamp;
};

/**
 * Add a line from the eye walk's vertex to every surface its light
 * subpath kept: the Connect member at the Vertex moment.
 *
 * A vertex connects where:
 * - it stands in the open and is not exact;
 * - the kept surface is connectable;
 * - the joined path has no more surfaces than an eye walk stands on;
 * - the line leaves each surface on the side the path arrived from;
 * - the line is longer than both lifts.
 *
 * A connection that brings nothing asks no line. Lines are added in the
 * order of the kept surfaces, after any earlier member's.
 *
 * @param tracing What the transport reads.
 * @param visit The path on its vertex, the light subpath and the lines.
 */
void connectStep(const Tracing& tracing, const StrategyVisit& visit);

/**
 * Return the line that connects the eye walk's vertex to one kept light
 * surface, weighed, or a line that brings nothing.
 *
 * The radiance is the product of:
 * - the eye's throughput, its response toward the surface and its
 *   cosine about the shading normal;
 * - the surface's response toward the eye, its cosine and its
 *   throughput, with the correction a shading normal needs for light
 *   carried forward;
 * - one over the squared distance;
 * - the share connectDensities() gives the connection.
 *
 * Where both sides are dispersed, the product keeps its hero alone,
 * collapsed once. The line runs from the eye's lifted vertex to the
 * surface's lifted point.
 *
 * @param tracing What the transport reads.
 * @param connection The path on its vertex and the kept surface.
 */
Line connectionLine(const Tracing& tracing, const Connection& connection);

}
