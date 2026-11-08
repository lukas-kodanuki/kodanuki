#pragma once

#include "extras/execution_correct_waves/packet.h"
#include "walk/walk.h"

#include <array>

namespace kodanuki {

/**
 * Where a packet's lanes stopped, and the tricks the walk ran with.
 */
struct PacketAnswer {
	/**
	 * Each lane's stop.
	 */
	const PacketMet& met;
	/**
	 * The tricks.
	 */
	const WaveTricks& tricks;
};

/**
 * Return each present lane's hit, a miss for one that met nothing; an
 * absent lane's hit is left unset.
 *
 * Each hit is hitOf() of the lane's stop. For a Shadow packet with
 * bareShadows on, only the kind is filled and the rest is a miss's.
 * With groupedHits on, three or more lanes that met one surface leaf read
 * every program of its list once for all of them in a math::Batch
 * (where the list holds more than one), each lane's values to the bit
 * those of its own scalar run, and choose each lane's nearest program
 * from them; four or more lanes that chose one program then read it
 * once in a math::DualBatch, and each lane's hit is leafHit() of its
 * own field (fewer read it on their own, nearestSurfaceHit()).
 *
 * @param geometry The tree and the scene.
 * @param packet The rays.
 * @param answer Where each lane stopped, and the tricks.
 */
std::array<Hit, packetWidth> packetHits(const Geometry& geometry, const Packet& packet, const PacketAnswer& answer);

}
