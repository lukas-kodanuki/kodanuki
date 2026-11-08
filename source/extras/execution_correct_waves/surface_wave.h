#pragma once

#include "extras/execution_correct_waves/packet.h"
#include "frame/execution.h"
#include "transport/trace.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * How many columns a block of a G-buffer band spans: four, so a block of
 * a band of surfaceBandRows rows fills a packet.
 */
constexpr int surfaceBlockWidth = 4;

/**
 * What the packets of a G-buffer band are traced and shaded with.
 */
struct SurfaceWave {
	/**
	 * The band.
	 */
	const SurfaceBand& band;
	/**
	 * The tricks the packet walk runs with.
	 */
	WaveTricks tricks;
	/**
	 * The registers the patterns run in.
	 */
	std::vector<double>& registers;
};

/**
 * Return a block's camera rays as packets, one an octant: the
 * surfaceRay() of each of the block's pixels, of the columns from x up to
 * surfaceBlockWidth of them, of every row of the band.
 *
 * Each ray goes to the packet of its octant (descendingOf()), lanes
 * filled from the first in row order; each lane's token is its pixel's
 * index in the band. An octant no ray runs toward holds an empty packet.
 *
 * @param band The rows.
 * @param x The block's first column.
 */
std::array<Packet, 8> blockPackets(const SurfaceBand& band, int x);

/**
 * Return the surface of each lane of a camera packet: cameraSurface() of
 * its ray and of the hit the packet walk gives it, which is the scalar
 * walk's to the bit; missedSurface() for an absent lane.
 *
 * @param wave The band, the tricks and the registers.
 * @param packet The camera rays.
 */
std::array<Surface, packetWidth> laneSurfaces(const SurfaceWave& wave, const Packet& packet);

/**
 * Trace a band's surfaces in packets of neighbouring pixels by the
 * packet walk: the Waves member of surfacesOf().
 *
 * The band is cut into blocks of surfaceBlockWidth columns, and each
 * block's rays are traced eight at a time by blockPackets(). The surfaces
 * come back by rows, the same to the byte as bandOneAtATime().
 *
 * @param band The rows.
 */
std::vector<Surface> bandInWaves(const SurfaceBand& band);

}
