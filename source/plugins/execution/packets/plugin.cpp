#include "plugins/execution/packets/plugin.h"

#include "core/frame.h"
#include "core/render.h"
#include "core/step.h"
#include "core/worker.h"
#include "plugins/execution/packets/answer.h"
#include "plugins/execution/packets/queue.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

struct PacketCount {
	std::uint64_t packets;
	std::uint64_t lanes;
};

struct PacketWorker {
	RayQueues queues;
	WaveTricks tricks;
	std::array<PacketCount, rayKindCount> counted;
};

struct PacketCounts {
	std::array<std::atomic<std::uint64_t>, rayKindCount> packets;
	std::array<std::atomic<std::uint64_t>, rayKindCount> lanes;
};

PacketWorker& packetWorker() {
	thread_local PacketWorker state = {
		.queues  = emptyQueues(std::size_t(poolCapacity / 8)),
		.tricks  = waveTricks(),
		.counted = {},
	};

	return state;
}

PacketCounts& packetCounts() {
	static PacketCounts counts = {
		.packets = {},
		.lanes   = {},
	};

	return counts;
}

void countedPacket(const Packet& packet, PacketWorker& state) {
	PacketCount& counted = state.counted[std::size_t(packet.kind)];
	counted.packets += 1;
	counted.lanes += std::uint64_t(std::popcount(packet.present));
}

void flushedCounts(PacketWorker& state) {
	PacketCounts& counts = packetCounts();
	for (std::size_t kind = 0; kind < state.counted.size(); ++kind) {
		counts.packets[kind].fetch_add(state.counted[kind].packets, std::memory_order_relaxed);
		counts.lanes[kind].fetch_add(state.counted[kind].lanes, std::memory_order_relaxed);
		state.counted[kind] = PacketCount { .packets = 0, .lanes = 0 };
	}
}

void queuedSlots(const Pool& pool, PacketWorker& state) {
	for (std::uint32_t slot = 0; slot < std::uint32_t(pool.capacity); ++slot) {
		if (pool.answered[slot] != 0 || !waitsOnRay(pool.phases[slot])) {
			continue;
		}

		const HeldRay held = {
			.ray   = pool.rays[slot],
			.token = slot,
		};

		queued(state.queues, pool.kinds[slot], held);
	}
}

void answeredPacket(const Geometry& geometry, const Packet& packet, PacketWorker& state, Pool& pool) {
	countedPacket(packet, state);
	const PacketMet met = walkedPacket(geometry, packet, state.tricks);

	const PacketAnswer answer = {
		.met    = met,
		.tricks = state.tricks,
	};

	const std::array<Hit, packetWidth> hits = packetHits(geometry, packet, answer);
	for (int lane = 0; lane < packetWidth; ++lane) {
		if (((packet.present >> lane) & 1) == 0) {
			continue;
		}
		const std::uint32_t slot = packet.tokens[std::size_t(lane)];
		pool.hits[slot]          = hits[std::size_t(lane)];
		pool.answered[slot]      = 1;
	}
}

void packetsExtended(const Renderer& renderer, Worker& worker) {
	PacketWorker& state = packetWorker();
	queuedSlots(worker.pool, state);
	while (!drained(state.queues)) {
		answeredPacket(renderer.world->geometry, nextPacket(state.queues), state, worker.pool);
	}
	flushedCounts(state);
}

std::vector<Counter> packetsPublished(const Renderer&) {
	const PacketCounts& counts = packetCounts();
	std::vector<Counter> counters;
	for (std::size_t kind = 0; kind < std::size_t(rayKindCount); ++kind) {
		const std::string name = rayKindName(RayKind(kind));
		counters.push_back(Counter { .name = "rays." + name, .value = double(counts.lanes[kind].load()) });
		counters.push_back(Counter { .name = "packets." + name, .value = double(counts.packets[kind].load()) });
	}
	return counters;
}

std::vector<std::string> packetsNeeded() {
	return { "geometry/tree" };
}

Plugin executionPacketsPlugin() {
	Hooks hooks      = noHooks();
	hooks.extendRays = packetsExtended;
	hooks.published  = packetsPublished;
	hooks.needed     = packetsNeeded;
	return Plugin {
		.name   = "execution/packets",
		.point  = Point::Execution,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
