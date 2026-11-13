#include "plugins/backend/cpu/plugin.h"

#include "core/render.h"
#include "core/step.h"
#include "core/worker.h"

#include <functional>
#include <thread>
#include <vector>

namespace kodanuki {

Crew cpuWorkers(const Renderer& renderer) {
	return Crew {
		.workers  = renderer.options.choices.threads,
		.capacity = poolCapacity,
	};
}

void cpuHosted(const Renderer&, int count, const std::function<void(int worker)>& run) {
	std::vector<std::thread> threads;
	for (int worker = 0; worker < count; ++worker) {
		threads.emplace_back(run, worker);
	}
	for (std::thread& thread : threads) {
		thread.join();
	}
}

bool cpuRan(const Renderer& renderer, Worker& worker, const Plugin& plugin, LoopStep step) {
	kernelOf(plugin, step)(renderer, worker);
	return true;
}

Plugin backendCpuPlugin() {
	Hooks hooks       = noHooks();
	hooks.openWorkers = cpuWorkers;
	hooks.hostWorkers = cpuHosted;
	hooks.ranStep     = cpuRan;
	return Plugin {
		.name   = "backend/cpu",
		.point  = Point::Backend,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
