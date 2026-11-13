#include "plugins/execution/packets/queue.h"

#include "plugins/geometry/tree/span.h"

#include <cstddef>

namespace kodanuki {

struct QueuePick {
	int kind;
	int octant;
};

void queued(RayQueues& queues, RayKind kind, const HeldRay& held) {
	queues.kinds[std::size_t(kind)].buckets[std::size_t(descendingOf(held.ray))].push_back(held);
}

int fullestOctant(const RayQueue& queue, std::size_t least) {
	for (int octant = 0; octant < 8; ++octant) {
		if (queue.buckets[std::size_t(octant)].size() >= least) {
			return octant;
		}
	}
	return -1;
}

QueuePick pickedBucket(const RayQueues& queues, std::size_t least) {
	for (int kind = rayKindCount - 1; kind >= 0; --kind) {
		const int octant = fullestOctant(queues.kinds[std::size_t(kind)], least);
		if (octant >= 0) {
			return QueuePick {
				.kind   = kind,
				.octant = octant,
			};
		}
	}
	return QueuePick {
		.kind   = -1,
		.octant = -1,
	};
}

Packet emptyPacket() {
	return Packet {
		.rays       = {},
		.tokens     = {},
		.present    = 0,
		.descending = 0,
		.kind       = RayKind::Camera,
	};
}

Packet nextPacket(RayQueues& queues) {
	const QueuePick whole = pickedBucket(queues, packetWidth);
	const QueuePick pick  = whole.kind >= 0 ? whole : pickedBucket(queues, 1);
	Packet packet         = emptyPacket();
	if (pick.kind < 0) {
		return packet;
	}
	std::vector<HeldRay>& bucket = queues.kinds[std::size_t(pick.kind)].buckets[std::size_t(pick.octant)];
	packet.kind                  = RayKind(pick.kind);
	packet.descending            = pick.octant;
	for (int lane = 0; lane < packetWidth && !bucket.empty(); ++lane) {
		packet.rays[std::size_t(lane)]   = bucket.back().ray;
		packet.tokens[std::size_t(lane)] = bucket.back().token;
		packet.present                   = std::uint8_t(packet.present | (1u << lane));
		bucket.pop_back();
	}
	return packet;
}

bool drained(const RayQueues& queues) {
	for (const RayQueue& queue : queues.kinds) {
		for (const std::vector<HeldRay>& bucket : queue.buckets) {
			if (!bucket.empty()) {
				return false;
			}
		}
	}
	return true;
}

RayQueues emptyQueues(std::size_t room) {
	RayQueues queues = {};
	for (RayQueue& queue : queues.kinds) {
		for (std::vector<HeldRay>& bucket : queue.buckets) {
			bucket.reserve(room);
		}
	}
	return queues;
}

}
