#include "plugins/execution/single/plugin.h"

#include "core/frame.h"
#include "core/render.h"
#include "core/step.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

struct SingleCounts {
	std::array<std::atomic<std::uint64_t>, rayKindCount> rays;
};

SingleCounts& singleCounts() {
	static SingleCounts counts = {
		.rays = {},
	};

	return counts;
}

void singleExtended(const Renderer& renderer, Worker& worker) {
	Pool& pool                                       = worker.pool;
	std::array<std::uint64_t, rayKindCount> extended = {};
	for (std::size_t slot = 0; slot < std::size_t(pool.capacity); ++slot) {
		if (!waitsOnRay(pool.phases[slot])) {
			continue;
		}
		pool.hits[slot]     = intersect(renderer.world->geometry, pool.rays[slot]);
		pool.answered[slot] = 1;
		extended[std::size_t(pool.kinds[slot])] += 1;
	}
	for (std::size_t kind = 0; kind < extended.size(); ++kind) {
		singleCounts().rays[kind].fetch_add(extended[kind], std::memory_order_relaxed);
	}
}

std::vector<Counter> singlePublished(const Renderer&) {
	std::vector<Counter> counters;
	for (std::size_t kind = 0; kind < std::size_t(rayKindCount); ++kind) {
		counters.push_back(Counter { .name = "rays." + rayKindName(RayKind(kind)), .value = double(singleCounts().rays[kind].load()) });
	}
	return counters;
}

Plugin executionSinglePlugin() {
	Hooks hooks      = noHooks();
	hooks.extendRays = singleExtended;
	hooks.published  = singlePublished;
	return Plugin {
		.name   = "execution/single",
		.point  = Point::Execution,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
