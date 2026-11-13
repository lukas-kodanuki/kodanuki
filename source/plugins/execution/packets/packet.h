#pragma once

#include "plugins/geometry/tree/walk.h"
#include "plugins/transport/paths/trace.h"

#include <array>
#include <cstdint>

namespace kodanuki {

/**
 * How many rays a packet holds.
 */
constexpr int packetWidth = 8;

/**
 * Up to eight rays of one kind running toward one octant.
 */
struct Packet {
	/**
	 * Each lane's ray.
	 */
	std::array<Ray, packetWidth> rays;
	/**
	 * Each lane's walk token.
	 */
	std::array<std::uint32_t, packetWidth> tokens;
	/**
	 * Which lanes hold a ray, one bit each.
	 */
	std::uint8_t present;
	/**
	 * The octant every ray runs toward, one bit an axis, set where it
	 * falls.
	 */
	int descending;
	/**
	 * What the rays ask.
	 */
	RayKind kind;
};

/**
 * Which tricks of the packet walk are on.
 *
 * None of them changes a byte of any lane's answer.
 */
struct WaveTricks {
	/**
	 * Answer a Shadow packet with the kind of each hit alone.
	 */
	bool bareShadows;
	/**
	 * Settle the lanes of a surface leaf whose answer is found eight at
	 * once (laneVerdicts()): those that surely meet nothing, and those
	 * with no crossing or a single one, before the rest are solved lane
	 * by lane.
	 */
	bool missedLanes;
	/**
	 * Read the programs of a surface leaf that three or more lanes met
	 * once for all of them, eight points to a step (math::Batch), choose
	 * each lane's nearest program from those values, and read the
	 * duals of each chosen program once for its lanes
	 * (math::DualBatch).
	 */
	bool groupedHits;
};

/**
 * Where each lane of a packet stopped.
 */
struct PacketMet {
	/**
	 * Each lane's stop, Missed for a lane absent or through.
	 */
	std::array<Met, packetWidth> lanes;
	/**
	 * Which lanes met something, one bit each.
	 */
	std::uint8_t found;
};

/**
 * Walk a packet through the packed tree, eight lanes at once.
 *
 * Each lane goes through the cells the scalar walk would visit, in the
 * same order and with the same float spans: each node's middles are
 * half the sum of its planes, a child's span is halvedSpan()'s, written
 * for all lanes through the selects of largerOf() and smallerOf(), and
 * a child is entered by the lanes whose lower is below their upper. A
 * lane leaves the walk at its first stop. Each leaf is settled lane by
 * lane through surfaceMet() and solidMet(), so each lane's stop is the
 * scalar walk's to the bit. With missedLanes on, where two or more
 * lanes reach a surface leaf, the lanes whose answer is found eight at
 * once (laneVerdicts())
 * are settled first, and only the others are solved lane by lane. With
 * bareShadows on, a Shadow lane from outside asks a surface leaf only
 * whether it stops there (surfaceReached()), so its stop keeps the
 * scalar walk's kind and leaf alone.
 *
 * @param geometry The tree and the scene.
 * @param packet The rays.
 * @param tricks The tricks that are on.
 */
PacketMet walkedPacket(const Geometry& geometry, const Packet& packet, const WaveTricks& tricks);

/**
 * Return the tricks the Waves member runs with: all on.
 */
WaveTricks waveTricks();

}
