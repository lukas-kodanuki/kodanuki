#pragma once

#include "plugins/execution/packets/packet.h"
#include "plugins/transport/paths/trace.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A ray waiting to be traced, under its walk's token.
 */
struct HeldRay {
	/**
	 * The ray.
	 */
	Ray ray;
	/**
	 * The walk's place in its wave.
	 */
	std::uint32_t token;
};

/**
 * The rays of one kind, in eight buckets by the octant they run toward.
 */
struct RayQueue {
	/**
	 * Each octant's rays, taken from the back.
	 */
	std::array<std::vector<HeldRay>, 8> buckets;
};

/**
 * One queue per ray kind.
 *
 * The order the rays are drained in moves no byte of the frame.
 */
struct RayQueues {
	/**
	 * The queues, by RayKind.
	 */
	std::array<RayQueue, rayKindCount> kinds;
};

/**
 * Put a ray in its kind's queue, in the bucket of its octant.
 *
 * The queues are filled in place (rule 46).
 *
 * @param queues The queues.
 * @param kind The ray's kind.
 * @param held The ray and its token.
 */
void queued(RayQueues& queues, RayKind kind, const HeldRay& held);

/**
 * Take the next packet off the queues.
 *
 * The kind is the latest by RayKind that holds a whole packet, else the
 * latest that holds any ray, so Shadow rays go first, then Inside,
 * Scatter and Camera: the rays that free a walk to go on. Within the
 * kind the octant is the lowest with a whole packet, else the lowest
 * with any ray, and up to eight rays come off the back of its bucket.
 *
 * @param queues The queues, which give up the rays.
 */
Packet nextPacket(RayQueues& queues);

/**
 * Return whether no ray waits.
 *
 * @param queues The queues.
 */
bool drained(const RayQueues& queues);

/**
 * Return queues holding no ray, each bucket with room for a wave's
 * rays.
 *
 * @param room How many rays a bucket has room for.
 */
RayQueues emptyQueues(std::size_t room);

}
